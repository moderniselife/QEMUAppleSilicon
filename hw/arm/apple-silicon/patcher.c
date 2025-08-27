/*
 * ChefKiss Patcher (PenguinWizardryC).
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

#include "hw/arm/apple-silicon/patcher.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"

void ck_patcher_find_callback_ctx(CKPatcherRange *range, const char *name,
                                  const uint8_t *find, const uint8_t *mask,
                                  size_t len, size_t align, void *ctx,
                                  CKPatcherCallback callback)
{
    size_t i;
    size_t match_i;
    uint8_t *match;
    bool found;

    if (align == 0) {
        align = 1;
    } else {
        g_assert_cmpuint(len % align, ==, 0);
    }

    if (mask == NULL) {
        match = memmem(range->ptr, range->length, find, len);
        if (match == NULL || !callback(ctx, match)) {
            error_report("`%s` patch did not apply in `%s`!", name,
                         range->name);
        } else {
            info_report("`%s` patch applied in `%s`!", name, range->name);
        }
    } else {
        for (i = 0; i < len; ++i) {
            g_assert_cmphex(find[i] & mask[i], ==, find[i]);
        }

        for (i = 0; i < range->length; i += align) {
            found = true;
            match = range->ptr + i;
            for (match_i = 0; match_i < len; ++match_i) {
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

void ck_patcher_find_callback(CKPatcherRange *range, const char *name,
                              const uint8_t *find, const uint8_t *mask,
                              size_t len, size_t align,
                              CKPatcherCallback callback)
{
    ck_patcher_find_callback_ctx(range, name, find, mask, len, align, NULL,
                                 callback);
}

typedef struct {
    const uint8_t *replace;
    const uint8_t *mask;
    size_t offset;
    size_t len;
} CKPatcherFindReplaceContext;

static bool ck_patcher_find_replace_callback(void *ctx, uint8_t *buffer)
{
    CKPatcherFindReplaceContext *repl_ctx;
    size_t i;

    repl_ctx = ctx;
    if (repl_ctx->mask == NULL) {
        memcpy(buffer + repl_ctx->offset, repl_ctx->replace, repl_ctx->len);
    } else {
        for (i = 0; i < repl_ctx->len; ++i) {
            buffer[repl_ctx->offset + i] =
                (buffer[repl_ctx->offset + i] & repl_ctx->mask[i]) |
                repl_ctx->replace[i];
        }
    }
    return true;
}

void ck_patcher_find_replace(CKPatcherRange *range, const char *name,
                             const uint8_t *find, const uint8_t *mask,
                             size_t len, size_t align, const uint8_t *replace,
                             const uint8_t *replace_mask, size_t replace_off,
                             size_t replace_len)
{
    CKPatcherFindReplaceContext ctx;

    g_assert_cmphex(replace_off + replace_len, <=, len);

    ctx.replace = replace;
    ctx.mask = replace_mask;
    ctx.offset = replace_off;
    ctx.len = replace_len;

    ck_patcher_find_callback_ctx(range, name, find, mask, len, align, &ctx,
                                 ck_patcher_find_replace_callback);
}

void *ck_patcher_find_next_insn(void *buffer, uint32_t num, uint32_t insn,
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

void *ck_patcher_find_prev_insn(void *buffer, uint32_t num, uint32_t insn,
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
