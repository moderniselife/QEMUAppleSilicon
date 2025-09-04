/*
 * Apple RTKit.
 *
 * Copyright (c) 2023-2025 Visual Ehrmanntraut (VisualEhrmanntraut).
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

#include "hw/misc/apple-silicon/a7iop/private.h"
#include "hw/misc/apple-silicon/a7iop/rtkit.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "trace.h"

const VMStateDescription vmstate_apple_rtkit = {
    .name = "AppleRTKit",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT8(ep0_status, AppleRTKit),
            VMSTATE_UINT32(protocol_version, AppleRTKit),
            VMSTATE_APPLE_A7IOP_MESSAGE(rollcall, AppleRTKit),
            VMSTATE_END_OF_LIST(),
        },
};

#define MSG_HELLO (1)
#define MSG_HELLO_ACK (2)
#define MSG_TYPE_PING (3)
#define MSG_TYPE_PING_ACK (4)
#define MSG_TYPE_SET_EP_STATUS (5)
#define MSG_TYPE_REQ_POWER (6)
#define MSG_GET_PSTATE(_x) ((_x) & 0xFFF) // TODO: Investigate this
#define PSTATE_SLPNOMEM (0x0)
#define PSTATE_WAIT_VR (0x201)
#define PSTATE_PWRGATE (0x202)
#define PSTATE_ON (0x220)
#define MSG_TYPE_POWER_ACK (7)
#define MSG_TYPE_ROLLCALL (8)
#define MSG_TYPE_POWER_NAP (10)
#define MSG_TYPE_AP_POWER (11)

static inline AppleA7IOPMessage *apple_rtkit_construct_msg(uint32_t ep,
                                                           uint64_t data)
{
    AppleA7IOPMessage *msg;
    AppleRTKitMessage *rtk_msg;

    msg = g_new0(AppleA7IOPMessage, 1);
    rtk_msg = (AppleRTKitMessage *)msg->data;
    rtk_msg->endpoint = ep;
    rtk_msg->msg = data;

    return msg;
}

static inline void apple_rtkit_send_msg(AppleRTKit *s, uint32_t ep,
                                        uint64_t data)
{
    apple_a7iop_send_ap(APPLE_A7IOP(s), apple_rtkit_construct_msg(ep, data));
}

void apple_rtkit_send_control_msg(AppleRTKit *s, uint32_t ep, uint64_t data)
{
    g_assert_cmpuint(ep, <, EP_USER_START);
    apple_rtkit_send_msg(s, ep, data);
}

void apple_rtkit_send_user_msg(AppleRTKit *s, uint32_t ep, uint64_t data)
{
    g_assert_cmpuint(ep, <, 256 - EP_USER_START);
    apple_rtkit_send_msg(s, ep + EP_USER_START, data);
}

static inline void apple_rtkit_register_ep(AppleRTKit *s, uint32_t ep,
                                           void *opaque,
                                           AppleRTKitEPHandler *handler,
                                           bool user)
{
    AppleRTKitEPData *data;

    g_assert_null(g_tree_lookup(s->endpoints, GUINT_TO_POINTER(ep)));

    data = g_new0(AppleRTKitEPData, 1);
    data->opaque = opaque;
    data->handler = handler;
    data->user = user;
    g_tree_insert(s->endpoints, GUINT_TO_POINTER(ep), data);
}

void apple_rtkit_register_control_ep(AppleRTKit *s, uint32_t ep, void *opaque,
                                     AppleRTKitEPHandler *handler)
{
    g_assert_cmpuint(ep, <, EP_USER_START);
    apple_rtkit_register_ep(s, ep, opaque, handler, false);
}

void apple_rtkit_register_user_ep(AppleRTKit *s, uint32_t ep, void *opaque,
                                  AppleRTKitEPHandler *handler)
{
    g_assert_cmpuint(ep, <, 224);
    apple_rtkit_register_ep(s, ep + EP_USER_START, opaque, handler, true);
}

static inline void apple_rtkit_unregister_ep(AppleRTKit *s, uint32_t ep)
{
    void *ep_data = g_tree_lookup(s->endpoints, GUINT_TO_POINTER(ep));
    if (ep_data != NULL) {
        g_tree_remove(s->endpoints, GUINT_TO_POINTER(ep));
        g_free(ep_data);
    }
}

void apple_rtkit_unregister_control_ep(AppleRTKit *s, uint32_t ep)
{
    g_assert_cmpuint(ep, <, EP_USER_START);
    apple_rtkit_unregister_ep(s, ep);
}

void apple_rtkit_unregister_user_ep(AppleRTKit *s, uint32_t ep)
{
    g_assert_cmpuint(ep, <, 224);
    apple_rtkit_unregister_ep(s, ep + EP_USER_START);
}

static gboolean apple_rtkit_rollcall_v10_foreach(gpointer key, gpointer value,
                                                 gpointer data)
{
    ((AppleRTKitManagementMessage *)data)->rollcall_v10.mask |=
        BIT(GPOINTER_TO_UINT(key));

    return false;
}

static void apple_rtkit_rollcall_v10(AppleRTKit *s)
{
    AppleA7IOP *a7iop;
    AppleA7IOPMessage *msg;
    AppleRTKitManagementMessage mgmt_msg;

    a7iop = APPLE_A7IOP(s);

    s->ep0_status = EP0_WAIT_ROLLCALL;
    mgmt_msg.type = MSG_TYPE_ROLLCALL;
    mgmt_msg.rollcall_v10.mask = 0;
    g_tree_foreach(s->endpoints, apple_rtkit_rollcall_v10_foreach, &mgmt_msg);
    msg = apple_rtkit_construct_msg(EP_MANAGEMENT, mgmt_msg.raw);
    apple_a7iop_send_ap(a7iop, msg);
}

typedef struct {
    AppleRTKit *s;
    uint32_t mask;
    uint32_t last_block;
} AppleRTKitRollcallV11Data;

static gboolean apple_rtkit_rollcall_v11_foreach(gpointer key, gpointer value,
                                                 gpointer data)
{
    AppleRTKitRollcallV11Data *d = (AppleRTKitRollcallV11Data *)data;
    AppleRTKit *s = d->s;
    AppleRTKitManagementMessage mgmt_msg = { 0 };
    AppleA7IOPMessage *msg;

    uint32_t ep = GPOINTER_TO_UINT(key);

    if (ep / EP_USER_START != d->last_block && d->mask != 0) {
        mgmt_msg.type = MSG_TYPE_ROLLCALL;
        mgmt_msg.rollcall_v11.mask = d->mask;
        mgmt_msg.rollcall_v11.block = d->last_block;
        mgmt_msg.rollcall_v11.end = false;
        msg = apple_rtkit_construct_msg(EP_MANAGEMENT, mgmt_msg.raw);
        QTAILQ_INSERT_TAIL(&s->rollcall, msg, next);
        d->mask = 0;
    }

    d->last_block = ep / EP_USER_START;
    d->mask |= BIT(ep % EP_USER_START);

    return false;
}

static void apple_rtkit_rollcall_v11(AppleRTKit *s)
{
    AppleA7IOP *a7iop;
    AppleA7IOPMessage *msg;
    AppleRTKitRollcallV11Data data = { 0 };
    AppleRTKitManagementMessage mgmt_msg = { 0 };

    a7iop = APPLE_A7IOP(s);

    while (!QTAILQ_EMPTY(&s->rollcall)) {
        msg = QTAILQ_FIRST(&s->rollcall);
        QTAILQ_REMOVE(&s->rollcall, msg, next);
        g_free(msg);
    }

    s->ep0_status = EP0_WAIT_ROLLCALL;
    data.s = s;
    g_tree_foreach(s->endpoints, apple_rtkit_rollcall_v11_foreach, &data);
    mgmt_msg.type = MSG_TYPE_ROLLCALL;
    mgmt_msg.rollcall_v11.mask = data.mask;
    mgmt_msg.rollcall_v11.block = data.last_block;
    mgmt_msg.rollcall_v11.end = true;
    msg = apple_rtkit_construct_msg(EP_MANAGEMENT, mgmt_msg.raw);
    QTAILQ_INSERT_TAIL(&s->rollcall, msg, next);

    msg = QTAILQ_FIRST(&s->rollcall);
    QTAILQ_REMOVE(&s->rollcall, msg, next);
    apple_a7iop_send_ap(a7iop, msg);
}

static void apple_rtkit_handle_mgmt_msg(void *opaque, uint32_t ep,
                                        uint64_t message)
{
    AppleRTKit *s = opaque;
    AppleA7IOP *a7iop = opaque;
    AppleRTKitManagementMessage *msg;
    AppleRTKitManagementMessage m = { 0 };

    msg = (AppleRTKitManagementMessage *)&message;

    trace_apple_rtkit_handle_mgmt_msg(a7iop->role, msg->raw, s->ep0_status,
                                      msg->type);

    switch (msg->type) {
    case MSG_HELLO_ACK: {
        g_assert_cmphex(s->ep0_status, ==, EP0_WAIT_HELLO);

        if (s->protocol_version <= 10) {
            apple_rtkit_rollcall_v10(s);
        } else {
            apple_rtkit_rollcall_v11(s);
        }
        break;
    }
    case MSG_TYPE_PING: {
        m.type = MSG_TYPE_PING_ACK;
        m.ping.seg = msg->ping.seg;
        m.ping.timestamp = msg->ping.timestamp;
        apple_rtkit_send_msg(s, ep, m.raw);
        return;
    }
    case MSG_TYPE_AP_POWER: {
        m.type = MSG_TYPE_AP_POWER;
        m.power.state = msg->power.state;
        apple_rtkit_send_msg(s, ep, m.raw);
        return;
    }
    case MSG_TYPE_REQ_POWER: {
        g_assert_cmphex(s->ep0_status, ==, EP0_IDLE);

        switch (MSG_GET_PSTATE(msg->raw)) {
        case PSTATE_WAIT_VR:
        case PSTATE_ON: {
            apple_a7iop_cpu_start(a7iop, true);
            break;
        }
        case PSTATE_SLPNOMEM: {
            m.type = MSG_TYPE_POWER_ACK;
            m.power.state = MSG_GET_PSTATE(msg->raw);
            apple_a7iop_set_cpu_status(a7iop, CPU_STATUS_IDLE);
            apple_rtkit_send_msg(s, ep, m.raw);
            break;
        }
        default: {
            break;
        }
        }
        break;
    }
    case MSG_TYPE_ROLLCALL: {
        g_assert_cmphex(s->ep0_status, ==, EP0_WAIT_ROLLCALL);

        if (QTAILQ_EMPTY(&s->rollcall)) {
            m.type = MSG_TYPE_POWER_ACK;
            m.power.state = 32;
            s->ep0_status = EP0_IDLE;
            trace_apple_rtkit_rollcall_finished(a7iop->role);
            apple_rtkit_send_msg(s, ep, m.raw);

            if (s->ops != NULL && s->ops->boot_done != NULL) {
                s->ops->boot_done(s->opaque);
            }
        } else {
            AppleA7IOPMessage *m2 = QTAILQ_FIRST(&s->rollcall);
            QTAILQ_REMOVE(&s->rollcall, m2, next);
            apple_a7iop_send_ap(a7iop, m2);
        }
        break;
    }
    case MSG_TYPE_SET_EP_STATUS: {
        break;
    }
    default: {
        break;
    }
    }
}

static void apple_rtkit_mgmt_send_hello(AppleRTKit *s)
{
    AppleRTKitManagementMessage msg = { 0 };

    trace_apple_rtkit_mgmt_send_hello(APPLE_A7IOP(s)->role);

    msg.type = MSG_HELLO;
    msg.hello.major = s->protocol_version;
    msg.hello.minor = s->protocol_version;
    s->ep0_status = EP0_WAIT_HELLO;

    apple_rtkit_send_control_msg(s, EP_MANAGEMENT, msg.raw);
}

static void apple_rtkit_iop_start(AppleA7IOP *s)
{
    AppleRTKit *rtk;

    rtk = APPLE_RTKIT(s);

    trace_apple_rtkit_iop_start(s->role);

    apple_a7iop_set_cpu_status(s, apple_a7iop_get_cpu_status(s) &
                                      ~CPU_STATUS_IDLE);

    if (rtk->ops && rtk->ops->start) {
        rtk->ops->start(rtk->opaque);
    }

    if (rtk->protocol_version >= 11) {
        apple_rtkit_mgmt_send_hello(rtk);
    }
}

static void apple_rtkit_iop_wakeup(AppleA7IOP *s)
{
    AppleRTKit *rtk;

    rtk = APPLE_RTKIT(s);

    trace_apple_rtkit_iop_wakeup(s->role);

    apple_a7iop_set_cpu_status(s, apple_a7iop_get_cpu_status(s) &
                                      ~CPU_STATUS_IDLE);

    if (rtk->ops && rtk->ops->wakeup) {
        rtk->ops->wakeup(rtk->opaque);
    }

    if (strncmp(s->role, "SMC", 4) == 0) {
        apple_rtkit_mgmt_send_hello(rtk);
    }
}

static void apple_rtkit_bh(void *opaque)
{
    AppleRTKit *s = opaque;
    AppleA7IOP *a7iop = opaque;
    AppleRTKitEPData *data;
    AppleA7IOPMessage *msg;
    AppleRTKitMessage *rtk_msg;

    QEMU_LOCK_GUARD(&s->lock);
    while (!apple_a7iop_mailbox_is_empty(a7iop->iop_mailbox)) {
        msg = apple_a7iop_recv_iop(a7iop);
        rtk_msg = (AppleRTKitMessage *)msg->data;
        data = g_tree_lookup(s->endpoints, GUINT_TO_POINTER(rtk_msg->endpoint));
        if (data && data->handler) {
            data->handler(data->opaque,
                          data->user ? rtk_msg->endpoint - EP_USER_START :
                                       rtk_msg->endpoint,
                          rtk_msg->msg);
        }
        g_free(msg);
    }
}

static const AppleA7IOPOps apple_rtkit_iop_ops = {
    .start = apple_rtkit_iop_start,
    .wakeup = apple_rtkit_iop_wakeup,
};

static gint g_uint_cmp(gconstpointer a, gconstpointer b)
{
    return a - b;
}

void apple_rtkit_init(AppleRTKit *s, void *opaque, const char *role,
                      uint64_t mmio_size, AppleA7IOPVersion version,
                      uint32_t protocol_version, const AppleRTKitOps *ops)
{
    AppleA7IOP *a7iop;

    a7iop = APPLE_A7IOP(s);
    apple_a7iop_init(a7iop, role, mmio_size, version, &apple_rtkit_iop_ops,
                     qemu_bh_new_guarded(apple_rtkit_bh, s,
                                         &DEVICE(s)->mem_reentrancy_guard));

    s->opaque = opaque ? opaque : s;
    s->endpoints = g_tree_new(g_uint_cmp);
    s->protocol_version = protocol_version;
    s->ops = ops;
    QTAILQ_INIT(&s->rollcall);
    qemu_mutex_init(&s->lock);
    apple_rtkit_register_control_ep(s, EP_MANAGEMENT, s,
                                    apple_rtkit_handle_mgmt_msg);
    apple_rtkit_register_control_ep(s, EP_CRASHLOG, s, NULL);
}

AppleRTKit *apple_rtkit_new(void *opaque, const char *role, uint64_t mmio_size,
                            AppleA7IOPVersion version,
                            uint32_t protocol_version, const AppleRTKitOps *ops)
{
    AppleRTKit *s;

    s = APPLE_RTKIT(qdev_new(TYPE_APPLE_RTKIT));
    apple_rtkit_init(s, opaque, role, mmio_size, version, protocol_version,
                     ops);

    return s;
}

static void apple_rtkit_reset_hold(Object *obj, ResetType type)
{
    AppleRTKit *s;
    AppleRTKitClass *rtkc;
    AppleA7IOPMessage *msg;

    s = APPLE_RTKIT(obj);
    rtkc = APPLE_RTKIT_GET_CLASS(obj);

    if (rtkc->parent_phases.hold != NULL) {
        rtkc->parent_phases.hold(obj, type);
    }

    QEMU_LOCK_GUARD(&s->lock);

    s->ep0_status = EP0_IDLE;

    while (!QTAILQ_EMPTY(&s->rollcall)) {
        msg = QTAILQ_FIRST(&s->rollcall);
        QTAILQ_REMOVE(&s->rollcall, msg, next);
        g_free(msg);
    }
}

static void apple_rtkit_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc;
    DeviceClass *dc;
    AppleRTKitClass *rtkc;

    rc = RESETTABLE_CLASS(klass);
    dc = DEVICE_CLASS(klass);
    rtkc = APPLE_RTKIT_CLASS(klass);

    dc->desc = "Apple RTKit IOP";
    resettable_class_set_parent_phases(rc, NULL, apple_rtkit_reset_hold, NULL,
                                       &rtkc->parent_phases);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo apple_rtkit_info = {
    .name = TYPE_APPLE_RTKIT,
    .parent = TYPE_APPLE_A7IOP,
    .instance_size = sizeof(AppleRTKit),
    .class_size = sizeof(AppleRTKitClass),
    .class_init = apple_rtkit_class_init,
};

static void apple_rtkit_register_types(void)
{
    type_register_static(&apple_rtkit_info);
}

type_init(apple_rtkit_register_types);
