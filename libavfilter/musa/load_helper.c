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

#include "config.h"

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_musa_internal.h"
#include "libavutil/mem.h"
#include "libavutil/musa_check.h"

#if CONFIG_SHADER_COMPRESSION
#include "libavutil/zlib_utils.h"
#endif

#include "load_helper.h"

#define CHECK_MU(x) FF_MUSA_CHECK_DL(avctx, mu, x)

int ff_musa_load_module(void *avctx, AVMUSADeviceContext *hwctx, MUmodule *mu_module,
                        const unsigned char *data, const unsigned int length)
{
    MusaFunctions *mu = hwctx->internal->cuda_dl;

    return CHECK_MU(mu->muModuleLoadData(mu_module, data));
}
