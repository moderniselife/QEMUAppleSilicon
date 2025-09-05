/*
 * Apple Always-On Processor.
 *
 * Copyright (c) 2025 Visual Ehrmanntraut.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "exec/memattrs.h"
#include "hw/misc/apple-silicon/a7iop/rtkit.h"
#include "hw/misc/apple-silicon/aop.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "system/dma.h"

// #define DEBUG_AOP

#ifdef DEBUG_AOP
#include "qemu/cutils.h"

#define DHEXDUMP(a, b, c) qemu_hexdump(stderr, a, b, c)
#define DPRINTF(v, ...) fprintf(stderr, v, ##__VA_ARGS__)
#define AOP_LOG_MSG(ep, msg)                                                  \
    do {                                                                      \
        fprintf(stderr, "AOP: message: ep=%u msg=0x" HWADDR_FMT_plx "\n", ep, \
                msg);                                                         \
    } while (0)
#else
#define DHEXDUMP(a, b, c) \
    do {                  \
    } while (0)
#define DPRINTF(v, ...) \
    do {                \
    } while (0)
#define AOP_LOG_MSG(ep, msg) \
    do {                     \
    } while (0)
#endif

#define TXOK_GUARD(v)          \
    do {                       \
        MemTxResult res = (v); \
        if (res != MEMTX_OK) { \
            return res;        \
        }                      \
    } while (0)

// IOP -> AP
#define OP_HELLO (0x80)
#define OP_REQUEST_REGION_BYTES (0x81)
#define OP_SET_RX_QUEUE_BY_ADDR (0x82)
#define OP_SET_TX_QUEUE_BY_ADDR (0x83)
#define OP_SET_HISTORICAL_QUEUE_BY_ADDR (0x84)
#define OP_TX_SIGNAL (0x85)
#define OP_ACK_START_QUEUE (0x86)
#define OP_ACK_STOP_QUEUE (0x87)
#define OP_REQUEST_REGION_CHUNKS (0x89)
#define OP_SET_RX_QUEUE_BY_REGION_OFFSET (0x8A)
#define OP_SET_TX_QUEUE_BY_REGION_OFFSET (0x8B)
#define OP_SET_HISTORICAL_QUEUE_BY_REGION_OFFSET (0x8C)
#define OP_ACK_HIBERNATE_QUEUE (0xC3)

// AP -> IOP
#define OP_ACK_HELLO (0xA0)
#define OP_ACK_REQUEST_REGION (0xA1)
#define OP_RX_SIGNAL (0xA2)
#define OP_START_QUEUE (0xA3)
#define OP_STOP_QUEUE (0xA4)
#define OP_HIBERNATE_QUEUE (0xC2)

#define MSG_OP(_op) ((uint64_t)(OP_##_op) << 48)

#define MSG_HELLO(_ver) (MSG_OP(HELLO) | (_ver & 0xFFFFFFFFFFFF))
#define MSG_REQUEST_REGION_BYTES(_len) \
    (MSG_OP(REQUEST_REGION_BYTES) | (_len & 0xFFFFFFFFFFFF))
#define MSG_SET_RX_QUEUE_BY_ADDR(_addr) (MSG_OP(SET_RX_QUEUE_BY_ADDR) | (_addr))
#define MSG_SET_TX_QUEUE_BY_ADDR(_addr) (MSG_OP(SET_TX_QUEUE_BY_ADDR) | (_addr))
#define MSG_TX_SIGNAL MSG_OP(TX_SIGNAL)

#define MSG_OP_GET(_val) ((uint8_t)(((_val) >> 48) & 0xFF))
#define MSG_ACK_REQUEST_REGION(_val) (_val & 0xFFFFFFFFFFFF)

#define RB_V7_IOP_MAGIC ((uint32_t)'IOP ')
#define RB_V7_AOP_MAGIC ((uint32_t)'AOP ')

#define SUB_PACKET_FLAG_CAT_REPORT (0)
#define SUB_PACKET_FLAG_CAT_COMMAND (1)
#define SUB_PACKET_FLAG_CAT_RESPONSE (2)
#define SUB_PACKET_FLAG_CAT_LONG_COMMAND (3)

#define SUB_PACKET_FLAG_CAT_GET(_cat) (((_cat) >> 4) & 0xFF)
#define SUB_PACKET_FLAG_CAT(_cat) (((_cat) & 0xFF) << 4)

#define PACKET_TYPE_SET_PROPERTY (0x4)
#define PACKET_TYPE_GET_PROPERTY (0xA)
#define PACKET_TYPE_SET_NAMED_PROPERTY (0x10)
#define PACKET_TYPE_GET_NAMED_PROPERTY (0x11)
#define PACKET_TYPE_READY_REPORT (0xC0)
#define PACKET_TYPE_MSG_REPORT (0xD0)

#define READY_REPORT_FLAG_MUX BIT(3)
#define READY_REPORT_FLAG_SPU_APP BIT(31)

#define RB_ENTRY_LEN (16)
#define PACKET_LEN (16)
#define SUB_PACKET_LEN (24)
#define READY_REPORT_LEN (44)

struct AppleAOPClass {
    /*< private >*/
    AppleRTKitClass base_class;

    /*< public >*/
    DeviceRealize parent_realize;
    ResettablePhases parent_reset;
};

