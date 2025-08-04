/*
 * Apple iPhone 11 Buttons
 *
 * Copyright (c) 2025 Christian Inci (chris-pcguy).
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
#include "hw/arm/apple-silicon/dtb.h"
#include "hw/irq.h"
#include "hw/misc/apple-silicon/a7iop/rtkit.h"
#include "hw/misc/apple-silicon/buttons.h"
#include "hw/misc/apple-silicon/smc.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "system/runstate.h"
#include "ui/input.h"

// #define DEBUG_BUTTONS
#ifdef DEBUG_BUTTONS
#define DPRINTF(fmt, ...)                             \
    do {                                              \
        qemu_log_mask(LOG_UNIMP, fmt, ##__VA_ARGS__); \
    } while (0)
#else
#define DPRINTF(fmt, ...) \
    do {                  \
    } while (0)
#endif

#define TYPE_APPLE_BUTTONS "apple.buttons"
OBJECT_DECLARE_SIMPLE_TYPE(AppleButtonsState, APPLE_BUTTONS)

#define BUTTONS_ID_BTN_RST (0)
#define BUTTONS_ID_HOLD (1)
#define BUTTONS_ID_VOLUP (2)
#define BUTTONS_ID_VOLDOWN (3)
#define BUTTONS_ID_RINGERAB (4)
#define BUTTONS_ID_HOME_AND_APPSWITCHER (6)


struct AppleButtonsState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    uint8_t ringer_old_state;
};

static void apple_buttons_send_button(AppleSMCState *s, uint8_t buttonIndex, uint8_t buttonState)
{
    AppleRTKit *rtk;
    KeyResponse r;
    rtk = APPLE_RTKIT(s);

    memset(&r, 0, sizeof(r));
    r.status = SMC_NOTIFICATION;
    r.response[0] = buttonState;
    r.response[1] = buttonIndex;
    r.response[2] = 0x01; // type: 1==lid state/hid-event? 2==vectorState? 3==hid buttons?
    r.response[3] = 0x72;
    apple_rtkit_send_user_msg(rtk, kSMCKeyEndpoint, r.raw);
}

static void apple_buttons_event(DeviceState *dev, QemuConsole *src,
                                InputEvent *evt)
{
    AppleButtonsState *s = APPLE_BUTTONS(dev);
    InputKeyEvent *key = evt->u.key.data;
    int qcode;
    uint8_t buttonIndex, buttonState;
    // smc-pmu
    AppleSMCState *smc = APPLE_SMC_IOP(object_property_get_link(
        OBJECT(qdev_get_machine()), "smc", &error_fatal));

    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER, NULL);
    assert(evt->type == INPUT_EVENT_KIND_KEY);
    qcode = qemu_input_key_value_to_qcode(key->key);

    //

    DPRINTF("%s: qcode: 0x%x/%d ; key->down: %d\n", __func__, qcode, qcode, key->down);
    DPRINTF("%s: Q_KEY_CODE_F1: 0x%x\n", __func__, Q_KEY_CODE_F1);
    DPRINTF("%s: Q_KEY_CODE_F12: 0x%x\n", __func__, Q_KEY_CODE_F12);

    buttonIndex = 0;
    buttonState = key->down;

    switch (qcode) {
#if 0
    case Q_KEY_CODE_F1:
        buttonIndex = BUTTONS_ID_BTN_RST; // "btn_rst" kernel panic
        break;
    case Q_KEY_CODE_F2:
        // can't realiably invoke Siri using Hold, because of key repeat crap,
        // which automatically creates key-up events
        buttonIndex = BUTTONS_ID_HOLD;
        break;
    case Q_KEY_CODE_F3:
        if (!buttonState) // do nothing on key-up
            return;
        buttonIndex = BUTTONS_ID_RINGERAB;
        buttonState = !s->ringer_old_state;
        s->ringer_old_state = buttonState;
        break;
    case Q_KEY_CODE_F4:
        buttonIndex = BUTTONS_ID_VOLUP;
        break;
    case Q_KEY_CODE_F5:
        buttonIndex = BUTTONS_ID_VOLDOWN;
        break;
    case Q_KEY_CODE_F6: // again, key repeat crap by the host os
        buttonIndex = BUTTONS_ID_HOME_AND_APPSWITCHER;
        break;
#endif
#if 1
    case Q_KEY_CODE_F1:
        buttonIndex = BUTTONS_ID_BTN_RST; // "btn_rst" kernel panic
        break;
    case Q_KEY_CODE_F2:
        if (!buttonState) // do nothing on key-up
            return;
        buttonIndex = BUTTONS_ID_RINGERAB;
        buttonState = !s->ringer_old_state;
        s->ringer_old_state = buttonState;
        break;
    case Q_KEY_CODE_F3:
        buttonIndex = BUTTONS_ID_VOLUP;
        break;
    case Q_KEY_CODE_F4:
        buttonIndex = BUTTONS_ID_VOLDOWN;
        break;
    case Q_KEY_CODE_F5:
        if (!buttonState) // do nothing on key-up
            return;
        buttonIndex = BUTTONS_ID_HOLD;
        buttonState = true;
        break;
    case Q_KEY_CODE_F6:
        if (!buttonState) // do nothing on key-up
            return;
        buttonIndex = BUTTONS_ID_HOLD;
        buttonState = false;
        break;
#if 0
    case Q_KEY_CODE_F7:
        if (!buttonState) // do nothing on key-up
            return;
        buttonIndex = BUTTONS_ID_HOME_AND_APPSWITCHER;
        buttonState = true;
        break;
    case Q_KEY_CODE_F8:
        if (!buttonState) // do nothing on key-up
            return;
        buttonIndex = BUTTONS_ID_HOME_AND_APPSWITCHER;
        buttonState = false;
        break;
#endif
#if 1
    case Q_KEY_CODE_F7: // home
        if (!buttonState) // do nothing on key-up
            return;
        apple_buttons_send_button(smc, BUTTONS_ID_HOME_AND_APPSWITCHER, true);
        apple_buttons_send_button(smc, BUTTONS_ID_HOME_AND_APPSWITCHER, false);
        return;
    case Q_KEY_CODE_F8: // app-switcher
        if (!buttonState) // do nothing on key-up
            return;
        apple_buttons_send_button(smc, BUTTONS_ID_HOME_AND_APPSWITCHER, true);
        apple_buttons_send_button(smc, BUTTONS_ID_HOME_AND_APPSWITCHER, false);
        apple_buttons_send_button(smc, BUTTONS_ID_HOME_AND_APPSWITCHER, true);
        apple_buttons_send_button(smc, BUTTONS_ID_HOME_AND_APPSWITCHER, false);
        return;
#endif
#endif
    case Q_KEY_CODE_F9:
        buttonIndex = 5;
        break;
    case Q_KEY_CODE_F10:
        buttonIndex = 7;
        break;
    case Q_KEY_CODE_F11:
        buttonIndex = 8;
        break;
    case Q_KEY_CODE_F12:
        buttonIndex = 9;
        break;
    // case Q_KEY_CODE_F11:
    //     buttonIndex = 10; // null-pointer kernel panic
    //     break;
    // case Q_KEY_CODE_F12:
    //     buttonIndex = 11; // null-pointer kernel panic
    //     break;
    default:
        return;
    }

    apple_buttons_send_button(smc, buttonIndex, buttonState);
}

static uint8_t smc_key_btnR_read(AppleSMCState *s, SMCKey *key,
                                 SMCKeyData *data, void *payload,
                                 uint8_t length)
{
    uint32_t value;
    uint32_t tmpval0;

    if (payload == NULL || length != key->info.size) {
        return kSMCBadArgumentError;
    }

    value = ldl_le_p(payload);

    if (data->data == NULL) {
        data->data = g_malloc(key->info.size);
    } else {
        uint32_t *data0 = data->data;
        DPRINTF("%s: data->data: %p ; data0[0]: 0x%08x\n", __func__, data->data,
                data0[0]);
    }

    DPRINTF("%s: key->info.size: 0x%08x ; length: 0x%08x\n", __func__,
            key->info.size, length);
    DPRINTF("%s: value: 0x%08x ; length: 0x%08x\n", __func__, value, length);

    switch (value) {
    default:
        DPRINTF("%s: UNKNOWN VALUE: 0x%08x\n", __func__, value);
        return kSMCBadFuncParameter;
    }
}

static uint8_t smc_key_btnR_write(AppleSMCState *s, SMCKey *key,
                                  SMCKeyData *data, void *payload,
                                  uint8_t length)
{
    AppleRTKit *rtk;
    uint32_t value;
    KeyResponse r;

    AppleButtonsState *buttons = APPLE_BUTTONS(object_property_get_link(
        OBJECT(qdev_get_machine()), "buttons", &error_fatal));

    if (payload == NULL || length != key->info.size) {
        return kSMCBadArgumentError;
    }

    rtk = APPLE_RTKIT(s);
    value = ldl_le_p(payload);

    // Do not use data->data here, as it only contains the data last written to
    // by the read function (smc_key_gP09_read)

    DPRINTF("%s: value: 0x%08x ; length: 0x%08x\n", __func__, value, length);

    switch (value) {
    default:
        DPRINTF("%s: UNKNOWN VALUE: 0x%08x\n", __func__, value);
        return kSMCBadFuncParameter;
    }
}

SysBusDevice *apple_buttons_create(DTBNode *node)
{
    DeviceState *dev;
    AppleButtonsState *s;
    SysBusDevice *sbd;
    DTBProp *prop;

    dev = qdev_new(TYPE_APPLE_BUTTONS);
    s = APPLE_BUTTONS(dev);
    sbd = SYS_BUS_DEVICE(dev);

    // smc-pmu
    AppleSMCState *smc = APPLE_SMC_IOP(object_property_get_link(
        OBJECT(qdev_get_machine()), "smc", &error_fatal));
    apple_smc_create_key_func(smc, 'btnR', 4, SMCKeyTypeUInt32,
                              SMC_ATTR_FUNCTION | SMC_ATTR_WRITEABLE |
                                  SMC_ATTR_READABLE | 0x20,
                              &smc_key_btnR_read, &smc_key_btnR_write);


    return sbd;
}

static void apple_buttons_qdev_reset_hold(Object *obj, ResetType type)
{
    AppleButtonsState *s = APPLE_BUTTONS(obj);

    s->ringer_old_state = 0;
}

static const QemuInputHandler apple_buttons_handler = {
    .name  = "Apple Buttons",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = apple_buttons_event,
};

static void apple_buttons_realize(DeviceState *dev, Error **errp)
{
    qemu_input_handler_register(dev, &apple_buttons_handler);
}

static void apple_buttons_unrealize(DeviceState *dev)
{
    AppleButtonsState *s = APPLE_BUTTONS(dev);
}

static const VMStateDescription vmstate_apple_buttons = {
    .name = "apple_buttons",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_END_OF_LIST(),
        }
};

static void apple_buttons_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = apple_buttons_qdev_reset_hold;

    dc->realize = apple_buttons_realize;
    dc->unrealize = apple_buttons_unrealize;
    dc->desc = "Apple Buttons";
    dc->vmsd = &vmstate_apple_buttons;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo apple_buttons_types[] = {
    {
        .name = TYPE_APPLE_BUTTONS,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AppleButtonsState),
        .class_init = apple_buttons_class_init,
    },
};

DEFINE_TYPES(apple_buttons_types)
