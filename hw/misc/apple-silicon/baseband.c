/*
 * Apple iPhone 11 Baseband
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
#include "hw/misc/apple-silicon/baseband.h"
#include "hw/misc/apple-silicon/smc.h"
#include "hw/misc/apple-silicon/spmi-baseband.h"
#include "hw/pci/msi.h"
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/units.h"

#define DEBUG_BASEBAND
#ifdef DEBUG_BASEBAND
#define HEXDUMP(a, b, c)               \
    do {                               \
        qemu_hexdump(stderr, a, b, c); \
    } while (0)
#define DPRINTF(fmt, ...)                             \
    do {                                              \
        qemu_log_mask(LOG_UNIMP, fmt, ##__VA_ARGS__); \
    } while (0)
#else
#define HEXDUMP(a, b, c) \
    do {                 \
    } while (0)
#define DPRINTF(fmt, ...) \
    do {                  \
    } while (0)
#endif

#define TYPE_APPLE_BASEBAND_DEVICE "apple.baseband_device"
OBJECT_DECLARE_SIMPLE_TYPE(AppleBasebandDeviceState, APPLE_BASEBAND_DEVICE)

#define TYPE_APPLE_BASEBAND "apple.baseband"
OBJECT_DECLARE_SIMPLE_TYPE(AppleBasebandState, APPLE_BASEBAND)

// s8000: 0x1000/0x1000 (qualcomm)
// t8015: 0x1000/0x400 (intel)
// srd.cx's ioreg file of the iPhone 11 says that it has three 32-bit bars and
// the sizes are as follows
// t8030: 0x1000/0x1000/0x2000 (intel)
// bar2 (zero-based) might be for msi-x

#define APPLE_BASEBAND_DEVICE_BAR0_SIZE (0x1000)
#define APPLE_BASEBAND_DEVICE_BAR1_SIZE (0x1000)
#define APPLE_BASEBAND_DEVICE_BAR2_SIZE (0x2000)

typedef struct custom_hmap_t {
    uint32_t cap_header;
    uint16_t vsec_id;
    char _6[6];
    uint32_t field_c_0x300f6;
    char _10[0x30];
    uint64_t field_40_msi_address_4KiB_aligned_BITWISE_OR_0x3;
    char _48[4];
    uint32_t field_4c_msi_address_BITWISE_AND_0xffc;
    char _50[0x10];
    uint64_t field_60_arg2_dart_window_virt_4KiB_aligned_BITWISE_OR_0x3;
    uint64_t field_68_arg3_4KiB_aligned;
} custom_hmap_t;

typedef struct custom_l1ss_t {
    uint32_t cap_header;
    uint32_t value_cap;
    uint32_t value_ctl1;
    uint32_t value_ctl2;
} custom_l1ss_t;

typedef struct QEMU_PACKED baseband_context0_t {
    uint16_t version;
    uint16_t size;
    uint32_t config;
    /* void* */ uint64_t peripheral_info_address;
    /* void* */ uint64_t cr_hia_address;
    /* void* */ uint64_t tr_tia_address;
    /* void* */ uint64_t cr_tia_address;
    /* void* */ uint64_t tr_hia_address;
    uint16_t cr_ia_entries;
    uint16_t tr_ia_entries;
    uint32_t mcr_address_low;
    uint32_t mcr_address_high;
    uint32_t mtr_address_low;
    uint32_t mtr_address_high;
    uint16_t mtr_entries;
    uint16_t mcr_entries;
    uint16_t mtr_doorbell;
    uint16_t mcr_doorbell;
    uint16_t mtr_msi;
    uint16_t mcr_msi;
    uint8_t mtr_header_size;
    uint8_t mtr_footer_size;
    uint8_t mcr_header_size;
    uint8_t mcr_footer_size;
    uint16_t bit0_out_of_order__bit1_in_place;
    uint16_t peripheral_info_msi;
    /* void* */ uint64_t scratch_pad_address;
    uint32_t scratch_pad_size;
    uint32_t field_64;
} baseband_context0_t;

struct AppleBasebandDeviceState {
    PCIDevice parent_obj;
    AppleBasebandState *root;

    MemoryRegion container;
    MemoryRegion bar0, bar1, bar2;
    MemoryRegion bar0_alias, bar1_alias, bar2_alias;
    // MemoryRegion msix; // no msix for now

    ApplePCIEPort *port;
    MemoryRegion *dma_mr;
    AddressSpace *dma_as;

    uint32_t hmap_hardcoded_offset;
    custom_hmap_t hmap;
    custom_l1ss_t l1ss;

    qemu_irq gpio_reset_det_irq;
    bool gpio_coredump_val;
    bool gpio_reset_det_val;
    uint32_t boot_stage;
    uint64_t context_addr;
    uint64_t image_addr;
    uint32_t image_size;
    void *image_ptr;
    baseband_context0_t baseband_context0;
};

struct AppleBasebandState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    AppleBasebandDeviceState *device;
#if 1
    qemu_irq irq;
#endif

    PCIBus *pci_bus;
};

#if 0
static void apple_baseband_set_irq(void *opaque, int irq_num, int level)
{
    AppleBasebandState *s = opaque;
    DPRINTF("%s: before first qemu_set_irq: host->irqs[irq_num]: %p ; irq_num: "
            "%d/0x%x ; level: %d\n",
            __func__, s->irq, irq_num, irq_num, level);
    qemu_set_irq(s->irq, level);
    DPRINTF("%s: after first qemu_set_irq: host->irqs[irq_num]: %p ; irq_num: "
            "%d/0x%x ; level: %d\n",
            __func__, s->irq, irq_num, irq_num, level);
}
#endif

#if 1
static void apple_baseband_set_irq(void *opaque, int irq_num, int level)
{
    AppleBasebandState *s = opaque;
    ApplePCIEPort *port = s->device->port;
    ApplePCIEHost *host = port->host;
    ApplePCIEState *pcie = host->pcie;
    PCIDevice *port_pci_dev = PCI_DEVICE(port);
    PCIDevice *pci_dev = PCI_DEVICE(s->device);
#if 1
    if (msi_enabled(pci_dev)) {
        if (level) {
            // maybe this wouldn't do anything, because the actual msi dma_as
            // is at the port, not at the device
            msi_notify(pci_dev, 0);
            // // msi_notify(port_pci_dev, 24);
            // msi_notify(port_pci_dev, 0);
        }
    } else
#endif
    {
        pci_set_irq(pci_dev, level);
    }
}
#endif

