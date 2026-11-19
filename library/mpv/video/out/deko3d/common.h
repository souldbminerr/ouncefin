/*
 * Copyright (c) 2024 averne <averne381@gmail.com>
 *
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <deko3d.h>

#ifdef __SWITCH2__
/* shaderc-backed GLSL->SPIR-V compiler (video/out/gpu/spirv.h); feeds
 * uam2rt for runtime DKSH compilation. */
struct spirv_compiler;
#endif

#define RA_DK_NUM_CMDBUFS 3
#define RA_DK_CMDBUF_SIZE MP_ALIGN_UP(0x10000, DK_MEMBLOCK_ALIGNMENT)

typedef struct {
    DkDevice device;
    DkQueue queue;

    DkCmdBuf cmdbuf;
    DkMemBlock cmdbuf_memblock;
    int cur_cmdbuf_slice;
    DkFence cmdbuf_fences[RA_DK_NUM_CMDBUFS];

#ifdef __SWITCH2__
    struct spirv_compiler *spirv;
#endif
} mp_dk_ctx;
