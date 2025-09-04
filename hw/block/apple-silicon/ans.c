/*
 * Apple ANS Controller.
 *
 * Copyright (c) 2023-2025 Visual Ehrmanntraut (VisualEhrmanntraut).
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
#include "hw/block/apple-silicon/ans.h"
#include "hw/irq.h"
#include "hw/pci/msi.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

// #define DEBUG_ANS
#ifdef DEBUG_ANS
#define DPRINTF(fmt, ...)                             \
    do {                                              \
        qemu_log_mask(LOG_UNIMP, fmt, ##__VA_ARGS__); \
    } while (0)
#else
#define DPRINTF(fmt, ...) \
    do {                  \
    } while (0)
#endif

#define TYPE_APPLE_ANS "apple.ans"
OBJECT_DECLARE_SIMPLE_TYPE(AppleANSState, APPLE_ANS)

#define ANS_LOG_MSG(ep, msg)                                                 \
    do {                                                                     \
        qemu_log_mask(LOG_GUEST_ERROR,                                       \
                      "ANS2: message: ep=%u msg=0x" HWADDR_FMT_plx "\n", ep, \
                      msg);                                                  \
    } while (0)

#define NVME_APPLE_MAX_PEND_CMDS 0x1210
#define NVME_APPLE_MAX_PEND_CMDS_VAL ((64 << 16) | 64)
#define NVME_APPLE_BOOT_STATUS 0x1300
#define NVME_APPLE_BOOT_STATUS_OK 0xde71ce55
#define NVME_APPLE_BASE_CMD_ID 0x1308
#define NVME_APPLE_BASE_CMD_ID_MASK 0xffff
#define NVME_APPLE_LINEAR_SQ_CTRL 0x24908
#define NVME_APPLE_LINEAR_SQ_CTRL_EN (1 << 0)
#define NVME_APPLE_MODESEL 0x1304
#define NVME_APPLE_VENDOR_REG_SIZE (0x60000)

struct AppleANSState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomems[4];
    MemoryRegion io_mmio;
    MemoryRegion io_ioport;
    MemoryRegion msix;
    AppleRTKit *rtk;
    qemu_irq irq;

    NvmeCtrl *nvme;
    uint32_t nvme_interrupt_idx;
    uint32_t vendor_reg[NVME_APPLE_VENDOR_REG_SIZE / sizeof(uint32_t)];
    bool started;
    PCIBus *pci_bus;
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

static void iop_autoboot_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                   unsigned size)
{
    DPRINTF("ANS2: AppleA7IOP autoboot reg WRITE @ 0x" HWADDR_FMT_plx
            " value: 0x" HWADDR_FMT_plx "\n",
            addr, data);
}

static uint64_t iop_autoboot_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    DPRINTF("ANS2: AppleA7IOP autoboot reg READ @ 0x" HWADDR_FMT_plx "\n",
            addr);
    return 0;
}

static const MemoryRegionOps iop_autoboot_reg_ops = {
    .write = iop_autoboot_reg_write,
    .read = iop_autoboot_reg_read,
};

static void apple_ans_vendor_reg_write(void *opaque, hwaddr addr, uint64_t data,
                                       unsigned size)
{
    AppleANSState *s = opaque;
    uint32_t *mmio = &s->vendor_reg[addr >> 2];
    DPRINTF("ANS2: vendor reg WRITE @ 0x" HWADDR_FMT_plx
            " value: 0x" HWADDR_FMT_plx "\n",
            addr, data);
    *mmio = data;
}

static uint64_t apple_ans_vendor_reg_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    AppleANSState *s = opaque;
    uint32_t *mmio = &s->vendor_reg[addr >> 2];
    uint32_t val = *mmio;

    DPRINTF("ANS2: vendor reg READ @ 0x" HWADDR_FMT_plx "\n", addr);
    switch (addr) {
    case NVME_APPLE_MAX_PEND_CMDS:
        val = NVME_APPLE_MAX_PEND_CMDS_VAL;
        break;
    case NVME_APPLE_BOOT_STATUS:
        val = NVME_APPLE_BOOT_STATUS_OK;
        break;
    case NVME_APPLE_BASE_CMD_ID:
        val = 0x6000;
        break;
    default:
        break;
    }
    return val;
}

static const MemoryRegionOps apple_ans_vendor_reg_ops = {
    .write = apple_ans_vendor_reg_write,
    .read = apple_ans_vendor_reg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .valid.unaligned = false,
};

static void apple_ans_set_irq(void *opaque, int irq_num, int level)
{
    // msi_enabled stays disabled and pci_set_irq doesn't work for ans, maybe
    // because the pci device isn't exposed.
    AppleANSState *s = opaque;
    qemu_set_irq(s->irq, level);
#if 0
    //return;
    PCIDevice *pci_dev = PCI_DEVICE(s->nvme);
#if 1
    if (msi_enabled(pci_dev)) {
        if (level) {
            msi_notify(pci_dev, 0);
        }
    } else
#endif
    {
        // pci_set_irq might work for s3e, but not for ans, maybe because the pci device isn't exposed.
        pci_set_irq(pci_dev, level);
        //qemu_set_irq(s->irq, level);
    }
#endif
}

static void apple_ans_start(void *opaque)
{
    AppleANSState *s = opaque;
    PCIDevice *pci_dev = PCI_DEVICE(s->nvme);
    uint32_t config;

    config = pci_default_read_config(pci_dev, PCI_COMMAND, 4);
    config |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_default_write_config(pci_dev, PCI_COMMAND, config, 4);
    g_assert_true(pci_dev->bus_master_enable_region.enabled);
    s->started = true;
}

static void apple_ans_ep_handler(void *opaque, uint32_t ep, uint64_t msg)
{
    ANS_LOG_MSG(ep, msg);
}

static const AppleRTKitOps ans_rtkit_ops = {
    .start = apple_ans_start,
    .wakeup = apple_ans_start,
};

SysBusDevice *apple_ans_create(DTBNode *node, AppleA7IOPVersion version,
                               uint32_t protocol_version, PCIBus *pci_bus)
{
    DeviceState *dev;
    AppleANSState *s;
    SysBusDevice *sbd;
    DTBNode *child;
    DTBProp *prop;
    uint64_t *reg;
    MemoryRegion *alias;
    PCIDevice *pci_dev;

    dev = qdev_new(TYPE_APPLE_ANS);
    s = APPLE_ANS(dev);
    sbd = SYS_BUS_DEVICE(dev);

    prop = dtb_find_prop(node, "reg");
    g_assert_nonnull(prop);

    reg = (uint64_t *)prop->data;

    s->rtk = apple_rtkit_new(s, "ANS2", reg[1], version, protocol_version,
                             &ans_rtkit_ops);
    object_property_add_child(OBJECT(s), "rtkit", OBJECT(s->rtk));
    apple_rtkit_register_user_ep(s->rtk, 0, s, apple_ans_ep_handler);
    sysbus_init_mmio(sbd, sysbus_mmio_get_region(SYS_BUS_DEVICE(s->rtk), 0));

    memory_region_init_io(&s->iomems[1], OBJECT(s), &ascv2_core_reg_ops, s,
                          TYPE_APPLE_ANS ".ascv2-core-reg", reg[3]);
    sysbus_init_mmio(sbd, &s->iomems[1]);

    memory_region_init_io(&s->iomems[2], OBJECT(s), &iop_autoboot_reg_ops, s,
                          TYPE_APPLE_ANS ".iop-autoboot-reg", reg[5]);
    sysbus_init_mmio(sbd, &s->iomems[2]);

    sysbus_pass_irq(sbd, SYS_BUS_DEVICE(s->rtk));
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in_named(dev, apple_ans_set_irq, "interrupt_pci", 1);

    child = dtb_get_node(node, "iop-ans-nub");
    g_assert_nonnull(child);

    dtb_set_prop_u32(child, "pre-loaded", 1);
    dtb_set_prop_u32(child, "running", 1);

    s->pci_bus = pci_bus;
    pci_dev = pci_new(-1, TYPE_NVME);
    s->nvme = NVME(pci_dev);

    object_property_set_str(OBJECT(s->nvme), "serial", "ChefKiss-ANS",
                            &error_fatal);
    object_property_set_bool(OBJECT(s->nvme), "is-apple-ans", true,
                             &error_fatal);
    object_property_set_uint(OBJECT(s->nvme), "max_ioqpairs", 7, &error_fatal);
    object_property_set_uint(OBJECT(s->nvme), "mdts", 8, &error_fatal);
    object_property_set_uint(OBJECT(s->nvme), "logical_block_size", 4096,
                             &error_fatal);
    object_property_set_uint(OBJECT(s->nvme), "physical_block_size", 4096,
                             &error_fatal);
    object_property_set_bool(OBJECT(s->nvme), "msix-exclusive-bar", true,
                             &error_fatal);
    object_property_add_child(OBJECT(s), "nvme", OBJECT(s->nvme));

    memory_region_init_io(&s->iomems[3], OBJECT(s), &apple_ans_vendor_reg_ops,
                          s, TYPE_APPLE_ANS ".mmio", reg[7]);
    alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(alias, OBJECT(s), TYPE_APPLE_ANS ".nvme",
                             &s->nvme->iomem, 0, 0x1200);
    memory_region_add_subregion_overlap(&s->iomems[3], 0, alias, 1);
    sysbus_init_mmio(sbd, &s->iomems[3]);

    return sbd;
}

static void apple_ans_realize(DeviceState *dev, Error **errp)
{
    AppleANSState *s = APPLE_ANS(dev);
    PCIDevice *pci_dev = PCI_DEVICE(s->nvme);
    qdev_realize(DEVICE(s->nvme), BUS(s->pci_bus), &error_fatal);
    g_assert_true(pci_is_express(pci_dev));
    pcie_endpoint_cap_init(pci_dev, 0);
    pcie_cap_deverr_init(pci_dev);

    msi_init(pci_dev, 0, 1, true, false, &error_fatal);
    // msi_init(pci_dev, 0, 8, true, false, &error_fatal);
    pci_pm_init(pci_dev, 0, &error_fatal);
    // pcie_cap_fill_link_ep_usp(pci_dev, QEMU_PCI_EXP_LNK_X2,
    //                           QEMU_PCI_EXP_LNK_8GT);
    pcie_aer_init(pci_dev, PCI_ERR_VER, 0x100, PCI_ERR_SIZEOF, &error_fatal);
    pci_config_set_class(pci_dev->config, PCI_CLASS_STORAGE_OTHER);
#if 0
    pci_config_set_interrupt_pin(pci_dev->config, 1);
    pci_set_byte(pci_dev->config + PCI_INTERRUPT_LINE, 0xff);
    //pci_dev->config[PCI_INTERRUPT_LINE] = 0xff;
    pci_dev->wmask[PCI_INTERRUPT_LINE] = 0x0;
    //pci_default_write_config(pci_dev, PCI_INTERRUPT_LINE, 0xff, 1);
#endif

    sysbus_realize(SYS_BUS_DEVICE(s->rtk), errp);
}

static void apple_ans_unrealize(DeviceState *dev)
{
    AppleANSState *s = APPLE_ANS(dev);

    qdev_unrealize(DEVICE(s->rtk));
}

static void apple_ans_reset(DeviceState *qdev)
{
    AppleANSState *s = APPLE_ANS(qdev);
    PCIDevice *d = PCI_DEVICE(s->nvme);

    pcie_cap_deverr_reset(d);
}

static int apple_ans_post_load(void *opaque, int version_id)
{
    AppleANSState *s = opaque;
    if (s->started) {
        apple_ans_start(s);
    }
    return 0;
}

static const VMStateDescription vmstate_apple_ans = {
    .name = "apple_ans",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = apple_ans_post_load,
    .fields =
        (const VMStateField[]){
            VMSTATE_UINT32(nvme_interrupt_idx, AppleANSState),
            VMSTATE_BOOL(started, AppleANSState),
            VMSTATE_END_OF_LIST(),
        }
};

static void apple_ans_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = apple_ans_realize;
    dc->unrealize = apple_ans_unrealize;
    device_class_set_legacy_reset(dc, apple_ans_reset);
    dc->desc = "Apple NAND Storage (ANS)";
    dc->vmsd = &vmstate_apple_ans;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo apple_ans_info = {
    .name = TYPE_APPLE_ANS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AppleANSState),
    .class_init = apple_ans_class_init,
};

static void apple_ans_register_types(void)
{
    type_register_static(&apple_ans_info);
}

type_init(apple_ans_register_types);