#if 0
static void apple_baseband_raise_msi(AppleBasebandDeviceState *s,
                                     uint32_t irq_num)
{
    AppleBasebandState *baseband = s->root;
    ApplePCIEPort *port = s->port;
    ApplePCIEHost *host = port->host;
    ApplePCIEState *pcie = host->pcie;
    PCIDevice *port_pci_dev = PCI_DEVICE(port);
    PCIDevice *pci_dev = PCI_DEVICE(baseband->device);

    bool port_is_msi_enabled = msi_enabled(port_pci_dev);
    bool baseband_is_msi_enabled = msi_enabled(pci_dev);
    DPRINTF("%s: is port msi_enabled()?: %d\n", __func__, port_is_msi_enabled);
    DPRINTF("%s: is baseband msi_enabled()?: %d\n", __func__,
            baseband_is_msi_enabled);

#if 1
    // msi_notify(port_pci_dev, 0);
    // msi_notify(pci_dev, 0);
    uint64_t msi_address = 0xfffff000ull;
    if (baseband_is_msi_enabled) {
        // port->port_last_interrupt |= 0x1000;
        // port->port_last_interrupt |= ;
        // msi_notify(port_pci_dev, 0);
        // msi_notify(pci_dev, 0);
#if 1
        uint32_t msi_value = 0;
        msi_value = irq_num;
        // //msi_value = 1 << irq_num;
        // //msi_value = 1 << 24;
        // //msi_value = 1 << 25;
        // //msi_value = UINT32_MAX;
        // should this be written by the device or by the OS?
        address_space_rw(&port->dma_as, msi_address, MEMTXATTRS_UNSPECIFIED,
                         &msi_value, sizeof(msi_value), true);
#endif
    }
#endif
    // port->port_last_interrupt |= 0x1000;
    // port->port_last_interrupt |= 0xfff;
    // port->port_last_interrupt |= 0xf;
    // port->port_last_interrupt |= 0x0;
    // qemu_set_irq(baseband->irq, level);
    // pci_set_irq(port_pci_dev, level);
    // pci_set_irq(port_pci_dev, 1);
    // pci_set_irq(pci_dev, 1);
    //  TODO: also try device_reset/power_flip instead of this
#if 0
    //port->skip_reset_clear = true;
    port_devices_set_power(port, false);
    //port->skip_reset_clear = true;
    port_devices_set_power(port, true);
#endif
}
#endif


static void baseband_gpio_coredump(void *opaque, int n, int level)
{
    AppleBasebandState *s = opaque;
    AppleBasebandDeviceState *s_device = s->device;
    bool coredump = !!level;
    assert(n == 0);
    DPRINTF("%s: iOS set_val: old: %d ; new %d\n", __func__,
            s_device->gpio_coredump_val, coredump);
    if (s_device->gpio_coredump_val != coredump) {
        //
    }
    s_device->gpio_coredump_val = coredump;
}

static void baseband_gpio_set_reset_det(DeviceState *dev, int level)
{
    AppleBasebandDeviceState *s = APPLE_BASEBAND_DEVICE(dev);
    DPRINTF("%s: device set_irq: old: %d ; new %d\n", __func__,
            s->gpio_reset_det_val, level);
    s->gpio_reset_det_val = level;
    qemu_set_irq(s->gpio_reset_det_irq, level);
    // apple_baseband_set_irq(s->root, 0, level);
}

static void apple_baseband_add_pcie_cap_hmap(AppleBasebandDeviceState *s,
                                             PCIDevice *dev)
{
    DPRINTF("%s: pci_is_express: %d\n", __func__, pci_is_express(dev));
    g_assert_cmpuint(sizeof(s->hmap), ==, 0x70);
    memset(&s->hmap, 0x0, sizeof(s->hmap));
    s->hmap.vsec_id = 0x24;
    pcie_add_capability(dev, PCI_EXT_CAP_ID_VNDR, 0x0, s->hmap_hardcoded_offset,
                        sizeof(s->hmap));
    // TODO: this might/will not work on big-endian
    // don't override the type, skip the first four bytes.
    memcpy(dev->config + s->hmap_hardcoded_offset + 4, &s->hmap.vsec_id,
           sizeof(s->hmap) - 4);
    // make it read-write, because iOS needs to write to it
    memset(dev->wmask + s->hmap_hardcoded_offset, 0xff, sizeof(s->hmap));
}

static uint8_t *apple_baseband_dma_read(AppleBasebandDeviceState *s,
                                        uint64_t offset, uint64_t size)
{
    uint8_t *buf;

    DPRINTF("%s: READ @ 0x" HWADDR_FMT_plx " size: 0x" HWADDR_FMT_plx "\n",
            __func__, offset, size);

    buf = g_malloc(size);
    if (dma_memory_read(s->dma_as, offset, buf, size, MEMTXATTRS_UNSPECIFIED) !=
        MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to read from DMA.",
                      __func__);
        g_free(buf);
        return NULL;
    }
    return buf;
}

static bool apple_baseband_dma_read_ptr(AppleBasebandDeviceState *s,
                                        uint64_t offset, uint64_t size,
                                        uint8_t *buf)
{
    DPRINTF("%s: READ @ 0x" HWADDR_FMT_plx " size: 0x" HWADDR_FMT_plx "\n",
            __func__, offset, size);

    if (dma_memory_read(s->dma_as, offset, buf, size, MEMTXATTRS_UNSPECIFIED) !=
        MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to read from DMA.",
                      __func__);
        return false;
    }
    return true;
}

static void apple_baseband_dma_write(AppleBasebandDeviceState *s,
                                     uint64_t offset, uint64_t size,
                                     uint8_t *buf)
{
    DPRINTF("%s: WRITE @ 0x" HWADDR_FMT_plx " size: 0x" HWADDR_FMT_plx "\n",
            __func__, offset, size);

    if (dma_memory_write(s->dma_as, offset, buf, size,
                         MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to write to DMA.", __func__);
    }
}

static void
apple_baseband_device_print_context_info(AppleBasebandDeviceState *s)
{
    // g_assert_cmpuint(sizeof(s->baseband_context0), ==, 0x68); // this is also
    // inside the reset function

    if (s->context_addr != 0) {
        if (!apple_baseband_dma_read_ptr(s, s->context_addr, 4,
                                         (uint8_t *)&s->baseband_context0)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to read from DMA_0.",
                          __func__);
            return;
        }

        g_assert_cmpuint(s->baseband_context0.version, ==, 0x1);
        g_assert_cmpuint(s->baseband_context0.size, ==, 0x68);

        if (!apple_baseband_dma_read_ptr(s, s->context_addr,
                                         s->baseband_context0.size,
                                         (uint8_t *)&s->baseband_context0)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to read from DMA_1.",
                          __func__);
            return;
        }

        DPRINTF("%s: version: 0x%x\n", __func__, s->baseband_context0.version);
        DPRINTF("%s: size: 0x%x\n", __func__, s->baseband_context0.size);
        DPRINTF("%s: config: 0x%x\n", __func__, s->baseband_context0.config);
        DPRINTF("%s: peripheral_info_address: 0x" HWADDR_FMT_plx "\n", __func__,
                s->baseband_context0.peripheral_info_address);
        DPRINTF("%s: cr_hia_address: 0x" HWADDR_FMT_plx "\n", __func__,
                s->baseband_context0.cr_hia_address);
        DPRINTF("%s: tr_tia_address: 0x" HWADDR_FMT_plx "\n", __func__,
                s->baseband_context0.tr_tia_address);
        DPRINTF("%s: cr_tia_address: 0x" HWADDR_FMT_plx "\n", __func__,
                s->baseband_context0.cr_tia_address);
        DPRINTF("%s: tr_hia_address: 0x" HWADDR_FMT_plx "\n", __func__,
                s->baseband_context0.tr_hia_address);
        DPRINTF("%s: cr_ia_entries: 0x%x\n", __func__,
                s->baseband_context0.cr_ia_entries);
        DPRINTF("%s: tr_ia_entries: 0x%x\n", __func__,
                s->baseband_context0.tr_ia_entries);
        DPRINTF("%s: mcr_address_low: 0x%x\n", __func__,
                s->baseband_context0.mcr_address_low);
        DPRINTF("%s: mcr_address_high: 0x%x\n", __func__,
                s->baseband_context0.mcr_address_high);
        DPRINTF("%s: mtr_address_low: 0x%x\n", __func__,
                s->baseband_context0.mtr_address_low);
        DPRINTF("%s: mtr_address_high: 0x%x\n", __func__,
                s->baseband_context0.mtr_address_high);
        DPRINTF("%s: mtr_entries: 0x%x\n", __func__,
                s->baseband_context0.mtr_entries);
        DPRINTF("%s: mcr_entries: 0x%x\n", __func__,
                s->baseband_context0.mcr_entries);
        DPRINTF("%s: mtr_doorbell: 0x%x\n", __func__,
                s->baseband_context0.mtr_doorbell);
        DPRINTF("%s: mcr_doorbell: 0x%x\n", __func__,
                s->baseband_context0.mcr_doorbell);
        DPRINTF("%s: mtr_msi: 0x%x\n", __func__, s->baseband_context0.mtr_msi);
        DPRINTF("%s: mcr_msi: 0x%x\n", __func__, s->baseband_context0.mcr_msi);
        DPRINTF("%s: mtr_header_size: 0x%x\n", __func__,
                s->baseband_context0.mtr_header_size);
        DPRINTF("%s: mtr_footer_size: 0x%x\n", __func__,
                s->baseband_context0.mtr_footer_size);
        DPRINTF("%s: mcr_header_size: 0x%x\n", __func__,
                s->baseband_context0.mcr_header_size);
        DPRINTF("%s: mcr_footer_size: 0x%x\n", __func__,
                s->baseband_context0.mcr_footer_size);
        DPRINTF("%s: bit0_out_of_order__bit1_in_place: 0x%x\n", __func__,
                s->baseband_context0.bit0_out_of_order__bit1_in_place);
        DPRINTF("%s: peripheral_info_msi: 0x%x\n", __func__,
                s->baseband_context0.peripheral_info_msi);
        DPRINTF("%s: scratch_pad_address: 0x" HWADDR_FMT_plx "\n", __func__,
                s->baseband_context0.scratch_pad_address);
        DPRINTF("%s: scratch_pad_size: 0x%x\n", __func__,
                s->baseband_context0.scratch_pad_size);
        DPRINTF("%s: field_64: 0x%x\n", __func__,
                s->baseband_context0.field_64);
    }
}