struct AppleAOPState {
    /*< private >*/
    AppleRTKit parent_obj;

    /*< public >*/
    MemoryRegion ascv2_iomem;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    GList *endpoints;
    uint32_t align;
};

typedef enum {
    EP_STATE_POWERED_OFF,
    EP_STATE_AWAITING_HELLO,
    EP_STATE_AWAITING_RX_ACK,
    EP_STATE_AWAITING_TX_ACK,
    EP_STATE_IDLE,
} AppleAOPEndpointState;

struct AppleAOPEndpoint {
    AppleAOPState *aop;
    QemuMutex mutex;
    uint32_t num;
    uint32_t rx_off;
    uint32_t tx_off;
    uint16_t seq;
    void *opaque;
    const AppleAOPEndpointDescription *descr;
    AppleAOPEndpointState state;
};

static MemTxResult apple_aop_ep_init_rb(AppleAOPEndpoint *s, uint32_t addr,
                                        uint32_t len)
{
    TXOK_GUARD(stl_le_dma(&s->aop->dma_as, addr, len - (s->aop->align * 3),
                          MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(
        stw_le_dma(&s->aop->dma_as, addr + 4, 6, MEMTXATTRS_UNSPECIFIED));
    return stw_le_dma(&s->aop->dma_as, addr + 6, 7, MEMTXATTRS_UNSPECIFIED);
}

static MemTxResult apple_aop_ep_set_rptr(AppleAOPEndpoint *s, uint32_t addr,
                                         uint32_t val)
{
    return stl_le_dma(&s->aop->dma_as, addr + s->aop->align, val,
                      MEMTXATTRS_UNSPECIFIED);
}

static MemTxResult apple_aop_ep_get_rptr(AppleAOPEndpoint *s, uint32_t addr,
                                         uint32_t *val)
{
    return ldl_le_dma(&s->aop->dma_as, addr + s->aop->align, val,
                      MEMTXATTRS_UNSPECIFIED);
}

static MemTxResult apple_aop_ep_set_wptr(AppleAOPEndpoint *s, uint32_t addr,
                                         uint32_t val)
{
    return stl_le_dma(&s->aop->dma_as, addr + s->aop->align * 2, val,
                      MEMTXATTRS_UNSPECIFIED);
}

static MemTxResult apple_aop_ep_get_wptr(AppleAOPEndpoint *s, uint32_t addr,
                                         uint32_t *val)
{
    return ldl_le_dma(&s->aop->dma_as, addr + s->aop->align * 2, val,
                      MEMTXATTRS_UNSPECIFIED);
}

static MemTxResult apple_aop_ep_read_rb_entry(AppleAOPEndpoint *s,
                                              uint32_t addr, uint32_t *length)
{
    uint32_t magic;

    TXOK_GUARD(
        ldl_be_dma(&s->aop->dma_as, addr, &magic, MEMTXATTRS_UNSPECIFIED));
    if (magic != RB_V7_AOP_MAGIC) {
        return MEMTX_DECODE_ERROR;
    }

    TXOK_GUARD(
        ldl_le_dma(&s->aop->dma_as, addr + 4, length, MEMTXATTRS_UNSPECIFIED));

    return MEMTX_OK;
}

static MemTxResult apple_aop_ep_write_rb_entry(AppleAOPEndpoint *s,
                                               uint32_t addr, uint32_t length)
{
    TXOK_GUARD(stl_be_dma(&s->aop->dma_as, addr, RB_V7_IOP_MAGIC,
                          MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(
        stl_le_dma(&s->aop->dma_as, addr + 4, length, MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(dma_memory_set(&s->aop->dma_as, addr + 8, 0, 8,
                              MEMTXATTRS_UNSPECIFIED));

    return MEMTX_OK;
}

static MemTxResult
apple_aop_ep_read_sub_packet(AppleAOPEndpoint *s, uint32_t addr,
                             uint32_t *payload_len, uint8_t *category,
                             uint16_t *type, uint16_t *seq, uint64_t *timestamp,
                             uint32_t *out_len)
{
    uint8_t version;
    uint8_t flags;

    TXOK_GUARD(dma_memory_read(&s->aop->dma_as, addr + 4, &version,
                               sizeof(version), MEMTXATTRS_UNSPECIFIED));
    if (version != 2) {
        return MEMTX_DECODE_ERROR;
    }

    TXOK_GUARD(
        ldl_le_dma(&s->aop->dma_as, addr, payload_len, MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(dma_memory_read(&s->aop->dma_as, addr + 5, &flags, sizeof(flags),
                               MEMTXATTRS_UNSPECIFIED));
    *category = SUB_PACKET_FLAG_CAT_GET(flags);
    TXOK_GUARD(
        lduw_le_dma(&s->aop->dma_as, addr + 6, type, MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(
        lduw_le_dma(&s->aop->dma_as, addr + 8, seq, MEMTXATTRS_UNSPECIFIED));
    if (timestamp != NULL) {
        TXOK_GUARD(ldq_le_dma(&s->aop->dma_as, addr + 10, timestamp,
                              MEMTXATTRS_UNSPECIFIED));
    }
    TXOK_GUARD(ldl_le_dma(&s->aop->dma_as, addr + 20, out_len,
                          MEMTXATTRS_UNSPECIFIED));

    return MEMTX_OK;
}

static MemTxResult apple_aop_ep_write_sub_packet(
    AppleAOPEndpoint *s, uint32_t addr, uint32_t payload_len, uint8_t category,
    uint16_t type, uint16_t seq, uint64_t timestamp, uint32_t out_len)
{
    TXOK_GUARD(
        stl_le_dma(&s->aop->dma_as, addr, payload_len, MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(dma_memory_set(&s->aop->dma_as, addr + 4, 2, 1,
                              MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(dma_memory_set(&s->aop->dma_as, addr + 5,
                              SUB_PACKET_FLAG_CAT(category), 1,
                              MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(
        stw_le_dma(&s->aop->dma_as, addr + 6, type, MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(
        stw_le_dma(&s->aop->dma_as, addr + 8, seq, MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(stq_le_dma(&s->aop->dma_as, addr + 10, timestamp,
                          MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(dma_memory_set(&s->aop->dma_as, addr + 18, 0, 2,
                              MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(stl_le_dma(&s->aop->dma_as, addr + 20, out_len,
                          MEMTXATTRS_UNSPECIFIED));

    return MEMTX_OK;
}

static MemTxResult apple_aop_ep_read_packet(AppleAOPEndpoint *s, uint32_t addr,

                                            uint16_t *seq, uint64_t *timestamp)
{
    uint8_t version;

    TXOK_GUARD(dma_memory_read(&s->aop->dma_as, addr, &version, sizeof(version),
                               MEMTXATTRS_UNSPECIFIED));
    if (version != 2) {
        return MEMTX_DECODE_ERROR;
    }

    if (seq != NULL) {
        TXOK_GUARD(lduw_le_dma(&s->aop->dma_as, addr + 1, seq,
                               MEMTXATTRS_UNSPECIFIED));
    }
    if (timestamp != NULL) {
        TXOK_GUARD(ldq_le_dma(&s->aop->dma_as, addr + 8, timestamp,
                              MEMTXATTRS_UNSPECIFIED));
    }

    return MEMTX_OK;
}

static MemTxResult apple_aop_ep_write_packet(AppleAOPEndpoint *s, uint32_t addr,

                                             uint16_t seq, uint64_t timestamp)
{
    TXOK_GUARD(
        dma_memory_set(&s->aop->dma_as, addr, 2, 1, MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(
        stw_le_dma(&s->aop->dma_as, addr + 1, seq, MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(dma_memory_set(&s->aop->dma_as, addr + 3, 0, 5,
                              MEMTXATTRS_UNSPECIFIED));
    TXOK_GUARD(stq_le_dma(&s->aop->dma_as, addr + 8, timestamp,
                          MEMTXATTRS_UNSPECIFIED));

    return MEMTX_OK;
}

static MemTxResult apple_aop_ep_send_packet_full(AppleAOPEndpoint *s,
                                                 uint16_t type,
                                                 uint8_t category, uint16_t seq,
                                                 const void *payload,
                                                 uint32_t len, uint32_t out_len)
{
    AppleRTKit *rtk;
    uint32_t wptr;
    uint32_t data_off;
    uint64_t timestamp;

    if (s->state != EP_STATE_IDLE) {
        return MEMTX_ERROR;
    }

    rtk = APPLE_RTKIT(s->aop);
    timestamp = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    TXOK_GUARD(apple_aop_ep_get_wptr(s, s->tx_off, &wptr));

    data_off = s->aop->align * 3;

    if (ROUND_UP(data_off + wptr + RB_ENTRY_LEN + PACKET_LEN + SUB_PACKET_LEN +
                     len,
                 s->aop->align) > s->descr->tx_len) {
        wptr = 0;
    }

    TXOK_GUARD(apple_aop_ep_write_rb_entry(s, s->tx_off + data_off + wptr,
                                           PACKET_LEN + SUB_PACKET_LEN + len));
    wptr += RB_ENTRY_LEN;
    TXOK_GUARD(apple_aop_ep_write_packet(s, s->tx_off + data_off + wptr, s->seq,
                                         timestamp));
    wptr += PACKET_LEN;
    TXOK_GUARD(apple_aop_ep_write_sub_packet(s, s->tx_off + data_off + wptr,
                                             len, category, type, seq,
                                             timestamp, out_len));
    wptr += SUB_PACKET_LEN;
    TXOK_GUARD(dma_memory_write(&s->aop->dma_as, s->tx_off + data_off + wptr,
                                payload, len, MEMTXATTRS_UNSPECIFIED));
    wptr += len;
    TXOK_GUARD(
        apple_aop_ep_set_wptr(s, s->tx_off, ROUND_UP(wptr, s->aop->align)));

    apple_rtkit_send_user_msg(rtk, s->num, MSG_TX_SIGNAL);

    if (s->seq < 0xFFFF) {
        s->seq += 1;
    } else {
        s->seq = 0;
    }

    return MEMTX_OK;
}

MemTxResult apple_aop_ep_send_report_locked(AppleAOPEndpoint *s,
                                            uint16_t packet_type,
                                            const void *payload,
                                            uint32_t payload_len,
                                            uint32_t out_len)
{
    return apple_aop_ep_send_packet_full(s, packet_type,
                                         SUB_PACKET_FLAG_CAT_REPORT, s->seq,
                                         payload, payload_len, out_len);
}

MemTxResult apple_aop_ep_send_reply_locked(AppleAOPEndpoint *s,
                                           uint16_t packet_type, uint16_t seq,
                                           const void *payload,
                                           uint32_t payload_len,
                                           uint32_t out_len)
{
    return apple_aop_ep_send_packet_full(s, packet_type,
                                         SUB_PACKET_FLAG_CAT_RESPONSE, seq,
                                         payload, payload_len, out_len);
}

MemTxResult apple_aop_ep_send_report(AppleAOPEndpoint *s, uint16_t packet_type,
                                     const void *payload, uint32_t payload_len,
                                     uint32_t out_len)
{
    QEMU_LOCK_GUARD(&s->mutex);

    return apple_aop_ep_send_report_locked(s, packet_type, payload, payload_len,
                                           out_len);
}

MemTxResult apple_aop_ep_send_reply(AppleAOPEndpoint *s, uint16_t packet_type,
                                    uint16_t seq, const void *payload,
                                    uint32_t payload_len, uint32_t out_len)
{
    QEMU_LOCK_GUARD(&s->mutex);

    return apple_aop_ep_send_reply_locked(s, packet_type, seq, payload,
                                          payload_len, out_len);
}

static MemTxResult apple_aop_ep_write_ready_report(AppleAOPEndpoint *s,
                                                   void *buf)
{
    char service_name[32] = { 0 };
    uint32_t flags;

    strncpy(service_name, s->descr->service_name, sizeof(service_name));
    memcpy(buf, service_name, sizeof(service_name));
    stl_le_p(buf + sizeof(service_name), s->descr->service_id);
    switch (s->descr->type) {
    case AOP_EP_TYPE_HID:
        flags = 0;
        break;
    case AOP_EP_TYPE_MUX:
        flags = READY_REPORT_FLAG_MUX;
        break;
    case AOP_EP_TYPE_APP:
        flags = READY_REPORT_FLAG_SPU_APP;
        break;
    default:
        g_assert_not_reached();
    }
    stl_le_p(buf + sizeof(service_name) + 4, flags);
    stl_le_p(buf + sizeof(service_name) + 8, s->descr->interface_num);

    return MEMTX_OK;
}

static bool apple_aop_ep_rx_empty(AppleAOPEndpoint *s)
{
    uint32_t wptr;
    uint32_t rptr;

    TXOK_GUARD(apple_aop_ep_get_wptr(s, s->rx_off, &wptr));
    TXOK_GUARD(apple_aop_ep_get_rptr(s, s->rx_off, &rptr));

    return wptr == rptr;
}

static MemTxResult apple_aop_ep_recv_packet_locked(
    AppleAOPEndpoint *s, uint16_t *packet_type, uint8_t *category,
    uint16_t *seq, void **payload, uint32_t *len, uint32_t *out_len)
{
    uint32_t rptr;
    uint32_t data_off;
    uint32_t entry_len;

    *payload = NULL;

    if (apple_aop_ep_rx_empty(s)) {
        return MEMTX_OK;
    }

    TXOK_GUARD(apple_aop_ep_get_rptr(s, s->rx_off, &rptr));

    data_off = s->aop->align * 3;

    TXOK_GUARD(
        apple_aop_ep_read_rb_entry(s, s->rx_off + data_off + rptr, &entry_len));
    rptr += RB_ENTRY_LEN;
    TXOK_GUARD(
        apple_aop_ep_read_packet(s, s->rx_off + data_off + rptr, NULL, NULL));
    rptr += PACKET_LEN;
    TXOK_GUARD(apple_aop_ep_read_sub_packet(s, s->rx_off + data_off + rptr, len,
                                            category, packet_type, seq, NULL,
                                            out_len));
    rptr += SUB_PACKET_LEN;
    *payload = g_malloc0(*len);
    TXOK_GUARD(dma_memory_read(&s->aop->dma_as, s->rx_off + data_off + rptr,
                               *payload, *len, MEMTXATTRS_UNSPECIFIED));
    rptr += *len;
    rptr = ROUND_UP(rptr, s->aop->align);
    if (rptr >= s->descr->rx_len) {
        rptr = 0;
    }
    TXOK_GUARD(apple_aop_ep_set_rptr(s, s->rx_off, rptr));

    return MEMTX_OK;
}

static void apple_aop_ep_handle_message(void *opaque, uint32_t ep, uint64_t msg)
{
    AppleAOPEndpoint *s = opaque;
    AppleRTKit *rtk;
    MemTxResult ret;
    uint8_t ready_report_buf[READY_REPORT_LEN];
    uint16_t type;
    uint8_t category;
    uint16_t seq;
    void *payload;
    uint32_t len;
    uint32_t out_len;
    void *payload_out;

    rtk = APPLE_RTKIT(s->aop);

    AOP_LOG_MSG(ep, msg);

    QEMU_LOCK_GUARD(&s->mutex);

    switch (s->state) {
    case EP_STATE_POWERED_OFF:
        qemu_log_mask(LOG_GUEST_ERROR, "Unexpected msg in POWERED_OFF state.");
        break;
    case EP_STATE_AWAITING_HELLO:
        if (MSG_OP_GET(msg) != OP_ACK_HELLO) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Unexpected msg 0x%X in `AWAITING_HELLO` state.",
                          MSG_OP_GET(msg));
            break;
        }

        apple_rtkit_send_user_msg(rtk, s->num,
                                  MSG_REQUEST_REGION_BYTES(s->descr->rx_len));
        s->state = EP_STATE_AWAITING_RX_ACK;
        break;
    case EP_STATE_AWAITING_RX_ACK:
        if (MSG_OP_GET(msg) != OP_ACK_REQUEST_REGION) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Unexpected msg 0x%X in `AWAITING_RX_ACK` state.",
                          MSG_OP_GET(msg));
            break;
        }

        s->rx_off = MSG_ACK_REQUEST_REGION(msg);
        ret = apple_aop_ep_init_rb(s, s->rx_off, s->descr->rx_len);
        if (ret != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Failed to initialise RX ringbuffer for `%s`: %d.",
                          s->descr->service_name, ret);
            break;
        }
        apple_rtkit_send_user_msg(rtk, s->num,
                                  MSG_SET_RX_QUEUE_BY_ADDR(s->rx_off));
        apple_rtkit_send_user_msg(rtk, s->num,
                                  MSG_REQUEST_REGION_BYTES(s->descr->tx_len));
        s->state = EP_STATE_AWAITING_TX_ACK;
        break;
    case EP_STATE_AWAITING_TX_ACK:
        if (MSG_OP_GET(msg) != OP_ACK_REQUEST_REGION) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Unexpected msg 0x%X in `AWAITING_TX_ACK` state.",
                          MSG_OP_GET(msg));
            break;
        }

        s->tx_off = MSG_ACK_REQUEST_REGION(msg);
        ret = apple_aop_ep_init_rb(s, s->tx_off, s->descr->tx_len);
        if (ret != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Failed to initialise TX ringbuffer for `%s`: %d.",
                          s->descr->service_name, ret);
            break;
        }
        apple_rtkit_send_user_msg(rtk, s->num,
                                  MSG_SET_TX_QUEUE_BY_ADDR(s->tx_off));

        s->state = EP_STATE_IDLE;
        ret = apple_aop_ep_write_ready_report(s, ready_report_buf);
        if (ret != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Failed to construct ready report for `%s`: %d.",
                          s->descr->service_name, ret);
            break;
        }
        ret = apple_aop_ep_send_report_locked(s, PACKET_TYPE_READY_REPORT,
                                              ready_report_buf,
                                              sizeof(ready_report_buf), 0);
        if (ret != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Failed to send ready report for `%s`: %d.",
                          s->descr->service_name, ret);
            break;
        }
        break;
    case EP_STATE_IDLE:
        if (MSG_OP_GET(msg) != OP_RX_SIGNAL) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Unexpected msg 0x%X in `IDLE` state.",
                          MSG_OP_GET(msg));
            break;
        }
        while (!apple_aop_ep_rx_empty(s)) {
            if (apple_aop_ep_recv_packet_locked(s, &type, &category, &seq,
                                                &payload, &len,
                                                &out_len) != MEMTX_OK) {
                qemu_log_mask(LOG_GUEST_ERROR, "Failed to receive message");
                break;
            }

            DPRINTF("Packet type: 0x%X\n", type);
            DHEXDUMP("RX", payload, len);

            payload_out = g_malloc0(out_len + sizeof(uint32_t));
            switch (type) {
            case PACKET_TYPE_GET_PROPERTY:
                if (s->descr->get_property == NULL) {
                    break;
                }

                stl_le_p(payload_out,
                         s->descr->get_property(
                             s->opaque, ldl_le_p(payload + sizeof(uint32_t)),
                             payload_out + sizeof(uint32_t)));
                break;
            default:
                if (s->descr->handle_command == NULL) {
                    break;
                }

                stl_le_p(payload_out,
                         s->descr->handle_command(
                             s->opaque, type, category, seq,
                             payload + sizeof(uint32_t), len - sizeof(uint32_t),
                             payload_out + sizeof(uint32_t),
                             out_len - sizeof(uint32_t)));
                break;
            }
            g_free(payload);
            DHEXDUMP("TX", payload_out, out_len + sizeof(uint32_t));
            if (apple_aop_ep_send_reply_locked(s, type, seq, payload_out,
                                               out_len + sizeof(uint32_t),
                                               0) != MEMTX_OK) {
                qemu_log_mask(LOG_GUEST_ERROR, "Failed to reply to message %d",
                              seq);
                break;
            }
            g_free(payload_out);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void apple_aop_ep_hello_foreach(gpointer data, gpointer user_data)
{
    AppleAOPEndpoint *s;
    AppleRTKit *rtk;

    s = (AppleAOPEndpoint *)data;
    rtk = APPLE_RTKIT(s->aop);

    QEMU_LOCK_GUARD(&s->mutex);

    apple_rtkit_send_user_msg(rtk, s->num, MSG_HELLO(8));
    s->state = EP_STATE_AWAITING_HELLO;
}

static void apple_aop_boot_done(void *opaque)
{
    AppleAOPState *s = opaque;

    g_list_foreach(s->endpoints, apple_aop_ep_hello_foreach, NULL);
}

static const AppleRTKitOps apple_aop_rtkit_ops = {
    .boot_done = apple_aop_boot_done,
};

static void ascv2_core_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                 unsigned size)
{
}

static uint64_t ascv2_core_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static const MemoryRegionOps ascv2_core_reg_ops = {
    .write = ascv2_core_reg_write,
    .read = ascv2_core_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 8,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .valid.unaligned = false,
};

static void apple_aop_realize(DeviceState *dev, Error **errp)
{
    AppleAOPState *s;
    AppleAOPClass *aopc;
    Object *obj;

    s = APPLE_AOP(dev);
    aopc = APPLE_AOP_GET_CLASS(dev);

    if (aopc->parent_realize != NULL) {
        aopc->parent_realize(dev, errp);
    }

    obj = object_property_get_link(OBJECT(dev), "dma-mr", &error_abort);

    s->dma_mr = MEMORY_REGION(obj);
    g_assert_nonnull(s->dma_mr);
    address_space_init(&s->dma_as, s->dma_mr, "aop.dma-as");
}

static void apple_aop_ep_reset_foreach(gpointer data, gpointer user_data)
{
    AppleAOPEndpoint *s;

    s = (AppleAOPEndpoint *)data;

    s->state = EP_STATE_POWERED_OFF;
    s->tx_off = 0;
    s->rx_off = 0;
}

static void apple_aop_reset_hold(Object *obj, ResetType type)
{
    AppleAOPState *s;
    AppleAOPClass *aopc;

    s = APPLE_AOP(obj);
    aopc = APPLE_AOP_GET_CLASS(obj);

    if (aopc->parent_reset.hold != NULL) {
        aopc->parent_reset.hold(obj, type);
    }

    g_list_foreach(s->endpoints, apple_aop_ep_reset_foreach, NULL);
}

static void apple_aop_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc;
    DeviceClass *dc;
    AppleAOPClass *aopc;

    rc = RESETTABLE_CLASS(klass);
    dc = DEVICE_CLASS(klass);
    aopc = APPLE_AOP_CLASS(klass);

    device_class_set_parent_realize(dc, apple_aop_realize,
                                    &aopc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, apple_aop_reset_hold, NULL,
                                       &aopc->parent_reset);
    dc->desc = "Apple Always-On Processor";
    dc->user_creatable = false;
    // dc->vmsd = &vmstate_apple_aop;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo apple_aop_info = {
    .name = TYPE_APPLE_AOP,
    .parent = TYPE_APPLE_RTKIT,
    .instance_size = sizeof(AppleAOPState),
    .class_size = sizeof(AppleAOPClass),
    .class_init = apple_aop_class_init,
};

static void apple_aop_register_types(void)
{
    type_register_static(&apple_aop_info);
}

type_init(apple_aop_register_types);

SysBusDevice *apple_aop_create(DTBNode *node, AppleA7IOPVersion version,
                               uint32_t rtkit_protocol_version)
{
    DeviceState *dev;
    AppleAOPState *s;
    SysBusDevice *sbd;
    AppleRTKit *rtk;
    DTBNode *child;
    DTBProp *prop;
    uint64_t *reg;

    dev = qdev_new(TYPE_APPLE_AOP);
    s = APPLE_AOP(dev);
    sbd = SYS_BUS_DEVICE(dev);
    rtk = APPLE_RTKIT(dev);
    dev->id = g_strdup("aop");

    child = dtb_get_node(node, "iop-aop-nub");
    g_assert_nonnull(child);

    prop = dtb_find_prop(node, "reg");
    g_assert_nonnull(prop);

    reg = (uint64_t *)prop->data;

    apple_rtkit_init(rtk, NULL, "AOP", reg[1], version, rtkit_protocol_version,
                     &apple_aop_rtkit_ops);

    memory_region_init_io(&s->ascv2_iomem, OBJECT(dev), &ascv2_core_reg_ops, s,
                          TYPE_APPLE_AOP ".ascv2-core-reg", reg[3]);
    sysbus_init_mmio(sbd, &s->ascv2_iomem);

    prop = dtb_find_prop(child, "aop-memory-alignment");
    s->align = prop == NULL ? 0x40 : ldl_le_p(prop->data);

    dtb_set_prop_u32(child, "pre-loaded", 1);

    return sbd;
}

AppleAOPEndpoint *apple_aop_ep_create(AppleAOPState *s, void *opaque,
                                      const AppleAOPEndpointDescription *descr)
{
    AppleAOPEndpoint *ep;
    AppleRTKit *rtk;

    g_assert_nonnull(descr);
    g_assert_cmpuint(descr->rx_len % s->align, ==, 0);
    g_assert_cmpuint(descr->tx_len % s->align, ==, 0);

    ep = g_new0(AppleAOPEndpoint, 1);
    rtk = APPLE_RTKIT(s);

    ep->aop = s;
    ep->num = g_list_length(s->endpoints);
    ep->opaque = opaque;
    ep->descr = descr;

    qemu_mutex_init(&ep->mutex);

    apple_rtkit_register_user_ep(rtk, ep->num, ep, apple_aop_ep_handle_message);

    s->endpoints = g_list_append(s->endpoints, ep);

    return ep;
}
