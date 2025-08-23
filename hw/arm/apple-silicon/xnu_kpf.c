#include "hw/arm/apple-silicon/boot.h"
#include "hw/arm/apple-silicon/mem.h"
#include "hw/arm/apple-silicon/xnu_pf.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"

#define NOP (0xD503201F)
#define RET (0xD65F03C0)
#define RETAB (0xD65F0FFF)
#define PACIBSP (0xD503237F)

/// Precondition: insn must be masked.
static uint32_t *find_next_insn(uint32_t *buffer, uint32_t num, uint32_t insn,
                                uint32_t mask)
{
    g_assert_cmphex(insn & mask, ==, insn);

    for (uint32_t i = 0; i < num; ++i) {
        uint32_t *cur = buffer + i;
        if ((ldl_le_p(cur) & mask) == insn) {
            return cur;
        }
    }

    return NULL;
}

/// Precondition: insn must be masked.
static uint32_t *find_prev_insn(uint32_t *buffer, uint32_t num, uint32_t insn,
                                uint32_t mask)
{
    g_assert_cmphex(insn & mask, ==, insn);

    for (uint32_t i = 0; i < num; ++i) {
        uint32_t *cur = buffer - i;
        if ((ldl_le_p(cur) & mask) == insn) {
            return cur;
        }
    }

    return NULL;
}

static bool kpf_apfs_rootauth(ApplePfPatch *patch, uint32_t *opcode_stream)
{
    opcode_stream[0] = NOP;
    opcode_stream[1] = 0x52800000; // mov w0, 0

    info_report("%s: Found handle_eval_rootauth", __func__);
    return true;
}

static bool kpf_apfs_vfsop_mount(ApplePfPatch *patch, uint32_t *opcode_stream)
{
    opcode_stream[0] = 0x52800000; // mov w0, 0

    info_report("%s: Found apfs_vfsop_mount", __func__);
    return true;
}

static void kpf_apfs_patches(ApplePfPatchset *patchset)
{
    // Bypass root authentication
    uint64_t matches_root_auth[] = {
        0x37280068, // tbnz w8, 5, 0xC
        0X52800A00, // mov w0, 0x50
        0xD65F03C0, // ret
    };
    uint64_t masks_root_auth[] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };

    xnu_pf_maskmatch(patchset, "handle_eval_rootauth", matches_root_auth,
                     masks_root_auth, ARRAY_SIZE(masks_root_auth),
                     (void *)kpf_apfs_rootauth);

    // Allow mounting root as R/W
    uint64_t matches_root_rw[] = {
        0x37700000, // tbnz w0, 0xE, *
        0xB94003A0, // ldr x*, [x29/sp, *]
        0x121F7800, // and w*, w*, 0xFFFFFFFE
        0xB90003A0, // str x*, [x29/sp, *]
    };

    uint64_t masks_root_rw[] = {
        0xFFF8001F,
        0xFFFE03A0,
        0xFFFFFC00,
        0xFFC003A0,
    };

    xnu_pf_maskmatch(patchset, "apfs_vfsop_mount", matches_root_rw,
                     masks_root_rw, ARRAY_SIZE(masks_root_rw),
                     (void *)kpf_apfs_vfsop_mount);
}