static void
apple_baseband_device_update_image_doorbell(AppleBasebandDeviceState *s)
{
    AppleBasebandState *baseband = s->root;
    ApplePCIEPort *port = s->port;
    if (s->image_ptr != NULL) {
        g_free(s->image_ptr);
        s->image_ptr = NULL;
    }
    if (s->image_addr != 0 && s->image_size != 0) {
        s->image_ptr = g_malloc(s->image_size);
        if (!apple_baseband_dma_read_ptr(s, s->image_addr, s->image_size,
                                         s->image_ptr)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Failed to read image from DMA.",
                          __func__);
            return;
        }

        //
        DPRINTF("%s: image_addr: 0x%" PRIX64 " image_size: 0x%x \n", __func__,
                s->image_addr, s->image_size);
        HEXDUMP("image_first_0x100 bytes", s->image_ptr,
                MIN(s->image_size, 0x100));
        // s->boot_stage = 1;
#if 1
        // apple_baseband_raise_msi(s, 0);
        // apple_pcie_port_temp_lower_msi_irq(port, 0);
        apple_baseband_set_irq(baseband, 0, 1); // TODO: not working yet
#endif
    }
}

static void apple_baseband_device_bar0_write(void *opaque, hwaddr addr,
                                             uint64_t data, unsigned size)
{
    AppleBasebandDeviceState *s = opaque;
    ApplePCIEPort *port = s->port;
    ApplePCIEHost *host = port->host;
    ApplePCIEState *pcie = host->pcie;
    AppleSPMIBasebandState *spmi = APPLE_SPMI_BASEBAND(object_property_get_link(
        OBJECT(qdev_get_machine()), "baseband-spmi", &error_fatal));
    int i, j;

    DPRINTF("%s: WRITE @ 0x" HWADDR_FMT_plx " value: 0x" HWADDR_FMT_plx "\n",
            __func__, addr, data);
    switch (addr) {
    case 0x80: // ICEBBBTIDevice::updateImageDoorbell
        s->boot_stage = data; // new boot stage
        // updateImageDoorbell not only on boot_stage 0x1
        apple_baseband_device_update_image_doorbell(s);
        break;
    case 0x90: // ICEBBRTIDevice::updateControl
        apple_pcie_port_temp_lower_msi_irq(port, 0);
        // bit0 // ICEBBRTIDevice::engage
#if 0
        if ((data & 1) != 0) {
        }
#endif
        // bit1 // ICEBBRTIDevice::initCheck
#if 1
        if ((data & 2) != 0) {
            apple_baseband_device_print_context_info(s);
        }
#endif
        break;
    case 0xa0: // ICEBBRTIDevice::updateSleepControl
        break;
    // case 0x???:
    // ICEBBRTIDevice::updateExtraDoorbell: addr == base + (index * 0x18) + 0x10
    default:
        break;
    }
}

static uint64_t apple_baseband_device_bar0_read(void *opaque, hwaddr addr,
                                                unsigned size)
{
    AppleBasebandDeviceState *s = opaque;
    uint32_t val = 0x0;

    switch (addr) {
    default:
        break;
    }

    DPRINTF("%s: READ @ 0x" HWADDR_FMT_plx " value: 0x%x"
            "\n",
            __func__, addr, val);
    return val;
}

static const MemoryRegionOps bar0_ops = {
    .read = apple_baseband_device_bar0_read,
    .write = apple_baseband_device_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl =
        {
            .min_access_size = 4,
            .max_access_size = 4,
        },
};

static void apple_baseband_device_bar1_write(void *opaque, hwaddr addr,
                                             uint64_t data, unsigned size)
{
    AppleBasebandDeviceState *s = opaque;

    DPRINTF("%s: WRITE @ 0x" HWADDR_FMT_plx " value: 0x" HWADDR_FMT_plx "\n",
            __func__, addr, data);
    switch (addr) {
    case 0x80: // ICEBBBTIDevice::updateImageAddr
        s->image_addr &= (0xffffffffull << 32);
        s->image_addr |= ((data & UINT32_MAX) << 0);
        break;
    case 0x84: // ICEBBBTIDevice::updateImageAddr
        s->image_addr &= (UINT32_MAX << 0);
        s->image_addr |= ((data & UINT32_MAX) << 32);
        break;
    case 0x88: // ICEBBBTIDevice::updateImageSize
        s->image_size = data;
        break;
    case 0x90: // ICEBBRTIDevice::updateContextAddr low
        s->context_addr &= (0xffffffffull << 32);
        s->context_addr |= ((data & UINT32_MAX) << 0);
        break;
    case 0x94: // ICEBBRTIDevice::updateContextAddr high
        s->context_addr &= (UINT32_MAX << 0);
        s->context_addr |= ((data & UINT32_MAX) << 32);
        break;
    case 0x98: // ICEBBRTIDevice::updateWindowBase ; DART window
        break;
    case 0x9c: // ICEBBRTIDevice::updateWindowBase ; DART window
        break;
    case 0xa0: // ICEBBRTIDevice::updateWindowLimit ; DART window
        break;
    case 0xa4: // ICEBBRTIDevice::updateWindowLimit ; DART window
        break;
    default:
        break;
    }
}

