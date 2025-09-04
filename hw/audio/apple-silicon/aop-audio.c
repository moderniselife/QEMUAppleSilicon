/*
 * Apple Always-On Processor: Audio.
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

#include "hw/audio/apple-silicon/aop-audio.h"
#include "hw/misc/apple-silicon/aop.h"

// #define DEBUG_AOP_AUDIO

#ifdef DEBUG_AOP_AUDIO
#define DPRINTF(v, ...) fprintf(stderr, v, ##__VA_ARGS__)
#else
#define DPRINTF(v, ...) \
    do {                \
    } while (0)
#endif

// AOP Audio Devices
// - mcaN | Multi-Channel Audio (cluster N)
// - apac | AOP Audio Codec?
// - acmm | Audio Codec Mclk Manager?
// - lpai | Low Power Audio Input

#define OP_COMMAND (0x20)

#define COMMAND_GET_DEVICE_ID (0xC3000001)
#define COMMAND_ATTACH_DEVICE (0xC3000002)
#define COMMAND_DETACH_DEVICE (0xC3000003)
#define COMMAND_GET_DEVICE_PROP (0xC3000004)
#define COMMAND_SET_DEVICE_PROP (0xC3000005)
#define COMMAND_REGISTER_ACCESS (0xC3000006)
#define COMMAND_HANDLE_EVENT (0xC3000008)

#define COMMAND_HDR_LEN (0x24)

#define DEV_PROP_MCA_RX_STATUS (0x79)
#define DEV_PROP_MCA_RX_STATUS_LEN (0x18)
#define DEV_PROP_MCA_TX_STATUS (0x7A)
#define DEV_PROP_MCA_TX_STATUS_LEN (0x30)
#define DEV_PROP_MCA_RX0_SHIM_OVERRUN (0xD7)
#define DEV_PROP_MCA_RX0_SHIM_OVERRUN_LEN (4)

#define DEV_PROP_PCM_NUM_SUPPORTED_ASSETS (0xC8)
#define DEV_PROP_PCM_NUM_SUPPORTED_ASSETS_LEN (4)

#define DEV_PROP_STATE (0xC8)
#define DEV_PROP_STATE_LEN (4)
#define DEV_PROP_SUPPORTS_HISTORICAL_DATA (0x12C)
#define DEV_PROP_SUPPORTS_HISTORICAL_DATA_LEN (0x4)
#define DEV_PROP_CHANNEL_CTRL (0x12D)
#define DEV_PROP_CHANNEL_CTRL_LEN (0x10)
#define DEV_PROP_STREAM_FORMAT (0x12E)
#define DEV_PROP_STREAM_FORMAT_LEN (0x10)

#define PROPERTY_IDENTITY (0x64)
#define PROPERTY_DEVICE_COUNT (0x65)
#define PROPERTY_IO_HANDLER_COUNT (0x66)
#define PROPERTY_VERSION (0x67)

struct AppleAOPAudioState {
    SysBusDevice parent_obj;

    AppleAOPEndpoint *ep;
    uint32_t supported_chans;
    uint32_t enabled_chans;
    uint32_t voice_trigger_chans;
    uint32_t history_chans;
};

static void apple_aop_audio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc;

    dc = DEVICE_CLASS(klass);

    dc->desc = "Apple Always-On Processor Audio";
    dc->user_creatable = false;
    // dc->vmsd = &vmstate_apple_aop_audio;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
}

static const TypeInfo apple_aop_audio_info = {
    .name = TYPE_APPLE_AOP_AUDIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AppleAOPAudioState),
    .class_init = apple_aop_audio_class_init,
};

static void apple_aop_audio_register_types(void)
{
    type_register_static(&apple_aop_audio_info);
}

type_init(apple_aop_audio_register_types);

static AppleAOPResult apple_aop_audio_get_prop(void *opaque, uint32_t prop,
                                               void *out)
{
    DPRINTF("AOPAudio GetProperty 0x%X\n", prop);

    switch (prop) {
    case PROPERTY_IDENTITY:
        stl_le_p(out, 'aop ');
        break;
    case PROPERTY_DEVICE_COUNT:
        stl_le_p(out, 3);
        break;
    case PROPERTY_IO_HANDLER_COUNT:
        stl_le_p(out, 0);
        break;
    default:
        break;
    }

    return AOP_RESULT_OK;
}

static AppleAOPResult
apple_aop_audio_handle_command(void *opaque, uint32_t type, uint8_t category,
                               uint16_t seq, void *payload, uint32_t len,
                               void *payload_out, uint32_t out_len)
{
    AppleAOPAudioState *s = opaque;

    if (type != OP_COMMAND || ldl_le_p(payload) != 0xFFFFFFFF) {
        return AOP_RESULT_ERROR;
    }

    switch (ldl_le_p(payload + sizeof(uint32_t))) {
    case COMMAND_GET_DEVICE_ID:
        DPRINTF("AOPAudio GetDeviceID %d\n",
                ldl_le_p(payload + COMMAND_HDR_LEN));

        switch (ldl_le_p(payload + COMMAND_HDR_LEN)) {
        case 0:
            stl_le_p(payload_out, 'lpai');
            break;
        case 1:
            stl_le_p(payload_out, 'apac');
            break;
        case 2:
            stl_le_p(payload_out, 'edtC');
            break;
        }
        break;
    case COMMAND_GET_DEVICE_PROP:
        DPRINTF("AOPAudio GetDeviceProperty %X 0x%X\n",
                ldl_le_p(payload + COMMAND_HDR_LEN),
                ldl_le_p(payload + COMMAND_HDR_LEN + 4));

        switch (ldl_le_p(payload + COMMAND_HDR_LEN)) {
        case 'lpai':
            switch (ldl_le_p(payload + COMMAND_HDR_LEN + 4)) {
            case DEV_PROP_STATE:
                stl_le_p(payload_out, DEV_PROP_STATE_LEN);
                stl_le_p(payload_out + 4, 'idle');
                break;
            case DEV_PROP_CHANNEL_CTRL:
                stl_le_p(payload_out, DEV_PROP_CHANNEL_CTRL_LEN);
                stl_le_p(payload_out + 4, s->supported_chans);
                stl_le_p(payload_out + 8, s->enabled_chans);
                stl_le_p(payload_out + 12, s->voice_trigger_chans);
                stl_le_p(payload_out + 16, s->history_chans);
                break;
            case DEV_PROP_STREAM_FORMAT:
                stl_le_p(payload_out, DEV_PROP_STREAM_FORMAT_LEN);
                stl_le_p(payload_out + 4, 'pcm ');
                stl_le_p(payload_out + 8, 48000);
                stl_le_p(payload_out + 12, 2);
                stl_le_p(payload_out + 16, 2);
                break;
            case DEV_PROP_SUPPORTS_HISTORICAL_DATA:
                stl_le_p(payload_out, DEV_PROP_SUPPORTS_HISTORICAL_DATA_LEN);
                stl_le_p(payload_out + 4, 0);
                break;
            }
            break;
        case 'lai ':
            switch (ldl_le_p(payload + COMMAND_HDR_LEN + 4)) {
            case DEV_PROP_STATE:
                stl_le_p(payload_out, DEV_PROP_STATE_LEN);
                stl_le_p(payload_out + 4, 'idle');
                break;
            case DEV_PROP_SUPPORTS_HISTORICAL_DATA:
                stl_le_p(payload_out, DEV_PROP_SUPPORTS_HISTORICAL_DATA_LEN);
                stl_le_p(payload_out + 4, 0);
                break;
            }
            break;
        case 'mca0':
        case 'mca1':
            switch (ldl_le_p(payload + COMMAND_HDR_LEN + 4)) {
            case DEV_PROP_MCA_RX_STATUS:
                stl_le_p(payload_out, DEV_PROP_MCA_RX_STATUS_LEN);
                break;
            case DEV_PROP_MCA_TX_STATUS:
                stl_le_p(payload_out, DEV_PROP_MCA_TX_STATUS_LEN);
                break;
            case DEV_PROP_MCA_RX0_SHIM_OVERRUN:
                stl_le_p(payload_out, DEV_PROP_MCA_RX0_SHIM_OVERRUN_LEN);
                break;
            }
            break;
        case 'acmm':
        case 'apac':
            switch (ldl_le_p(payload + COMMAND_HDR_LEN + 4)) {
            case DEV_PROP_STATE:
                stl_le_p(payload_out, DEV_PROP_STATE_LEN);
                stl_le_p(payload_out + 4, 'pwrd');
                break;
            }
            break;
        case 'pcmM':
            switch (ldl_le_p(payload + COMMAND_HDR_LEN + 4)) {
            case DEV_PROP_PCM_NUM_SUPPORTED_ASSETS:
                stl_le_p(payload_out, DEV_PROP_PCM_NUM_SUPPORTED_ASSETS_LEN);
                stl_le_p(payload_out + 4, 2);
                break;
            }
            break;
        }
        break;
    case COMMAND_SET_DEVICE_PROP:
        DPRINTF("AOPAudio SetDeviceProperty %X 0x%X\n",
                ldl_le_p(payload + COMMAND_HDR_LEN),
                ldl_le_p(payload + COMMAND_HDR_LEN + 4));

        switch (ldl_le_p(payload + COMMAND_HDR_LEN)) {
        case 'lpai':
            switch (ldl_le_p(payload + COMMAND_HDR_LEN + 4)) {
            case DEV_PROP_CHANNEL_CTRL:
                s->supported_chans = ldl_le_p(payload + COMMAND_HDR_LEN + 12);
                s->enabled_chans = ldl_le_p(payload + COMMAND_HDR_LEN + 16);
                s->voice_trigger_chans =
                    ldl_le_p(payload + COMMAND_HDR_LEN + 20);
                s->history_chans = ldl_le_p(payload + COMMAND_HDR_LEN + 24);
                break;
            }
            break;
        }
        break;
    }

    return AOP_RESULT_OK;
}

static const AppleAOPEndpointDescription apple_aop_audio_ep_descr = {
    .type = AOP_EP_TYPE_APP,
    .service_name = "aop-audio",
    .service_id = 0x1000000D,
    .rx_len = 0x4000,
    .tx_len = 0xD0000,
    .get_property = apple_aop_audio_get_prop,
    .handle_command = apple_aop_audio_handle_command,
};

SysBusDevice *apple_aop_audio_create(AppleAOPState *aop)
{
    DeviceState *dev;
    SysBusDevice *sbd;
    AppleAOPAudioState *s;

    dev = qdev_new(TYPE_APPLE_AOP_AUDIO);
    sbd = SYS_BUS_DEVICE(dev);
    s = APPLE_AOP_AUDIO(dev);

    s->ep = apple_aop_ep_create(aop, s, &apple_aop_audio_ep_descr);

    return sbd;
}
