/*
 * Copyright (c) 2003-2010 Michael Niedermayer <michaelni@gmx.at>
 *
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

/**
 * @file
 * H.264 DSP functions.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef AVCODEC_H264DSP_H
#define AVCODEC_H264DSP_H

#include <stdint.h>
#include <stddef.h>

typedef void (*h264_weight_func)(uint8_t *block, ptrdiff_t stride, int height,
                                 int log2_denom, int weight, int offset);
typedef void (*h264_biweight_func)(uint8_t *dst, uint8_t *src,
                                   ptrdiff_t stride, int height, int log2_denom,
                                   int weightd, int weights, int offset);

/**
 * Context for storing H.264 DSP functions   Digital Signal Processing
 *
 * 把解码/处理里那些 又重、又固定的底层运算（滤波、插值、找起始码、IDCT 等）抽成函数表，可以换成 C / SIMD / 汇编实现，逻辑代码只调函数指针。
 */
typedef struct H264DSPContext {
    /* weighted MC */
    h264_weight_func weight_h264_pixels_tab[4];
    h264_biweight_func biweight_h264_pixels_tab[4];

    /* loop filter */
    void (*h264_v_loop_filter_luma)(uint8_t *pix /*align 16*/, ptrdiff_t stride,
                                    int alpha, int beta, int8_t *tc0);
    void (*h264_h_loop_filter_luma)(uint8_t *pix /*align 4 */, ptrdiff_t stride,
                                    int alpha, int beta, int8_t *tc0);
    void (*h264_h_loop_filter_luma_mbaff)(uint8_t *pix /*align 16*/, ptrdiff_t stride,
                                          int alpha, int beta, int8_t *tc0);
    /* v/h_loop_filter_luma_intra: align 16 */
    void (*h264_v_loop_filter_luma_intra)(uint8_t *pix, ptrdiff_t stride,
                                          int alpha, int beta);
    void (*h264_h_loop_filter_luma_intra)(uint8_t *pix, ptrdiff_t stride,
                                          int alpha, int beta);
    void (*h264_h_loop_filter_luma_mbaff_intra)(uint8_t *pix /*align 16*/,
                                                ptrdiff_t stride, int alpha, int beta);
    void (*h264_v_loop_filter_chroma)(uint8_t *pix /*align 8*/, ptrdiff_t stride,
                                      int alpha, int beta, int8_t *tc0);
    void (*h264_h_loop_filter_chroma)(uint8_t *pix /*align 4*/, ptrdiff_t stride,
                                      int alpha, int beta, int8_t *tc0);
    void (*h264_h_loop_filter_chroma_mbaff)(uint8_t *pix /*align 8*/,
                                            ptrdiff_t stride, int alpha, int beta,
                                            int8_t *tc0);
    void (*h264_v_loop_filter_chroma_intra)(uint8_t *pix /*align 8*/,
                                            ptrdiff_t stride, int alpha, int beta);
    void (*h264_h_loop_filter_chroma_intra)(uint8_t *pix /*align 8*/,
                                            ptrdiff_t stride, int alpha, int beta);
    void (*h264_h_loop_filter_chroma_mbaff_intra)(uint8_t *pix /*align 8*/,
                                                  ptrdiff_t stride, int alpha, int beta);
    // h264_loop_filter_strength: simd only. the C version is inlined in h264_loopfilter.c
    void (*h264_loop_filter_strength)(int16_t bS[2][4][4], uint8_t nnz[40],
                                      int8_t ref[2][40], int16_t mv[2][40][2],
                                      int bidir, int edges, int step,
                                      int mask_mv0, int mask_mv1, int field);

    /* IDCT */
    //处理普通 4×4，用块里的 16 个系数做反变换，得到 4×4 的残差，加到 dst 上。
    void (*h264_idct_add)(uint8_t *dst /*align 4*/,
                          int16_t *block /*align 16*/, int stride);
    //处理 8×8，用这一块的 64 个系数做反变换，得到 64 个残差 加到 8×8 的预测像素上。
    void (*h264_idct8_add)(uint8_t *dst /*align 8*/,
                           int16_t *block /*align 16*/, int stride);
    //4×4左上角的DC不为0且其余为0 
    void (*h264_idct_dc_add)(uint8_t *dst /*align 4*/,
                             int16_t *block /*align 16*/, int stride);
    //8×8左上角的DC不为0且其余为0 
    void (*h264_idct8_dc_add)(uint8_t *dst /*align 8*/,
                              int16_t *block /*align 16*/, int stride);
    //处理整个宏块的 16 个亮度 4×4。它自己不做变换，而是逐块查看非零个数，再决定调用谁：只有左上角一个非零系数时调用 h264_idct_dc_add，否则调用 h264_idct_add。
    void (*h264_idct_add16)(uint8_t *dst /*align 16*/, const int *blockoffset,
                            int16_t *block /*align 16*/, int stride,
                            const uint8_t nnzc[5 * 8]);
    //处理一个宏块里的 4 个亮度 8×8。宏块用了 8×8 变换
    void (*h264_idct8_add4)(uint8_t *dst /*align 16*/, const int *blockoffset,
                            int16_t *block /*align 16*/, int stride,
                            const uint8_t nnzc[5 * 8]);
    //h264_idct_add8 做的是色度 4×4 的反变换。4:2:0 时 Cb、Cr 各 4 块，一共 8 块。dst[0] 是 Cb 的像素，dst[1] 是 Cr 的像素。
    void (*h264_idct_add8)(uint8_t **dst /*align 16*/, const int *blockoffset,
                           int16_t *block /*align 16*/, int stride,
                           const uint8_t nnzc[15 * 8]);
    /*
    这 16 个 4×4 的左上角直流被抽出来，单独组成一块 16 个直流，做哈达玛变换。每个 4×4 剩下 15 个交流系数。没有 8×8 变换，也没有运动矢量。
    h264_idct_add16intra 处理的就是这 16 个 4×4。调用前，16 个直流已经反变换并填回各自的左上角。块里若还有交流，16 个系数一起反变换；若只剩左上角那个直流，就只加这一个数
    */
    void (*h264_idct_add16intra)(uint8_t *dst /*align 16*/, const int *blockoffset,
                                 int16_t *block /*align 16*/,
                                 int stride, const uint8_t nnzc[5 * 8]);
    
    //用于 Intra16 亮度。input 是 mb_luma_dc 里的 16 个直流，
    void (*h264_luma_dc_dequant_idct)(int16_t *output,
                                      int16_t *input /*align 16*/, int qmul);
    //色度DC系数反量化 反变换 它的输入和输出是同一块内存
    void (*h264_chroma_dc_dequant_idct)(int16_t *block, int qmul);

    /* bypass-transform
     直接预测加残差 dst[i] += block[i]
     */
    void (*h264_add_pixels8_clear)(uint8_t *dst, int16_t *block, int stride);
    void (*h264_add_pixels4_clear)(uint8_t *dst, int16_t *block, int stride);

    /**
     * Search buf from the start for up to size bytes. Return the index
     * of a zero byte, or >= size if not found. Ideally, use lookahead
     * to filter out any zero bytes that are known to not be followed by
     * one or more further zero bytes and a one byte. Better still, filter
     * out any bytes that form the trailing_zero_8bits syntax element too.
     */
    int (*startcode_find_candidate)(const uint8_t *buf, int size);
} H264DSPContext;

void ff_h264dsp_init(H264DSPContext *c, const int bit_depth,
                     const int chroma_format_idc);
void ff_h264dsp_init_aarch64(H264DSPContext *c, const int bit_depth,
                             const int chroma_format_idc);
void ff_h264dsp_init_arm(H264DSPContext *c, const int bit_depth,
                         const int chroma_format_idc);
void ff_h264dsp_init_ppc(H264DSPContext *c, const int bit_depth,
                         const int chroma_format_idc);
void ff_h264dsp_init_x86(H264DSPContext *c, const int bit_depth,
                         const int chroma_format_idc);
void ff_h264dsp_init_mips(H264DSPContext *c, const int bit_depth,
                          const int chroma_format_idc);
void ff_h264dsp_init_loongarch(H264DSPContext *c, const int bit_depth,
                               const int chroma_format_idc);

#endif /* AVCODEC_H264DSP_H */