typedef struct QEMU_PACKED custom_baseband0_t {
    uint16_t unkn0; // 0x0
    uint8_t chip_id; // 0x2 ; ChipID
    uint8_t unkn1; // 0x3
    uint8_t pad0[6]; // 0x4
    uint8_t serial_number[12]; // 0xa ; ChipSerialNo/SNUM
    uint32_t cert_id; // 0x16 ; CertID/CERTID
    uint8_t public_key_hash[28]; // 0x1a ; PKHASH/CertHash
    uint8_t pad1[6]; // 0x36
} custom_baseband0_t;

static uint64_t apple_baseband_device_bar1_read(void *opaque, hwaddr addr,
                                                unsigned size)
{
    AppleBasebandDeviceState *s = opaque;
    ApplePCIEPort *port = s->port;
    // uint32_t *mmio = &s->vendor_reg[addr >> 2];
    // uint32_t val = *mmio;
    uint32_t val = 0x0;
    uint32_t vals[0x3c / 4] = { 0 };
    custom_baseband0_t custom_baseband0 = { 0 };
    // g_assert_cmpuint(sizeof(custom_baseband0), ==, 60);
    memset(&custom_baseband0, 0x0, sizeof(custom_baseband0));

    switch (addr) {
    case 0x0: // boot stage
        val = s->boot_stage;
        // baseband_gpio_set_reset_det(DEVICE(s), 0);
        // baseband_gpio_set_reset_det(DEVICE(s), 1);
        break;
    case 0x4 ... 0x3c:
        custom_baseband0.unkn0 = 0xdead;
        // custom_baseband0.chip_id = 0x60; // chip-id
        custom_baseband0.chip_id = 0x68; // chip-id ; maybe use 0x68
        custom_baseband0.unkn1 = 0xfe;
        memcpy(custom_baseband0.pad0, "FOBART",
               sizeof(custom_baseband0.pad0)); // non-null-terminated
        memcpy(custom_baseband0.serial_number, "SNUMSNUMSNUM",
               sizeof(custom_baseband0.serial_number)); // non-null-terminated
        // iPhone 11 value from wiki. random iPhone 7 log value is found in a
        // wiki page, so the values should be good.
        custom_baseband0.cert_id = 524245983;
        memcpy(custom_baseband0.public_key_hash, "HASHHASHHASHHASHHASHHASHHASH",
               sizeof(custom_baseband0.public_key_hash)); // non-null-terminated
        memcpy(custom_baseband0.pad1, "67890A",
               sizeof(custom_baseband0.pad1)); // non-null-terminated
        uint8_t *custom_baseband0_ptr = (uint8_t *)&custom_baseband0;
        val = ldl_le_p(custom_baseband0_ptr + addr - 0x4);
        break;
    case 0x60: // ICEBBRTIDevice::getImageResponse ; ICEBBBTIDevice::getExitCode
        // ACIPCBTIDevice::successExitCode: says 0x1 only
        // IOACIPCBTIDevice::successExitCode: says 0x1 and/or 0x10.
        val = 0x1;
        apple_pcie_port_temp_lower_msi_irq(port, 0);
        break;
    case 0x64 ... 0x70: // ICEBBBTIDevice::msiInterrupt
        val = 0x0;
        apple_pcie_port_temp_lower_msi_irq(port, 0);
        break;
    case 0x88: // ICEBBRTIDevice::getImageSize
        val = s->image_size;
        break;
    case 0x8c: // ICEBBRTIDevice::getStatus
        val = 0x1;
        break;
    case 0xac: // ICEBBRTIDevice::getCapability
        val = 0x0;
        break;
    default:
        break;
    }

    DPRINTF("%s: READ @ 0x" HWADDR_FMT_plx " value: 0x%x"
            " size %d\n",
            __func__, addr, val, size);
    return val;
}

static const MemoryRegionOps bar1_ops = {
    .read = apple_baseband_device_bar1_read,
    .write = apple_baseband_device_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl =
        {
            .min_access_size = 4,
            .max_access_size = 4,
        },
};

static void apple_baseband_device_bar2_write(void *opaque, hwaddr addr,
                                             uint64_t data, unsigned size)
{
    AppleBasebandDeviceState *s = opaque;

    DPRINTF("%s: WRITE @ 0x" HWADDR_FMT_plx " value: 0x" HWADDR_FMT_plx "\n",
            __func__, addr, data);
    switch (addr) {
    default:
        break;
    }
}

static uint64_t apple_baseband_device_bar2_read(void *opaque, hwaddr addr,
                                                unsigned size)
{
    AppleBasebandDeviceState *s = opaque;
    uint32_t val = 0x0;

    switch (addr) {
    default:
        break;
    }

    DPRINTF("%s: READ @ 0x" HWADDR_FMT_plx " value: 0x%x"
            "\n",
            __func__, addr, val);
    return val;
}

static const MemoryRegionOps bar2_ops = {
    .read = apple_baseband_device_bar2_read,
    .write = apple_baseband_device_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl =
        {
            .min_access_size = 4,
            .max_access_size = 4,
        },
};

static uint32_t apple_baseband_custom_pci_config_read(PCIDevice *d,
                                                      uint32_t address, int len)
{
    AppleBasebandState *baseband = APPLE_BASEBAND(object_property_get_link(
        OBJECT(qdev_get_machine()), "baseband", &error_fatal));
    AppleBasebandDeviceState *baseband_device = baseband->device;
    ApplePCIEPort *port = baseband_device->port;
    ApplePCIEHost *host = port->host;
    ApplePCIEState *pcie = host->pcie;
    PCIDevice *port_pci_dev = PCI_DEVICE(port);
    PCIDevice *pci_dev = PCI_DEVICE(baseband_device);
    uint32_t val;

    switch (address) {
    default:
    jump_default:
        val = pci_default_read_config(d, address, len);
        DPRINTF("%s: default: READ DEFAULT @ 0x%x value:"
                " 0x%x\n",
                __func__, address, val);
        if (address == (baseband_device->hmap_hardcoded_offset + 0x6c)) {
            // read end
            DPRINTF("%s: read end\n", __func__);
#if 0
            baseband_gpio_set_reset_det(DEVICE(baseband_device), 0); // 0 means 1 == reset detected
            baseband_gpio_set_reset_det(DEVICE(baseband_device), 1); // 1 means 0 == alive
#endif
#if 0
            apple_baseband_raise_msi(baseband_device, 0);
            //apple_baseband_raise_msi(baseband_device, 0x18);
            //apple_baseband_set_irq(baseband, 0, 1);
#endif
#if 0
            //port->skip_reset_clear = true;
            port_devices_set_power(port, false);
            //port->skip_reset_clear = true;
            //port_devices_set_power(port, true);
#endif
#if 0
            pci_set_power(pci_dev, false);
            pci_set_power(pci_dev, true);
#endif
        }
        break;
    }

    DPRINTF("%s: READ @ 0x%x value: 0x%x\n", __func__, address, val);
    return val;
}