static bool kpf_amfi_callback(ApplePfPatch *patch, uint32_t *opcode_stream)
{
    if (((opcode_stream[-1] & 0xFF000000) != 0x91000000) &&
        ((opcode_stream[-2] & 0xFF000000) != 0x91000000)) {
        return false;
    }

    uint32_t *ldrb = find_next_insn(opcode_stream, 256, 0x39402C00, 0xFFFFFC00);
    uint32_t cdhash_param = extract32(*ldrb, 5, 5);
    uint32_t *frame;
    uint32_t *start = opcode_stream;
    bool pac;

    frame = find_prev_insn(opcode_stream, 10, 0x910003FD, 0xFF8003FF);
    if (frame == NULL) {
        info_report("%s: Found AMFI (Leaf)", __func__);
    } else {
        info_report("%s: Found AMFI (Routine)", __func__);
        start = find_prev_insn(frame, 10, 0xA9A003E0, 0xFFE003E0);
        if (start == NULL) {
            start = find_prev_insn(frame, 10, 0xD10003FF, 0xFF8003FF);
            if (start == NULL) {
                error_report("%s: Failed to find AMFI start", __func__);
                return false;
            }
        }
    }

    pac = find_prev_insn(start, 5, PACIBSP, 0xFFFFFFFF) != NULL;
    switch (cdhash_param) {
    case 0: {
        // ADRP x8, *
        uint32_t *adrp = find_prev_insn(start, 10, 0x90000008, 0x9F00001F);
        if (adrp != NULL) {
            start = adrp;
        }
        info_report("%s: lookup_in_static_trust_cache @ 0x%" PRIx64, __func__,
                    ptov_static((hwaddr)start));
        *start++ = 0x52802020; // MOV W0, 0x101
        *start++ = (pac ? RETAB : RET);
        return true;
    }
    case 1:
        info_report("%s: lookup_in_trust_cache_module @ 0x%" PRIx64, __func__,
                    ptov_static((hwaddr)start));
        *start++ = 0x52800040; // mov w0, 2
        *start++ = 0x39000040; // strb w0, [x2]
        *start++ = 0x52800020; // mov w0, 1
        *start++ = 0x39000060; // strb w0, [x3]
        *start++ = 0x52800020; // mov w0, 1
        *start++ = (pac ? RETAB : RET);
        return true;
    default:
        error_report("Found unexpected AMFI prototype: %d", cdhash_param);
        break;
    }
    error_report("Failed to patch anything for AMFI");
    return false;
}

static void kpf_amfi_patch(ApplePfPatchset *patchset)
{
    // This patch leads to AMFI believing that everything is in trustcache
    uint64_t matches[] = {
        0x52800200, // mov w*, 0x16
        0xD3000000, // lsr *
        0x9B000000, // madd *
    };
    uint64_t masks[] = { 0xFFFFFF00, 0xFF000000, 0xFF000000 };
    xnu_pf_maskmatch(patchset, "amfi_patch", matches, masks,
                     ARRAY_SIZE(matches), (void *)kpf_amfi_callback);
}

static bool kpf_trustcache_callback(ApplePfPatch *patch,
                                    uint32_t *opcode_stream)
{
    uint32_t *start = find_prev_insn(opcode_stream, 100, PACIBSP, 0xFFFFFFFF);

    if (start == NULL) {
        return false;
    }

    info_report("%s: pmap_lookup_in_static_trust_cache_internal @ 0x%" PRIx64,
                __func__, ptov_static((hwaddr)start));
    *start++ = 0x52802020; // mov w0, 0x101
    *start++ = RET;

    return true;
}

static void kpf_trustcache_patch(ApplePfPatchset *patchset)
{
    uint64_t matches[] = { 0xD29DCFC0 }; // mov w*, 0xEE7E
    uint64_t masks[] = { 0xFFFFFFC0 };
    xnu_pf_maskmatch(patchset, "trustcache16", matches, masks,
                     ARRAY_SIZE(matches), (void *)kpf_trustcache_callback);
}

static bool kpf_amfi_sha1(ApplePfPatch *patch, uint32_t *opcode_stream)
{
    uint32_t *cmp = find_next_insn(opcode_stream, 0x10, 0x7100081F,
                                   0xFFFFFFFF); // cmp w0, 2

    if (cmp == NULL) {
        error_report("%s: failed to find cmp", __func__);
        return false;
    }

    info_report("Found AMFI hashtype check");
    xnu_pf_disable_patch(patch);
    *cmp = 0x6B00001F; // cmp w0, w0
    return true;
}

static void kpf_amfi_kext_patches(ApplePfPatchset *patchset)
{
    // Allow running binaries with SHA1 signatures
    uint64_t matches[] = { 0x36D00002 }; // tbz w2, 0x1A, *
    uint64_t masks[] = { 0xFFF8001F };
    xnu_pf_maskmatch(patchset, "amfi_sha1", matches, masks, ARRAY_SIZE(matches),
                     (void *)kpf_amfi_sha1);
}

