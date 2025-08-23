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

#ifndef HW_ARM_APPLE_SILICON_PF_H
#define HW_ARM_APPLE_SILICON_PF_H

#include "hw/arm/apple-silicon/boot.h"

typedef struct {
    hwaddr base;
    hwaddr size;
    void *ptr;
} CkPfRange;

CkPfRange *ck_pf_range_from_xnu_va(hwaddr base, hwaddr size);

CkPfRange *ck_pf_find_segment(MachoHeader64 *hdr, const char *name);
CkPfRange *ck_pf_find_section(MachoHeader64 *hdr, const char *segment,
                              const char *section);

CkPfRange *ck_pf_get_kernel_text(MachoHeader64 *hdr);
MachoHeader64 *ck_pf_get_image_header(MachoHeader64 *hdr,
                                      const char *bundle_id);

/// Precondition: `insn` must be masked.
void *ck_pf_find_next_insn(void *buffer, uint32_t num, uint32_t insn,
                           uint32_t mask);
/// Precondition: `insn` must be masked.
void *ck_pf_find_prev_insn(void *buffer, uint32_t num, uint32_t insn,
                           uint32_t mask);

typedef bool (*CkPfCallback)(void *ctx, uint8_t *buffer);
void ck_pf_find_callback(CkPfRange *range, const char *name,
                         const uint8_t *find, const uint8_t *mask, size_t count,
                         CkPfCallback callback);
/// Precondition: bytes in `find` must be masked.
void ck_pf_find_replace(CkPfRange *range, const char *name, const uint8_t *find,
                        const uint8_t *mask, size_t count,
                        const uint8_t *replace, const uint8_t *replace_mask,
                        size_t replace_off, size_t replace_count);

void ck_patch_kernel(MachoHeader64 *hdr);
#endif