static void apple_baseband_custom_pci_config_write(PCIDevice *d,
                                                   uint32_t address,
                                                   uint32_t val, int len)
{
    AppleBasebandState *baseband = APPLE_BASEBAND(object_property_get_link(
        OBJECT(qdev_get_machine()), "baseband", &error_fatal));
    AppleBasebandDeviceState *baseband_device = baseband->device;
    ApplePCIEPort *port = baseband_device->port;
    ApplePCIEHost *host = port->host;
    ApplePCIEState *pcie = host->pcie;
    PCIDevice *port_pci_dev = PCI_DEVICE(port);
    PCIDevice *pci_dev = PCI_DEVICE(baseband_device);

    DPRINTF("%s: WRITE @ 0x%x value: 0x%x\n", __func__, address, val);

    switch (address) {
    default:
    jump_default:
        DPRINTF("%s: default: WRITE DEFAULT @ 0x%x value:"
                " 0x%x\n",
                __func__, address, val);
        pci_default_write_config(d, address, val, len);
        if (address == (baseband_device->hmap_hardcoded_offset + 0xc)) {
            // write end
            DPRINTF("%s: write end\n", __func__);
#if 0
            baseband_gpio_set_reset_det(DEVICE(baseband_device), 0); // 0 means 1 == reset detected
            baseband_gpio_set_reset_det(DEVICE(baseband_device), 1); // 1 means 0 == alive
#endif
#if 0
            // maybe should happen at the write of hmap_hardcoded_offset + 0xc instead.
            // apple_baseband_raise_msi(baseband_device, 0);
            apple_baseband_set_irq(baseband, 0, 1);
#endif
#if 0
            //port->skip_reset_clear = true;
            port_devices_set_power(port, false);
            //port->skip_reset_clear = true;
            //port_devices_set_power(port, true);
#endif
#if 0
            pci_set_power(pci_dev, false);
            pci_set_power(pci_dev, true);
#endif
        }
        break;
    }
}

static uint8_t smc_key_gP07_read(AppleSMCState *s, SMCKey *key,
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

static uint8_t smc_key_gP07_write(AppleSMCState *s, SMCKey *key,
                                  SMCKeyData *data, void *payload,
                                  uint8_t length)
{
    AppleRTKit *rtk;
    uint32_t value;

    AppleBasebandState *baseband = APPLE_BASEBAND(object_property_get_link(
        OBJECT(qdev_get_machine()), "baseband", &error_fatal));
    AppleBasebandDeviceState *baseband_device = baseband->device;
    ApplePCIEPort *port = baseband_device->port;
    ApplePCIEHost *host = port->host;
    ApplePCIEState *pcie = host->pcie;
    PCIDevice *port_pci_dev = PCI_DEVICE(port);

    if (payload == NULL || length != key->info.size) {
        return kSMCBadArgumentError;
    }

    rtk = APPLE_RTKIT(s);
    value = ldl_le_p(payload);

    // Do not use data->data here, as it only contains the data last written to
    // by the read function (smc_key_gP09_read)

    DPRINTF("%s: value: 0x%08x ; length: 0x%08x\n", __func__, value, length);

    switch (value) {
    // function-bb_on: 0x00800000 write?
    // AppleBasebandPlatform::setPowerOnBBPMUPinGated: bit0 == enable
    case 0x00800000:
    case 0x00800001: {
        int enable_baseband_power = (value & 1) != 0;
        DPRINTF("%s: setPowerOnBBPMUPinGated/bb_on enable: %d\n", __func__,
                enable_baseband_power);
#if 0
        // the move from pmuexton to here was unnecessary, because this doesn't seem to influence AppleBasebandPlatform::resetDetectInterrupt, and having this at the previous location also leads to further pcie access attempts
        // yet, bb_on seems to be the correct place, since being used at various reset functions
        // still, it doesn't seem to appear to be the actual correct place
        //baseband_gpio_set_reset_det(DEVICE(baseband_device), 0); // 0 means 1 == reset detected
        //baseband_gpio_set_reset_det(DEVICE(baseband_device), 1); // 1 means 0 == alive
        // 0 means 1 == reset detected ; 1 means 0 == alive
        //baseband_gpio_set_reset_det(DEVICE(baseband_device), enable_baseband_power); // 1 means 0 == alive
        apple_baseband_set_irq(baseband, 0, 1);
#endif
        return kSMCSuccess;
    }
    default:
        DPRINTF("%s: UNKNOWN VALUE: 0x%08x\n", __func__, value);
        return kSMCBadFuncParameter;
    }
}

static uint8_t smc_key_gP09_read(AppleSMCState *s, SMCKey *key,
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
    // function-pmu_exton: 0x02000000 read?
    case 0x02000000: {
        DPRINTF("%s: pmu_exton\n", __func__);
        return kSMCSuccess;
    }
    case 0x06000000: {
        DPRINTF("%s: getVectorType\n", __func__);
        // AppleSMCPMU::getVectorType
        // value 0x0/0x1 means vector type "Level", else "Edge"
        // tmpval0 = 0x0;
        // tmpval0 = 0x1;
        tmpval0 = 0x2;
        memcpy(data->data, &tmpval0, sizeof(tmpval0));
        return kSMCSuccess;
    }
    default:
        DPRINTF("%s: UNKNOWN VALUE: 0x%08x\n", __func__, value);
        return kSMCBadFuncParameter;
    }
}

