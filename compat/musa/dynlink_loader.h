/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

// musify: MUSA-parallel of compat/musa/dynlink_loader.h.
// FFmpeg's CUDA path defines FFNV_LOAD_FUNC/FFNV_SYM_FUNC + logging macros, then
// pulls in the <ffmtcodec/dynlink_loader.h> wrapper. The MUSA path is identical
// in shape: the installed `ffmtcodec` wrapper package provides a
// <ffmtcodec/dynlink_loader.h> with the same FFNV_* macro contract and the same
// MusaFunctions / MuvidFunctions / MtencFunctions dynload structs, but populates
// them with MUSA symbols (mu*, muvid*, MtEncodeAPICreateInstance) and loads the
// MUSA video libraries (libmusa.so.1, libvideo_musa.so, libencode_musa.so).
// Mapping evidence: general.json `ffmtcodec` -> `ffmtcodec`; wrapper package
// installed at /usr/local/include/ffmtcodec/ (ffmtcodec.pc version 13.0.19.0).
// See MUSIFY.md `## Knowledge Base` [Round 1] + CODE_ANALYSIS.md §2 Multimedia
// MT Video Wiki Query Contract.

#ifndef COMPAT_MUSA_DYNLINK_LOADER_H
#define COMPAT_MUSA_DYNLINK_LOADER_H

#include "libavutil/log.h"
#include "compat/w32dlfcn.h"

#define FFNV_LOAD_FUNC(path) dlopen((path), RTLD_LAZY)
#define FFNV_SYM_FUNC(lib, sym) dlsym((lib), (sym))
#define FFNV_FREE_FUNC(lib) dlclose(lib)
#define FFNV_LOG_FUNC(logctx, msg, ...) av_log(logctx, AV_LOG_ERROR, msg,  __VA_ARGS__)
#define FFNV_DEBUG_LOG_FUNC(logctx, msg, ...) av_log(logctx, AV_LOG_DEBUG, msg, __VA_ARGS__)

/* musify: ffmtcodec/dynlink_loader.h includes dynlink_musa.h but still gates
 * MusaFunctions' concrete struct on the legacy FFNV_DYNLINK_CUDA_H macro.
 * Define that compatibility guard before including the authoritative ffmtcodec
 * loader so MUSA mu* function pointers are visible instead of an incomplete
 * typedef. */
#ifndef FFNV_DYNLINK_CUDA_H
#define FFNV_DYNLINK_CUDA_H
#endif
#ifndef musa_free_functions
#define musa_free_functions musa_free_functions
#endif

#include <ffmtcodec/dynlink_loader.h>

#endif /* COMPAT_MUSA_DYNLINK_LOADER_H */
