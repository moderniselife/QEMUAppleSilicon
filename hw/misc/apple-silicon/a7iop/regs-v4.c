/*
 * Apple A7IOP V4 Registers.
 *
 * Copyright (c) 2023-2025 Visual Ehrmanntraut (VisualEhrmanntraut).
 * Copyright (c) 2023-2025 Christian Inci (chris-pcguy).
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
#include "exec/hwaddr.h"
#include "hw/misc/apple-silicon/a7iop/core.h"
#include "hw/sysbus.h"
#include "qemu/lockable.h"
#include "qemu/log.h"
#include "private.h"
#include "system/memory.h"

#define REG_AXI_BASE_LO (0x8)
#define REG_AXI_BASE_HI (0x10)
#define REG_AXI_START_LO (0x18)
#define REG_AXI_START_HI (0x20)
#define REG_AXI_END_LO (0x28)
#define REG_AXI_END_HI (0x30)
#define REG_AXI_CTRL (0x38)
#define AXI_CTRL_RUN BIT(0)
#define REG_CPU_CTRL (0x44)
#define REG_CPU_STATUS (0x48)
#define REG_UNKNOWN_4C (0x4C)
#define REG_KIC_GLB_CFG (0x80C)
#define KIC_GLB_CFG_TIMER_EN (1 << 1)
#define REG_INTERRUPT_STATUS (0x81C) // "akf: READ IRQ %x"
#define REG_SEP_AKF_DISABLE_INTERRUPT_BASE (0xA00)
#define REG_SEP_AKF_ENABLE_INTERRUPT_BASE (0xA80)
#define REG_KIC_MAILBOX_EXT_SET (0xC00)
#define REG_KIC_MAILBOX_EXT_CLR (0xC04)
#define REG_IDLE_STATUS (0x8000)
#define REG_KIC_TMR_CFG1 (0x10000)
#define KIC_TMR_CFG_FSL_TIMER (0 << 4)
#define KIC_TMR_CFG_FSL_SW (1 << 4)
#define KIC_TMR_CFG_FSL_EXTERNAL (2 << 4)
#define KIC_TMR_CFG_SMD_FIQ (0 << 3)
#define KIC_TMR_CFG_SMD_IRQ (1 << 3)
#define KIC_TMR_CFG_EMD_IRQ (1 << 2)
#define KIC_TMR_CFG_IMD_FIQ (0 << 1)
#define KIC_TMR_CFG_IMD_IRQ (1 << 1)
#define KIC_TMR_CFG_EN (1 << 0)
#define KIC_TMR_CFG_NMI                                               \
    (KIC_TMR_CFG_FSL_SW | KIC_TMR_CFG_SMD_FIQ | KIC_TMR_CFG_IMD_FIQ | \
     KIC_TMR_CFG_EN)
#define REG_KIC_TMR_CFG2 (0x10004)
#define REG_KIC_TMR_STATE_SET1 (0x10020)
#define KIC_TMR_STATE_SET_SGT (1 << 0)
#define REG_KIC_TMR_STATE_SET2 (0x10024)
#define REG_KIC_GLB_TIME_BASE_LO (0x10030)
#define REG_KIC_GLB_TIME_BASE_HI (0x10038)

#define AKF_MAILBOX_OFF (0x100)

static void apple_a7iop_v4_reg_write(void *opaque, hwaddr addr,
                                     const uint64_t data, unsigned size)
{
    AppleA7IOP *s = opaque;

    switch (addr) {
    case REG_CPU_CTRL:
        apple_a7iop_set_cpu_ctrl(s, (uint32_t)data);
        break;
    case REG_SEP_AKF_DISABLE_INTERRUPT_BASE + 0x00: // group 0
    case REG_SEP_AKF_DISABLE_INTERRUPT_BASE + 0x04: // group 1
    case REG_SEP_AKF_DISABLE_INTERRUPT_BASE + 0x08: // group 2
    case REG_SEP_AKF_DISABLE_INTERRUPT_BASE + 0x0C: // group 3
        WITH_QEMU_LOCK_GUARD(&s->iop_mailbox->lock)
        {
            s->iop_mailbox->interrupts_enabled
                [(addr - REG_SEP_AKF_DISABLE_INTERRUPT_BASE) >> 2] &= ~data;
            apple_a7iop_mailbox_update_irq(s->iop_mailbox);
        }

        break;
    case REG_SEP_AKF_ENABLE_INTERRUPT_BASE + 0x00: // group 0
    case REG_SEP_AKF_ENABLE_INTERRUPT_BASE + 0x04: // group 1
    case REG_SEP_AKF_ENABLE_INTERRUPT_BASE + 0x08: // group 2
    case REG_SEP_AKF_ENABLE_INTERRUPT_BASE + 0x0C: // group 3
        WITH_QEMU_LOCK_GUARD(&s->iop_mailbox->lock)
        {
            s->iop_mailbox->interrupts_enabled
                [(addr - REG_SEP_AKF_ENABLE_INTERRUPT_BASE) >> 2] |= data;
            apple_a7iop_mailbox_update_irq(s->iop_mailbox);
        }
        break;
    case REG_KIC_MAILBOX_EXT_CLR:
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "A7IOP(%s): Unknown write to 0x" HWADDR_FMT_plx
                      " of value 0x" HWADDR_FMT_plx "\n",
                      s->role, addr, data);
        break;
    }
}

static uint64_t apple_a7iop_v4_reg_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    AppleA7IOP *s = opaque;
    uint64_t ret = 0;

    switch (addr) {
    case REG_CPU_CTRL:
        return apple_a7iop_get_cpu_ctrl(s);
    case REG_CPU_STATUS:
        return apple_a7iop_get_cpu_status(s);
    case REG_UNKNOWN_4C:
        ret = 1;
        // TODO: response not interrupt available, but something with
        // REG_V3_CPU_CTRL?
        break;
    case REG_INTERRUPT_STATUS: {
        AppleA7IOPMailbox *a7iop_mbox = s->iop_mailbox;
        uint32_t interrupt_status =
            apple_a7iop_interrupt_status_pop(a7iop_mbox);
        WITH_QEMU_LOCK_GUARD(&s->lock)
        {
            apple_a7iop_mailbox_update_irq_status(a7iop_mbox);
            if (interrupt_status) {
                ret = interrupt_status;
            } else if (a7iop_mbox->iop_nonempty) {
                ret = 0x40000;
            } else if (a7iop_mbox->iop_empty) {
                ret = 0x40001;
            } else if (a7iop_mbox->ap_nonempty) {
                ret = 0x40002;
            } else if (a7iop_mbox->ap_empty) {
                ret = 0x40003;
            } else {
                ret = 0x70001;
            }
        }
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP,
                      "A7IOP(%s): Unknown read from 0x" HWADDR_FMT_plx "\n",
                      s->role, addr);
        break;
    }

    return ret;
}

static const MemoryRegionOps apple_a7iop_v4_reg_ops = {
    .write = apple_a7iop_v4_reg_write,
    .read = apple_a7iop_v4_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .impl.min_access_size = 4,
    .impl.max_access_size = 8,
    .valid.unaligned = false,
};

void apple_a7iop_init_mmio_v4(AppleA7IOP *s, uint64_t mmio_size)
{
    SysBusDevice *sbd;
    char name[32];

    sbd = SYS_BUS_DEVICE(s);

    snprintf(name, sizeof(name), TYPE_APPLE_A7IOP ".%s.regs", s->role);
    memory_region_init_io(&s->mmio, OBJECT(s), &apple_a7iop_v4_reg_ops, s, name,
                          mmio_size);
    sysbus_init_mmio(sbd, &s->mmio);

    memory_region_add_subregion_overlap(&s->mmio, AKF_STRIDE + AKF_MAILBOX_OFF,
                                        &s->iop_mailbox->mmio, 1);
    memory_region_add_subregion_overlap(
        &s->mmio, AKF_STRIDE * 2 + AKF_MAILBOX_OFF, &s->ap_mailbox->mmio, 1);
}