static uint8_t smc_key_gP09_write(AppleSMCState *s, SMCKey *key,
                                  SMCKeyData *data, void *payload,
                                  uint8_t length)
{
    AppleRTKit *rtk;
    uint32_t value;
    KeyResponse r;

    AppleBasebandState *baseband = APPLE_BASEBAND(object_property_get_link(
        OBJECT(qdev_get_machine()), "baseband", &error_fatal));
    AppleBasebandDeviceState *baseband_device = baseband->device;
    ApplePCIEPort *port = baseband_device->port;
    ApplePCIEHost *host = port->host;
    ApplePCIEState *pcie = host->pcie;
    PCIDevice *port_pci_dev = PCI_DEVICE(port);

    if (payload == NULL || length != key->info.size) {
        return kSMCBadArgumentError;
    }

    rtk = APPLE_RTKIT(s);
    value = ldl_le_p(payload);

    // Do not use data->data here, as it only contains the data last written to
    // by the read function (smc_key_gP09_read)

    DPRINTF("%s: value: 0x%08x ; length: 0x%08x\n", __func__, value, length);

    switch (value) {
    case 0x04000000: {
        // disableVectorHard/IENA
        DPRINTF("%s: disableVectorHard\n", __func__);
        // apple_baseband_set_irq(baseband, 0, 1);
        // goto enableVector;
        return kSMCSuccess;
    }
    case 0x04000001: {
        // enableVector/IENA
        DPRINTF("%s: enableVector\n", __func__);
        // apple_baseband_set_irq(baseband, 0, 1);
        // baseband_gpio_set_reset_det(DEVICE(baseband_device), false);
#if 0
        enableVector:
        memset(&r, 0, sizeof(r));
        r.status = SMC_NOTIFICATION;
        //r.response[0] = 0x01; // maybe enable afterwards (directly after disabling)
        r.response[0] = (value & 1); // maybe enable afterwards (directly after disabling)
        // r.response[1] = 0x15; // t8015: gP15 ; match with function name
        r.response[1] = 0x09; // for type 2: vectorNumber-7 probably t8030: gP09 ; match with function name
        r.response[2] = 0x02; // type: 1==lid state? 2==vectorState? 3==hid buttons?
        r.response[3] = 0x72;
        apple_rtkit_send_user_msg(rtk, kSMCKeyEndpoint, r.raw);
#endif
        return kSMCSuccess;
    }
    // function-pmu_exton_config: 0x07000000/0x07000001 write?
    case 0x07000000:
    // case 0x0700dead:
    case 0x07000001: {
        // AppleBasebandPlatform::pmuExtOnConfigGated
        // // bit0 == use_pmuExtOnConfigOverride_enabled == maybe enable
        // baseband bit0 == pull-down enabled
        int use_pmuExtOnConfigOverride_pulldown = (value & 1) != 0;
        DPRINTF("%s: pmuExtOnConfigGated/pmu_exton_config enable: %d\n",
                __func__, use_pmuExtOnConfigOverride_pulldown);
        if (!use_pmuExtOnConfigOverride_pulldown) {
            DPRINTF("%s: ignoring pmuExtOnConfigGated/pmu_exton_config enable:"
                    " %d\n",
                    __func__, use_pmuExtOnConfigOverride_pulldown);
            // DPRINTF("%s: set false pmuExtOnConfigGated/pmu_exton_config
            // enable:"
            //         " %d\n",
            //         __func__, use_pmuExtOnConfigOverride_pulldown);
            // baseband_gpio_set_reset_det(DEVICE(baseband_device), false);
            // DPRINTF("%s: set true pmuExtOnConfigGated/pmu_exton_config
            // enable:"
            //         " %d\n",
            //         __func__, use_pmuExtOnConfigOverride_pulldown);
            // baseband_gpio_set_reset_det(DEVICE(baseband_device), true);
            return kSMCSuccess;
        }
#if 0
        AppleSPMIBasebandState *baseband_spmi = APPLE_SPMI_BASEBAND(object_property_get_link(OBJECT(qdev_get_machine()), "baseband-spmi", &error_fatal));
        g_assert_nonnull(baseband_spmi);
#endif
#if 0
        // having this at this position influences AppleBasebandPlatform::resetDetectInterrupt
        // this seem to lead to further pcie access attempts, five minutes after boot
        // whoops, that was actually just readTimeCounter/readEntriesCounter
        //baseband_gpio_set_reset_det(DEVICE(baseband_device), 0); // 0 means 1 == reset detected ; getModemResetGated() must return 0x1
        //baseband_gpio_set_reset_det(DEVICE(baseband_device), 1); // 1 means 0 == alive ; getModemResetGated() must return 0x0
        // 0 means 1 == reset detected ; 1 means 0 == alive
        // edge/flip-flip is needed here for the interrupt ; is it actually needed?
        // pulldown == 0 == reset then alive
        // pulldown == 1 == alive then reset
        // use_pmuExtOnConfigOverride_pulldown == alive then reset, but should be the other way around
        // use_pmuExtOnConfigOverride_pulldown then !use_pmuExtOnConfigOverride_pulldown is reset
        baseband_gpio_set_reset_det(DEVICE(baseband_device), use_pmuExtOnConfigOverride_pulldown);
        baseband_gpio_set_reset_det(DEVICE(baseband_device), !use_pmuExtOnConfigOverride_pulldown);
#endif
#if 0
        // this might lead to "reset detected" ; ... or not
        memset(&r, 0, sizeof(r));
        r.status = SMC_NOTIFICATION;
        r.response[0] = use_pmuExtOnConfigOverride_pulldown;
        //r.response[0] = !use_pmuExtOnConfigOverride_pulldown;
        //r.response[0] = 0x01; // maybe enable afterwards (directly after disabling)
        // r.response[1] = 0x15; // t8015: gP15 ; match with function name
        r.response[1] = 0x09; // for type 2: vectorNumber-7 probably t8030: gP09 ; match with function name
        r.response[2] = 0x02; // type: 1==lid state? 2==vectorState? 3==hid buttons?
        r.response[3] = 0x72;
        apple_rtkit_send_user_msg(rtk, kSMCKeyEndpoint, r.raw);
        return kSMCSuccess;
#endif
        // apple_baseband_set_irq(baseband, 0, 1);
        return kSMCSuccess;
    }
    default:
        DPRINTF("%s: UNKNOWN VALUE: 0x%08x\n", __func__, value);
        return kSMCBadFuncParameter;
    }
}

static uint8_t smc_key_gP11_read(AppleSMCState *s, SMCKey *key,
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
    // gP11 is actually for amfm (wifi/bluetooth-pcie bridge)
    default:
        DPRINTF("%s: UNKNOWN VALUE: 0x%08x\n", __func__, value);
        return kSMCBadFuncParameter;
    }
}

static uint8_t smc_key_gP11_write(AppleSMCState *s, SMCKey *key,
                                  SMCKeyData *data, void *payload,
                                  uint8_t length)
{
    AppleRTKit *rtk;
    uint32_t value;

    AppleBasebandState *baseband = APPLE_BASEBAND(object_property_get_link(
        OBJECT(qdev_get_machine()), "baseband", &error_fatal));
    ApplePCIEPort *port = baseband->device->port;
    ApplePCIEHost *host = port->host;
    ApplePCIEState *pcie = host->pcie;
    PCIDevice *port_pci_dev = PCI_DEVICE(port);

    if (payload == NULL || length != key->info.size) {
        return kSMCBadArgumentError;
    }

    rtk = APPLE_RTKIT(s);
    value = ldl_le_p(payload);

    // Do not use data->data here, as it only contains the data last written to
    // by the read function (smc_key_gP09_read)

    DPRINTF("%s: value: 0x%08x ; length: 0x%08x\n", __func__, value, length);

    switch (value) {
    // gP11 is actually for amfm (wifi/bluetooth-pcie bridge)
    default:
        DPRINTF("%s: UNKNOWN VALUE: 0x%08x\n", __func__, value);
        return kSMCBadFuncParameter;
    }
}