static bool kpf_mac_mount_callback(ApplePfPatch *patch, uint32_t *opcode_stream)
{
    uint32_t *mac_mount = &opcode_stream[0];

    uint32_t *mac_mount_1 =
        find_prev_insn(mac_mount, 0x40, 0x37280000, 0xFFFE0000);
    if (mac_mount_1 == NULL) {
        mac_mount_1 = find_next_insn(mac_mount, 0x40, 0x37280000, 0xFFFE0000);
        if (mac_mount_1 == NULL) {
            error_report("%s: failed to find NOP point", __func__);
            return false;
        }
    }

    // Allow MNT_UNION mounts
    mac_mount_1[0] = NOP;

    // Search for ldrb w8, [x*, 0x71]
    mac_mount_1 = find_prev_insn(mac_mount, 0x40, 0x3941C408, 0xFFFFFC1F);
    if (mac_mount_1 == NULL) {
        mac_mount_1 = find_next_insn(mac_mount, 0x40, 0x3941C408, 0xFFFFFC1F);
        if (mac_mount_1 == NULL) {
            error_report("%s: failed to find xzr point", __func__);
            return false;
        }
    }

    // Replace with a mov x8, xzr
    // This will bypass the (vp->v_mount->mnt_flag & MNT_ROOTFS) check
    mac_mount_1[0] = 0xAA1F03E8;

    xnu_pf_disable_patch(patch);

    info_report("Found mac_mount");
    return true;
}

static void kpf_mac_mount_patch(ApplePfPatchset *patchset)
{
    // This patch will allow us to remount the rootfs and do UNION mounts.
    uint64_t matches[] = { 0x321F2FE9 }; // orr w9, wzr, 0x1FFE
    uint64_t masks[] = { 0xFFFFFFFF };
    xnu_pf_maskmatch(patchset, "mac_mount_patch1", matches, masks,
                     ARRAY_SIZE(matches), (void *)kpf_mac_mount_callback);
    matches[0] = 0x5283FFC9; // movz w9, 0x1FFE
    xnu_pf_maskmatch(patchset, "mac_mount_patch2", matches, masks,
                     ARRAY_SIZE(matches), (void *)kpf_mac_mount_callback);
}

void xnu_kpf(MachoHeader64 *hdr)
{
    ApplePfPatchset *text_exec_patchset;
    ApplePfRange *text_exec;
    ApplePfPatchset *ppltext_patchset;
    ApplePfRange *ppltext_exec;
    ApplePfPatchset *apfs_patchset;
    MachoHeader64 *apfs_header;
    ApplePfRange *apfs_text_exec;
    MachoHeader64 *amfi_hdr;
    ApplePfPatchset *amfi_patchset;
    ApplePfRange *amfi_text_exec;

    text_exec_patchset = xnu_pf_patchset_create(XNU_PF_ACCESS_32BIT);
    text_exec = xnu_pf_get_actual_text_exec(hdr);

    ppltext_patchset = xnu_pf_patchset_create(XNU_PF_ACCESS_32BIT);
    ppltext_exec = xnu_pf_section(hdr, "__PPLTEXT", "__text");

    apfs_patchset = xnu_pf_patchset_create(XNU_PF_ACCESS_32BIT);
    apfs_header = xnu_pf_get_kext_header(hdr, "com.apple.filesystems.apfs");
    apfs_text_exec = xnu_pf_section(apfs_header, "__TEXT_EXEC", "__text");

    kpf_apfs_patches(apfs_patchset);
    xnu_pf_apply(apfs_text_exec, apfs_patchset);
    xnu_pf_patchset_destroy(apfs_patchset);

    amfi_patchset = xnu_pf_patchset_create(XNU_PF_ACCESS_32BIT);
    amfi_hdr = xnu_pf_get_kext_header(
        hdr, "com.apple.driver.AppleMobileFileIntegrity");
    amfi_text_exec = xnu_pf_section(amfi_hdr, "__TEXT_EXEC", "__text");
    kpf_amfi_kext_patches(amfi_patchset);
    xnu_pf_apply(amfi_text_exec, amfi_patchset);
    xnu_pf_patchset_destroy(amfi_patchset);

    kpf_amfi_patch(text_exec_patchset);
    kpf_mac_mount_patch(text_exec_patchset);
    xnu_pf_apply(text_exec, text_exec_patchset);
    xnu_pf_patchset_destroy(text_exec_patchset);

    kpf_amfi_patch(ppltext_patchset);
    kpf_trustcache_patch(ppltext_patchset);
    if (ppltext_exec == NULL) {
        warn_report("Failed to find `__PPLTEXT`.");
    } else {
        xnu_pf_apply(ppltext_exec, ppltext_patchset);
    }
    xnu_pf_patchset_destroy(ppltext_patchset);

    g_free(text_exec);
    g_free(ppltext_exec);
    g_free(apfs_text_exec);
    g_free(amfi_text_exec);
}
