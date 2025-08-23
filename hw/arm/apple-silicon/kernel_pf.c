/*
 * ChefKiss Kernel Patch Finder.
 *
 * Copyright (c) 2025 Visual Ehrmanntraut (VisualEhrmanntraut).
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

#include "hw/arm/apple-silicon/mem.h"
#include "hw/arm/apple-silicon/pf.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"

#define NOP (0xD503201F)
#define NOP_BYTES 0x1F, 0x20, 0x03, 0xD5
#define RET (0xD65F03C0)
#define RETAB (0xD65F0FFF)
#define PACIBSP (0xD503237F)

static void ck_kernel_pf_apfs_patches(CkPfRange *range)
{
    uint8_t find_root_auth[] = {
        0x68, 0x00, 0x28, 0x37, // tbnz w8, 5, 0xC
        0X00, 0x0A, 0x80, 0x52, // mov w0, 0x50
        0xC0, 0x03, 0x5F, 0xD6, // ret
    };
    uint8_t repl_root_auth[] = { NOP_BYTES, 0x00, 0x00, 0x80,
                                 0x52 }; // mov w0, #0
    ck_pf_find_replace(range, "bypass root authentication", find_root_auth,
                       NULL, sizeof(find_root_auth), repl_root_auth, NULL, 0,
                       sizeof(repl_root_auth));

    uint8_t find_root_rw[] = {
        0x00, 0x00, 0x70, 0x37, // tbnz w0, 0xE, ?
        0xA0, 0x03, 0x40, 0xB9, // ldr x?, [x29/sp, ?]
        0x00, 0x78, 0x1F, 0x12, // and w?, w?, 0xFFFFFFFE
        0xA0, 0x03, 0x00, 0xB9, // str x?, [x29/sp, ?]
    };
    uint8_t mask_root_rw[] = {
        0x1F, 0x00, 0xF8, 0xFF, 0xA0, 0x03, 0xFE, 0xFF,
        0x00, 0xFC, 0xFF, 0xFF, 0xA0, 0x03, 0xC0, 0xFF,
    };
    uint8_t repl_root_rw[] = { 0x00, 0x00, 0x80, 0x52 }; // mov w0, #0
    ck_pf_find_replace(range, "allow mounting root as r/w", find_root_rw,
                       mask_root_rw, sizeof(find_root_rw), repl_root_rw, NULL,
                       0, sizeof(repl_root_rw));
}

static bool ck_kernel_pf_tc_callback(void *ctx, uint8_t *buffer)
{
    if (((ldl_le_p(buffer - 4) & 0xFF000000) != 0x91000000) &&
        ((ldl_le_p(buffer - 8) & 0xFF000000) != 0x91000000)) {
        return false;
    }

    void *ldrb = ck_pf_find_next_insn(buffer, 256, 0x39402C00, 0xFFFFFC00);
    uint32_t cdhash_param = extract32(ldl_le_p(ldrb), 5, 5);
    void *frame;
    void *start = buffer;
    bool pac;

    frame = ck_pf_find_prev_insn(buffer, 10, 0x910003FD, 0xFF8003FF);
    if (frame == NULL) {
        info_report("%s: Found AMFI (Leaf)", __func__);
    } else {
        info_report("%s: Found AMFI (Routine)", __func__);
        start = ck_pf_find_prev_insn(frame, 10, 0xA9A003E0, 0xFFE003E0);
        if (start == NULL) {
            start = ck_pf_find_prev_insn(frame, 10, 0xD10003FF, 0xFF8003FF);
            if (start == NULL) {
                error_report("%s: Failed to find AMFI start", __func__);
                return false;
            }
        }
    }

    pac = ck_pf_find_prev_insn(start, 5, PACIBSP, 0xFFFFFFFF) != NULL;
    switch (cdhash_param) {
    case 0: {
        // adrp x8, ?
        void *adrp = ck_pf_find_prev_insn(start, 10, 0x90000008, 0x9F00001F);
        if (adrp != NULL) {
            start = adrp;
        }
        stl_le_p(start, 0x52802020); // mov w0, 0x101
        stl_le_p(start + 4, (pac ? RETAB : RET));
        return true;
    }
    case 1:
        stl_le_p(start, 0x52800040); // mov w0, 2
        stl_le_p(start + 4, 0x39000040); // strb w0, [x2]
        stl_le_p(start + 8, 0x52800020); // mov w0, 1
        stl_le_p(start + 12, 0x39000060); // strb w0, [x3]
        stl_le_p(start + 16, 0x52800020); // mov w0, 1
        stl_le_p(start + 20, (pac ? RETAB : RET));
        return true;
    default:
        error_report("Found unexpected AMFI prototype: %d", cdhash_param);
        break;
    }
    return false;
}

static void ck_kernel_pf_tc_patch(CkPfRange *range)
{
    uint8_t find[] = {
        0x00, 0x02, 0x80, 0x52, // mov w?, 0x16
        0x00, 0x00, 0x00, 0xD3, // lsr ?
        0x00, 0x00, 0x00, 0x9B, // madd ?
    };
    uint8_t mask[] = { 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
                       0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF };
    ck_pf_find_callback(range, "AMFI, all binaries in trustcache", find, mask,
                        sizeof(find), ck_kernel_pf_tc_callback);
}

static bool ck_kernel_pf_tc_ios16_callback(void *ctx, uint8_t *buffer)
{
    void *start = ck_pf_find_prev_insn(buffer, 100, PACIBSP, 0xFFFFFFFF);

    if (start == NULL) {
        return false;
    }

    stl_le_p(start, 0x52802020); // mov w0, 0x101
    stl_le_p(start + 4, RET);

    return true;
}

static void ck_kernel_pf_tc_ios16_patch(CkPfRange *range)
{
    uint8_t find[] = { 0xC0, 0xCF, 0x9D, 0xD2 }; // mov w?, 0xEE7E
    uint8_t mask[] = { 0xC0, 0xFF, 0xFF, 0xFF };
    ck_pf_find_callback(range, "AMFI, all binaries in trustcache (iOS 16)",
                        find, mask, sizeof(find),
                        ck_kernel_pf_tc_ios16_callback);
}

static bool ck_kernel_pf_amfi_sha1(void *ctx, uint8_t *buffer)
{
    void *cmp = ck_pf_find_next_insn(buffer, 0x10, 0x7100081F,
                                     0xFFFFFFFF); // cmp w0, 2

    if (cmp == NULL) {
        error_report("%s: failed to find cmp", __func__);
        return false;
    }

    stl_le_p(cmp, 0x6B00001F); // cmp w0, w0
    return true;
}

static void ck_kernel_pf_amfi_kext_patches(CkPfRange *range)
{
    uint8_t find[] = { 0x02, 0x00, 0xD0, 0x36 }; // tbz w2, 0x1A, ?
    uint8_t mask[] = { 0x1F, 0x00, 0xF8, 0xFF };
    ck_pf_find_callback(range, "allow SHA1 signatures in AMFI", find, mask,
                        sizeof(find), ck_kernel_pf_amfi_sha1);
}

static bool ck_kernel_pf_mac_mount_callback(void *ctx, uint8_t *buffer)
{
    void *mac_mount =
        ck_pf_find_prev_insn(buffer, 0x40, 0x37280000, 0xFFFE0000);
    if (mac_mount == NULL) {
        mac_mount = ck_pf_find_next_insn(buffer, 0x40, 0x37280000, 0xFFFE0000);
        if (mac_mount == NULL) {
            error_report("%s: failed to find nop point", __func__);
            return false;
        }
    }

    // Allow MNT_UNION mounts
    stl_le_p(mac_mount, NOP);

    // Search for ldrb w8, [x?, 0x71]
    mac_mount = ck_pf_find_prev_insn(buffer, 0x40, 0x3941C408, 0xFFFFFC1F);
    if (mac_mount == NULL) {
        mac_mount = ck_pf_find_next_insn(buffer, 0x40, 0x3941C408, 0xFFFFFC1F);
        if (mac_mount == NULL) {
            error_report("%s: failed to find xzr point", __func__);
            return false;
        }
    }

    // Replace with a mov x8, xzr
    // This will bypass the (vp->v_mount->mnt_flag & MNT_ROOTFS) check
    stl_le_p(mac_mount, 0xAA1F03E8);

    return true;
}

static void ck_kernel_pf_mac_mount_patch(CkPfRange *range)
{
    uint8_t find_old[] = { 0xE9, 0x2F, 0x1F, 0x32 }; // orr w9, wzr, 0x1FFE
    ck_pf_find_callback(range, "allow remounting rootfs, union mounts (old)",
                        find_old, NULL, sizeof(find_old),
                        ck_kernel_pf_mac_mount_callback);
    uint8_t find_new[] = { 0xC9, 0xFF, 0x83, 0x52 }; // movz w9, 0x1FFE
    ck_pf_find_callback(range, "allow remounting rootfs, union mounts (new)",
                        find_new, NULL, sizeof(find_new),
                        ck_kernel_pf_mac_mount_callback);
}

static void ck_kernel_pf_kprintf_patch(CkPfRange *range)
{
    uint8_t find[] = {
        0xAA, 0x43, 0x00, 0x91, // add x10, fp, #0x10
        0xEA, 0x07, 0x00, 0xF9, // str x10, [sp, #0x8]
        0x08, 0x00, 0x00, 0x2A, // orr w8, w?, w?
        0x08, 0x00, 0x00, 0x34, // cbz w8, #?
    };
    uint8_t mask[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                       0x1F, 0xFC, 0xE0, 0xFF, 0x1F, 0x00, 0x00, 0xFF };
    uint8_t replace[] = { 0xE8, 0x03, 0x1F, 0x2A };
    ck_pf_find_replace(range, "force enable kprintf", find, mask, sizeof(find),
                       replace, NULL, 8, sizeof(replace));
}

void ck_patch_kernel(MachoHeader64 *hdr)
{
    g_autofree CkPfRange *text_exec;
    g_autofree CkPfRange *ppltext_exec;
    MachoHeader64 *apfs_header;
    g_autofree CkPfRange *apfs_text_exec;
    MachoHeader64 *amfi_hdr;
    g_autofree CkPfRange *amfi_text_exec;

    apfs_header = ck_pf_find_image_header(hdr, "com.apple.filesystems.apfs");
    apfs_text_exec = ck_pf_find_section(apfs_header, "__TEXT_EXEC", "__text");
    ck_kernel_pf_apfs_patches(apfs_text_exec);

    amfi_hdr = ck_pf_find_image_header(
        hdr, "com.apple.driver.AppleMobileFileIntegrity");
    amfi_text_exec = ck_pf_find_section(amfi_hdr, "__TEXT_EXEC", "__text");
    ck_kernel_pf_amfi_kext_patches(amfi_text_exec);

    text_exec = ck_pf_get_kernel_text(hdr);
    ck_kernel_pf_tc_patch(text_exec);
    ck_kernel_pf_mac_mount_patch(text_exec);
    ck_kernel_pf_kprintf_patch(text_exec);

    ppltext_exec = ck_pf_find_section(hdr, "__PPLTEXT", "__text");
    if (ppltext_exec == NULL) {
        warn_report("Failed to find `__PPLTEXT.__text`.");
    } else {
        ck_kernel_pf_tc_patch(ppltext_exec);
        ck_kernel_pf_tc_ios16_patch(ppltext_exec);
    }
}