SysBusDevice *apple_baseband_create(DTBNode *node, PCIBus *pci_bus,
                                    ApplePCIEPort *port)
{
    DeviceState *dev;
    AppleBasebandState *s;
    SysBusDevice *sbd;
    DTBNode *child;
    DTBProp *prop;
    uint64_t *reg;
    MemoryRegion *alias;
    PCIDevice *pci_dev;

    dev = qdev_new(TYPE_APPLE_BASEBAND);
    s = APPLE_BASEBAND(dev);
    sbd = SYS_BUS_DEVICE(dev);

#if 0
    prop = dtb_find_prop(node, "reg");
    g_assert_nonnull(prop);

    reg = (uint64_t *)prop->data;
#endif

#if 1
    // sysbus_init_irq(sbd, &s->irq);
    //  qdev_init_gpio_in_named(dev, apple_baseband_set_irq, "interrupt_pci",
    //  1);
#endif

    s->pci_bus = pci_bus;
    pci_dev = pci_new(-1, TYPE_APPLE_BASEBAND_DEVICE);
    s->device = APPLE_BASEBAND_DEVICE(pci_dev);
    s->device->root = s;
    s->device->port = port;
    s->device->dma_mr = port->dma_mr;
    s->device->dma_as = &port->dma_as;

    object_property_add_child(OBJECT(s), "device", OBJECT(s->device));

    // smc-pmu
    AppleSMCState *smc = APPLE_SMC_IOP(object_property_get_link(
        OBJECT(qdev_get_machine()), "smc", &error_fatal));
    apple_smc_create_key_func(smc, 'gP07', 4, SMCKeyTypeUInt32,
                              SMC_ATTR_FUNCTION | SMC_ATTR_WRITEABLE |
                                  SMC_ATTR_READABLE | 0x20,
                              &smc_key_gP07_read, &smc_key_gP07_write);
    apple_smc_create_key_func(smc, 'gP09', 4, SMCKeyTypeUInt32,
                              SMC_ATTR_FUNCTION | SMC_ATTR_WRITEABLE |
                                  SMC_ATTR_READABLE | 0x20,
                              &smc_key_gP09_read, &smc_key_gP09_write);
    apple_smc_create_key_func(smc, 'gP11', 4, SMCKeyTypeUInt32,
                              SMC_ATTR_FUNCTION | SMC_ATTR_WRITEABLE |
                                  SMC_ATTR_READABLE | 0x20,
                              &smc_key_gP11_read, &smc_key_gP11_write);
    // TODO: gP09/gP11 are 0xf0, so gP07 should be as well.
    // TODO: missing, according to t8015, gP01/gp05/gp0e/gp0f/gp12/gp13/gp15

    return sbd;
}

#if 0
static void apple_baseband_pci_msi_trigger(PCIDevice *dev, MSIMessage msg)
{
    PCIDevice *parent_dev = pci_bridge_get_device(pci_get_bus(dev));
    DPRINTF("%s: dev: name/devfn: %s:%x\n", __func__, dev->name, dev->devfn);
    DPRINTF("%s: parent_dev: name/devfn: %s:%x\n",
            __func__, parent_dev->name, parent_dev->devfn);
    // parent_dev->msi_trigger(parent_dev, msg);
    AppleBasebandState *baseband = APPLE_BASEBAND(object_property_get_link(
        OBJECT(qdev_get_machine()), "baseband", &error_fatal));
    AppleBasebandDeviceState *baseband_device = baseband->device;
    ApplePCIEPort *port = baseband_device->port;
    ApplePCIEHost *host = port->host;
    ApplePCIEState *pcie = host->pcie;
    PCIDevice *port_pci_dev = PCI_DEVICE(port);
    PCIDevice *pci_dev = PCI_DEVICE(baseband_device);
#if 0
    //pci_dev->msi_trigger(pci_dev, msg);
    port_pci_dev->msi_trigger(port_pci_dev, msg);
#endif
#if 1
    //MSIMessage msg = {};
    MemTxAttrs attrs = {
        //.requester_id = pci_requester_id(pci_dev)
        .requester_id = pci_requester_id(port_pci_dev)
    };

    //if (msi_enabled(pci_dev))
    {
        //msg = msi_get_message(pci_dev, 0);
        //address_space_stl_le(&address_space_memory, msg.address, msg.data, attrs, NULL);
        address_space_stl_le(&port->dma_as, msg.address, msg.data, attrs, NULL);
    }
#endif
}
#endif

static void apple_baseband_device_pci_realize(PCIDevice *dev, Error **errp)
{
    AppleBasebandDeviceState *s = APPLE_BASEBAND_DEVICE(dev);
    uint8_t *pci_conf = dev->config;
    int ret, i;

    pci_conf[PCI_INTERRUPT_PIN] = 1;
    // wifi and bluetoth seem to have those ids, but not baseband
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0);

    memory_region_init_io(&s->bar0, OBJECT(dev), &bar0_ops, s,
                          "apple-baseband-device-bar0",
                          APPLE_BASEBAND_DEVICE_BAR0_SIZE);
    memory_region_init_io(&s->bar1, OBJECT(dev), &bar1_ops, s,
                          "apple-baseband-device-bar1",
                          APPLE_BASEBAND_DEVICE_BAR1_SIZE);
#if 0
    memory_region_init_io(&s->bar2, OBJECT(dev), &bar2_ops, s,
                          "apple-baseband-device-bar2",
                          APPLE_BASEBAND_DEVICE_BAR2_SIZE);
#endif

    g_assert_true(pci_is_express(dev));
    pcie_endpoint_cap_init(dev, 0x70);

    pcie_cap_deverr_init(dev);

    msi_init(dev, 0x50, 1, true, false, &error_fatal);
    pci_pm_init(dev, 0x40, &error_fatal);
#if 1
    // warning: this will override the settings of the ports as well.
    // for T8030
    if (s->port->maximum_link_speed == 2) {
        // S8000's baseband actually seems to have 1, not 2. s3e has 2.
        pcie_cap_fill_link_ep_usp(dev, QEMU_PCI_EXP_LNK_X1,
                                  QEMU_PCI_EXP_LNK_8GT);
    }
    // for S8000/T8015(?)
    if (s->port->maximum_link_speed == 1) {
        // might also need to be X1 instead of X2
        pcie_cap_fill_link_ep_usp(dev, QEMU_PCI_EXP_LNK_X2,
                                  QEMU_PCI_EXP_LNK_5GT);
    }
#endif
    // sizes: 0x50 for the bridges and qualcomm baseband,
    // 0x3c for broadcom wifi, 0x48 for nvme
    // versions: 1 for broadcom wifi, 2 for the rest
    // // pcie_aer_init(pci_dev, 1, 0x100, PCI_ERR_SIZEOF, &error_fatal);
    // pcie_aer_init(dev, PCI_ERR_VER, 0x100, 0x50, &error_fatal);
    pcie_aer_init(dev, PCI_ERR_VER, 0x100, PCI_ERR_SIZEOF, &error_fatal);

    // TODO: under S8000/T8015, bar0/bar2 are 64-bit, but t8030 doesn't seem to
    // like that. even though that it says bar0==0x10 ; bar1 == 0x18 inside
    // AppleConvergedPCI::mapBarGated
#if 0
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64, &s->bar0);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64, &s->bar1);
#endif
#if 1
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar1);
#if 0
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar2);
#endif
#endif
#define BASEBAND_BAR_SUB_ADDR 0x40000000ULL
#if 1
    memory_region_init(&s->container, OBJECT(s), "baseband-bar-container",
                       APPLE_BASEBAND_DEVICE_BAR0_SIZE +
                           APPLE_BASEBAND_DEVICE_BAR1_SIZE);
#endif
#if 0
    memory_region_init(&s->container, OBJECT(s), "baseband-bar-container",
                       APPLE_BASEBAND_DEVICE_BAR0_SIZE +
                       APPLE_BASEBAND_DEVICE_BAR1_SIZE +
                       APPLE_BASEBAND_DEVICE_BAR2_SIZE);
#endif
    // these aliases are needed, because iOS will mess with the pci subregions
    memory_region_init_alias(&s->bar0_alias, OBJECT(s), "baseband-bar0-alias",
                             &s->bar0, 0x0, APPLE_BASEBAND_DEVICE_BAR0_SIZE);
    memory_region_init_alias(&s->bar1_alias, OBJECT(s), "baseband-bar1-alias",
                             &s->bar1, 0x0, APPLE_BASEBAND_DEVICE_BAR1_SIZE);
