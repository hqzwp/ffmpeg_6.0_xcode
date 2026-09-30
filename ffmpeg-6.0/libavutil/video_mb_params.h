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

#ifndef AVUTIL_VIDEO_MB_PARAMS_H
#define AVUTIL_VIDEO_MB_PARAMS_H

#include <stddef.h>
#include <stdint.h>

#include "libavutil/frame.h"

/**
 * Macroblock type bits. Values match the decoder's internal MB_TYPE_* flags.
 * A block may have more than one bit set.
 */
#define AV_MB_TYPE_INTRA4x4   (1u <<  0)
#define AV_MB_TYPE_INTRA16x16 (1u <<  1)
#define AV_MB_TYPE_INTRA_PCM  (1u <<  2)
#define AV_MB_TYPE_16x16      (1u <<  3)
#define AV_MB_TYPE_16x8       (1u <<  4)
#define AV_MB_TYPE_8x16       (1u <<  5)
#define AV_MB_TYPE_8x8        (1u <<  6)
#define AV_MB_TYPE_INTERLACED (1u <<  7)
#define AV_MB_TYPE_DIRECT2    (1u <<  8)
#define AV_MB_TYPE_ACPRED     (1u <<  9)
#define AV_MB_TYPE_GMC        (1u << 10)
#define AV_MB_TYPE_SKIP       (1u << 11)
#define AV_MB_TYPE_P0L0       (1u << 12)
#define AV_MB_TYPE_P1L0       (1u << 13)
#define AV_MB_TYPE_P0L1       (1u << 14)
#define AV_MB_TYPE_P1L1       (1u << 15)
#define AV_MB_TYPE_L0         (AV_MB_TYPE_P0L0 | AV_MB_TYPE_P1L0)
#define AV_MB_TYPE_L1         (AV_MB_TYPE_P0L1 | AV_MB_TYPE_P1L1)
#define AV_MB_TYPE_L0L1       (AV_MB_TYPE_L0 | AV_MB_TYPE_L1)
#define AV_MB_TYPE_QUANT      (1u << 16)
#define AV_MB_TYPE_CBP        (1u << 17)
#define AV_MB_TYPE_INTRA      (AV_MB_TYPE_INTRA4x4 | AV_MB_TYPE_INTRA16x16 | AV_MB_TYPE_INTRA_PCM)

/**
 * H.264 macroblock side data, stored as AV_FRAME_DATA_VIDEO_MB_INFO.
 *
 * Layout of AVFrameSideData.data:
 *   AVVideoMBParams
 *   uint32_t mb_type[mb_height * mb_stride]
 *   int8_t   ref_index[2][4 * mb_height * mb_stride]
 *
 * mb_type index is mb_x + mb_y * mb_stride.
 * ref_index for one macroblock starts at 4 * (mb_x + mb_y * mb_stride).
 * The four values are a 2x2 block stored with stride 2:
 *   [0] top-left, [1] top-right, [2] bottom-left, [3] bottom-right.
 * List 0 then list 1. Unused references are negative.
 */
typedef struct AVVideoMBParams {
    uint32_t mb_width;
    uint32_t mb_height;
    uint32_t mb_stride;
    uint32_t reserved;
} AVVideoMBParams;

static inline uint32_t *av_video_mb_type(AVVideoMBParams *par)
{
    return (uint32_t *)((uint8_t *)par + sizeof(AVVideoMBParams));
}

static inline int8_t *av_video_mb_ref_index(AVVideoMBParams *par, int list)
{
    const size_t count = (size_t)par->mb_height * par->mb_stride;
    uint8_t *base = (uint8_t *)par + sizeof(AVVideoMBParams) + count * sizeof(uint32_t);
    return (int8_t *)base + (list ? 1 : 0) * 4 * count;
}

#endif /* AVUTIL_VIDEO_MB_PARAMS_H */
