/*
 * ChefKiss Patch Finder (PenguinWizardryC).
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

#include "hw/arm/apple-silicon/pf.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include <string.h>

CkPfRange *ck_pf_range_from_xnu_va(const char *name, hwaddr base, hwaddr size)
{
    CkPfRange *range = g_new0(CkPfRange, 1);
    range->addr = base;
    range->length = size;
    range->ptr = xnu_va_to_ptr(base);
    range->name = name;
    return range;
}

CkPfRange *ck_pf_find_segment(MachoHeader64 *header, const char *segment_name)
{
    MachoSegmentCommand64 *seg;

    seg = macho_get_segment(header, segment_name);
    return seg == NULL ? NULL :
                         ck_pf_range_from_xnu_va(segment_name, seg->vmaddr,
                                                 seg->filesize);
}

CkPfRange *ck_pf_find_section(MachoHeader64 *header, const char *segment_name,
                              const char *section_name)
{
    MachoSection64 *sec;
    MachoSegmentCommand64 *seg;

    seg = macho_get_segment(header, segment_name);
    if (seg == NULL) {
        return NULL;
    }

    sec = macho_get_section(seg, section_name);
    return sec == NULL ?
               NULL :
               ck_pf_range_from_xnu_va(segment_name, sec->addr, sec->size);
}

// TODO: Fix host endianness BE vs LE troubles in here.
MachoHeader64 *ck_pf_find_image_header(MachoHeader64 *kheader,
                                       const char *kext_bundle_id)
{
    uint64_t *info, *start;
    uint32_t count;
    uint32_t i;
    char kname[256];
    const char *prelinkinfo, *last_dict;

    if (kheader->file_type == MH_FILESET) {
        return macho_get_fileset_header(kheader, kext_bundle_id);
    }

    g_autofree CkPfRange *kmod_info_range =
        ck_pf_find_section(kheader, "__PRELINK_INFO", "__kmod_info");
    if (kmod_info_range == NULL) {
        g_autofree CkPfRange *kext_info_range =
            ck_pf_find_section(kheader, "__PRELINK_INFO", "__info");
        if (kext_info_range == NULL) {
            error_report("Unsupported XNU.");
            return NULL;
        }

        prelinkinfo =
            strstr((const char *)kext_info_range->ptr, "PrelinkInfoDictionary");
        last_dict = strstr(prelinkinfo, "<array>") + 7;
        while (last_dict) {
            const char *nested_dict, *ident;
            const char *end_dict = strstr(last_dict, "</dict>");
            if (!end_dict) {
                break;
            }

            nested_dict = strstr(last_dict + 1, "<dict>");
            while (nested_dict) {
                if (nested_dict > end_dict) {
                    break;
                }

                nested_dict = strstr(nested_dict + 1, "<dict>");
                end_dict = strstr(end_dict + 1, "</dict>");
            }

            ident = g_strstr_len(last_dict, end_dict - last_dict,
                                 "CFBundleIdentifier");
            if (ident != NULL) {
                const char *value = strstr(ident, "<string>");
                if (value != NULL) {
                    const char *value_end;

                    value += strlen("<string>");
                    value_end = strstr(value, "</string>");
                    if (value_end != NULL) {
                        memcpy(kname, value, value_end - value);
                        kname[value_end - value] = 0;
                        if (strcmp(kname, kext_bundle_id) == 0) {
                            const char *addr =
                                g_strstr_len(last_dict, end_dict - last_dict,
                                             "_PrelinkExecutableLoadAddr");
                            if (addr != NULL) {
                                const char *avalue = strstr(addr, "<integer");
                                if (avalue != NULL) {
                                    avalue = strstr(avalue, ">");
                                    if (avalue != NULL) {
                                        return xnu_va_to_ptr(
                                            strtoull(++avalue, 0, 0));
                                    }
                                }
                            }
                        }
                    }
                }
            }

            last_dict = strstr(end_dict, "<dict>");
        }

        return NULL;
    }
    g_autofree CkPfRange *kmod_start_range =
        ck_pf_find_section(kheader, "__PRELINK_INFO", "__kmod_start");
    if (kmod_start_range != NULL) {
        info = (uint64_t *)kmod_info_range->ptr;
        start = (uint64_t *)kmod_start_range->ptr;
        count = kmod_info_range->length / 8;
        for (i = 0; i < count; i++) {
            const char *kext_name = (const char *)xnu_va_to_ptr(info[i]) + 0x10;
            if (strcmp(kext_name, kext_bundle_id) == 0) {
                return xnu_va_to_ptr(start[i]);
            }
        }
    }

    return NULL;
}

CkPfRange *ck_pf_get_kernel_text(MachoHeader64 *header)
{
    if (header->file_type == MH_FILESET) {
        MachoHeader64 *kernel =
            ck_pf_find_image_header(header, "com.apple.kernel");
        return kernel == NULL ?
                   NULL :
                   ck_pf_find_section(kernel, "__TEXT_EXEC", "__text");
    }

    return ck_pf_find_section(header, "__TEXT_EXEC", "__text");
}

static void ck_pf_find_callback_ctx(CkPfRange *range, const char *name,
                                    const uint8_t *find, const uint8_t *mask,
                                    size_t count, void *ctx,
                                    CkPfCallback callback)
{
    size_t i;
    size_t match_i;
    uint8_t *match;
    bool found;

    if (mask == NULL) {
        match = memmem(range->ptr, range->length, find, count);
        if (match == NULL || !callback(ctx, match)) {
            error_report("`%s` patch did not apply in `%s`!", name,
                         range->name);
        } else {
            info_report("`%s` patch applied in `%s`!", name, range->name);
        }
    } else {
        for (i = 0; i < count; i++) {
            g_assert_cmphex(find[i] & mask[i], ==, find[i]);
        }

        for (i = 0; i < range->length; ++i) {
            found = true;
            match = range->ptr + i;
            for (match_i = 0; match_i < count; ++match_i) {
                if ((match[match_i] & mask[match_i]) != find[match_i]) {
                    found = false;
                    break;
                }
            }
            if (found && callback(ctx, range->ptr + i)) {
                info_report("`%s` patch applied in `%s`!", name, range->name);
                return;
            }
        }
        error_report("`%s` patch did not apply in `%s`!", name, range->name);
    }
}

void ck_pf_find_callback(CkPfRange *range, const char *name,
                         const uint8_t *find, const uint8_t *mask, size_t count,
                         CkPfCallback callback)
{
    ck_pf_find_callback_ctx(range, name, find, mask, count, NULL, callback);
}

typedef struct {
    const uint8_t *replace;
    const uint8_t *mask;
    size_t offset;
    size_t count;
} CkPfFindReplaceCtx;

static bool ck_pf_find_replace_callback(void *ctx, uint8_t *buffer)
{
    CkPfFindReplaceCtx *repl_ctx;
    size_t i;

    repl_ctx = ctx;
    if (repl_ctx->mask == NULL) {
        memcpy(buffer + repl_ctx->offset, repl_ctx->replace, repl_ctx->count);
    } else {
        for (i = 0; i < repl_ctx->count; ++i) {
            buffer[repl_ctx->offset + i] =
                (buffer[repl_ctx->offset + i] & repl_ctx->mask[i]) |
                repl_ctx->replace[i];
        }
    }
    return true;
}

void ck_pf_find_replace(CkPfRange *range, const char *name, const uint8_t *find,
                        const uint8_t *mask, size_t count,
                        const uint8_t *replace, const uint8_t *replace_mask,
                        size_t replace_off, size_t replace_count)
{
    CkPfFindReplaceCtx ctx;

    g_assert_cmphex(replace_off + replace_count, <=, count);

    ctx.replace = replace;
    ctx.mask = replace_mask;
    ctx.offset = replace_off;
    ctx.count = replace_count;

    ck_pf_find_callback_ctx(range, name, find, mask, count, &ctx,
                            ck_pf_find_replace_callback);
}

void *ck_pf_find_next_insn(void *buffer, uint32_t num, uint32_t insn,
                           uint32_t mask, uint32_t skip)
{
    g_assert_cmphex(insn & mask, ==, insn);

    for (uint32_t i = 0; i < num; ++i) {
        uint8_t *cur = buffer + i * sizeof(uint32_t);
        if ((ldl_le_p(cur) & mask) == insn) {
            if (skip == 0) {
                return cur;
            } else {
                --skip;
            }
        }
    }

    return NULL;
}

void *ck_pf_find_prev_insn(void *buffer, uint32_t num, uint32_t insn,
                           uint32_t mask, uint32_t skip)
{
    g_assert_cmphex(insn & mask, ==, insn);

    for (uint32_t i = 0; i < num; ++i) {
        void *cur = buffer - (i * sizeof(uint32_t));
        if ((ldl_le_p(cur) & mask) == insn) {
            if (skip == 0) {
                return cur;
            } else {
                --skip;
            }
        }
    }

    return NULL;
}