#if 0
    memory_region_init_alias(&s->bar2_alias, OBJECT(s), "baseband-bar2-alias",
                             &s->bar2, 0x0, APPLE_BASEBAND_DEVICE_BAR2_SIZE);
#endif
    // this needs to be switch precisely here, because both the emulator and iOS
    // have some "damned if you do, damned if you don't" behavior.
    // apparently, the bars need to be mapped in reverse. easier than keep
    // renaming shit for two/three bars
#if 1
    // for two bars
    memory_region_add_subregion(&s->container, 0x0000, &s->bar1_alias);
    memory_region_add_subregion(&s->container, APPLE_BASEBAND_DEVICE_BAR1_SIZE,
                                &s->bar0_alias);
#endif
#if 0
    // for three bars
    memory_region_add_subregion(&s->container, 0x0000, &s->bar2_alias);
    memory_region_add_subregion(&s->container, APPLE_BASEBAND_DEVICE_BAR2_SIZE,
                                &s->bar1_alias);
    memory_region_add_subregion(&s->container,
                                APPLE_BASEBAND_DEVICE_BAR2_SIZE +
                                APPLE_BASEBAND_DEVICE_BAR1_SIZE,
                                &s->bar0_alias);
#endif
    memory_region_add_subregion(get_system_memory(),
                                APCIE_ROOT_COMMON_ADDRESS +
                                    BASEBAND_BAR_SUB_ADDR + 0x0000,
                                &s->container);
    s->image_ptr = NULL;
    // setting msi_trigger doesn't work here
    // dev->msi_trigger = apple_baseband_pci_msi_trigger;
}

static void apple_baseband_device_qdev_reset_hold(Object *obj, ResetType type)
{
    AppleBasebandDeviceState *s = APPLE_BASEBAND_DEVICE(obj);
    PCIDevice *dev = PCI_DEVICE(obj);

    pci_set_word(dev->config + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    // Don't risk any overlap here. e.g. with AER
    s->hmap_hardcoded_offset = 0x180;
    // s->hmap_hardcoded_offset = 0x100;
    // HMAP might not actually be required
    // apple_baseband_add_pcie_cap_hmap(s, dev);
    // apple_baseband_add_pcie_cap_l1ss(s, dev);
    // TODO: maybe check coredump value and handling
    s->gpio_coredump_val = 0;
    s->gpio_reset_det_val = 0;
    // baseband_gpio_set_reset_det(DEVICE(s), 0); // 0 means 1 == reset detected
    baseband_gpio_set_reset_det(DEVICE(s), 1); // 1 means 0 == alive

    // s->boot_stage = 0xfeedb007; // rom stage is legacy
    // s->boot_stage = 0xffffffff; // failed to read execution environment
    s->boot_stage = 0x0;
    // s->boot_stage = 0x2; // this stage will skip HMAP, but progresses further
    s->context_addr = 0x0;
    s->image_addr = 0x0;
    s->image_size = 0x0;
    if (s->image_ptr != NULL) {
        g_free(s->image_ptr);
        s->image_ptr = NULL;
    }
    memset(&s->baseband_context0, 0, sizeof(s->baseband_context0));
    g_assert_cmpuint(sizeof(s->baseband_context0), ==, 0x68);
    g_assert_cmpuint(sizeof(custom_baseband0_t), ==, 60);

    // TODO: pcie_cap_slot_reset can and will silently revert
    // set_power/set_enable when it's being done here
    DPRINTF("%s: port_manual_enable: %d ; dev->enabled: %d\n", __func__,
            s->port->manual_enable, dev->enabled);
}

static void apple_baseband_device_pci_uninit(PCIDevice *dev)
{
    AppleBasebandDeviceState *s = APPLE_BASEBAND_DEVICE(dev);

    pcie_aer_exit(dev);
    pcie_cap_exit(dev);
    msi_uninit(dev);
}

static void apple_baseband_device_class_init(ObjectClass *class,
                                             const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *c = PCI_DEVICE_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);

    c->realize = apple_baseband_device_pci_realize;
    c->exit = apple_baseband_device_pci_uninit;
    // changed the values from s8000 to t8015
    // and from t8015 to what the internet says might be t8030
    c->vendor_id = PCI_VENDOR_ID_INTEL; // t8015 && t8030
    // it appears that the intel x-gold product id's are just model number plus
    // 0.
    // c->device_id = 0x7480; // t8015
    c->device_id = 0x7660; // t8030
    c->revision = 0x01; // t8015 && t8030?
    c->class_id = 0x0d40; // t8015 && t8030
    c->config_read = apple_baseband_custom_pci_config_read;
    c->config_write = apple_baseband_custom_pci_config_write;

    rc->phases.hold = apple_baseband_device_qdev_reset_hold;

    dc->desc = "Apple Baseband Device";
    dc->user_creatable = false;

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);

    dc->hotpluggable = false;
}

static void apple_baseband_realize(DeviceState *dev, Error **errp)
{
    AppleBasebandState *s = APPLE_BASEBAND(dev);
    AppleBasebandDeviceState *s_device = s->device;
    PCIDevice *pci_dev = PCI_DEVICE(s->device);
    qdev_realize(DEVICE(s->device), BUS(s->pci_bus), &error_fatal);

    // qdev_init_gpio_in_named(DEVICE(s), apple_baseband_set_irq,
    //                         "interrupt_pci", 1);
    qdev_init_gpio_in_named(DEVICE(s), baseband_gpio_coredump,
                            BASEBAND_GPIO_COREDUMP, 1);
    qdev_init_gpio_out_named(DEVICE(s), &s_device->gpio_reset_det_irq,
                             BASEBAND_GPIO_RESET_DET_OUT, 1);

    // setting msi_trigger here seems to work to some extend
    // pci_dev->msi_trigger = apple_baseband_pci_msi_trigger;
    // // PCIDevice *parent_dev = pci_bridge_get_device(pci_get_bus(pci_dev));
    // // parent_dev->msi_trigger = apple_baseband_pci_msi_trigger;
}

static void apple_baseband_unrealize(DeviceState *dev)
{
    AppleBasebandState *s = APPLE_BASEBAND(dev);
}

static const VMStateDescription vmstate_apple_baseband = {
    .name = "apple_baseband",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields =
        (const VMStateField[]){
            VMSTATE_END_OF_LIST(),
        }
};

static void apple_baseband_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = apple_baseband_realize;
    dc->unrealize = apple_baseband_unrealize;
    dc->desc = "Apple Baseband";
    dc->vmsd = &vmstate_apple_baseband;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo apple_baseband_types[] = {
    {
        .name = TYPE_APPLE_BASEBAND_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(AppleBasebandDeviceState),
        .class_init = apple_baseband_device_class_init,
        .interfaces = (InterfaceInfo[]){ { INTERFACE_PCIE_DEVICE }, {} },
    },
    {
        .name = TYPE_APPLE_BASEBAND,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AppleBasebandState),
        .class_init = apple_baseband_class_init,
    },
};

DEFINE_TYPES(apple_baseband_types)
