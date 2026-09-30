/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * Context Adaptive Binary Arithmetic Coder.
 */

#ifndef AVCODEC_CABAC_H
#define AVCODEC_CABAC_H

#include <stdint.h>

extern const uint8_t ff_h264_cabac_tables[512 + 4*2*64 + 4*64 + 63];
#define H264_NORM_SHIFT_OFFSET 0
#define H264_LPS_RANGE_OFFSET 512
#define H264_MLPS_STATE_OFFSET 1024
#define H264_LAST_COEFF_FLAG_OFFSET_8x8_OFFSET 1280

#define CABAC_BITS 16
#define CABAC_MASK ((1<<CABAC_BITS)-1)


/*
 * CABAC（Context Adaptive Binary Arithmetic Coding）
 * 上下文自适应二进制算术编码，H.264/AVC slice 在 entropy_coding_mode_flag=1 时使用。
 *
 * 1) 解的是什么
 *    Slice 数据是一串按标准顺序读出的 bin（0/1）：如 mb_skip、mb_type、ref、
 *    coeff_significant 等。每个 bin 单独做一次算术解码，不是整段 Huffman 码字。
 *
 * 2) Context（上下文）
 *    同一类语法在不同邻域条件下概率不同。解码器用左/上已解 MB 等算 ctx 索引，
 *    再查 sl->cabac_state[ctx]（见 ff_h264_init_cabac_states）。不同 ctx 互不影响。
 *
 * 3) 一个 bin 怎么解（见 cabac_functions.h 的 get_cabac_inline）
 *    - range：当前区间宽度；low：已读比特在区间内的位置（与 range 同尺度）。
 *    - 用 state 查 LPS 子区间宽度，把 range 切成 MPS/LPS 两段。
 *    - 比较 low 落在哪段 → 得到本 bin 的 0/1 → 缩 range、更新 state，必要时 refill。
 *    编码端做逆过程；整段 slice 的 bin 序列由 low 在逐步划分中被唯一还原。
 *
 * 4) 与本结构体、工程入口
 *    ff_init_cabac_decoder() 初始化 low/range 并绑定 slice 字节流；
 *    H.264 各语法元素的 get_cabac 调用见 h264_cabac.c（如 decode_cabac_mb_skip）。
 
 
 MPS   Most Probable Symbol
 LPS   Least Probable Symbol
 0-1
          MPS                    LPS
 |_________0_____________|_______1______|
 
 */
typedef struct CABACContext{
    int low;   /* 算术解码区间下界（含已注入的 bytestream 比特） */
    int range; /* 当前区间宽度，解码每步后缩小或 renormalize */
    const uint8_t *bytestream_start; /* slice RBSP 起始 */
    const uint8_t *bytestream;       /* 下一次 refill 读指针 */
    const uint8_t *bytestream_end;
} CABACContext;

int ff_init_cabac_decoder(CABACContext *c, const uint8_t *buf, int buf_size);

#endif /* AVCODEC_CABAC_H */
