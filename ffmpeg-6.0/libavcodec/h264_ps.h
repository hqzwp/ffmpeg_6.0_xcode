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

/**
 * @file
 * H.264 parameter set handling
 */

#ifndef AVCODEC_H264_PS_H
#define AVCODEC_H264_PS_H

#include <stdint.h>

#include "libavutil/buffer.h"
#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"

#include "avcodec.h"
#include "get_bits.h"
#include "h264.h"
#include "h2645_vui.h"

#define MAX_SPS_COUNT          32
#define MAX_PPS_COUNT         256
#define MAX_LOG2_MAX_FRAME_NUM    (12 + 4)

/**
 * Sequence parameter set
 */
typedef struct SPS {
    unsigned int sps_id;
    //编码档次：66=Baseline, 77=Main, 100=High, 110=High10...
    int profile_idc;
    //编码档次：66=Baseline, 77=Main, 100=High, 110=High10...
    int level_idc;
    //色度采样：0=灰度, 1=420, 2=422, 3=444
    int chroma_format_idc;
    //是否绕过变换/量化（极少见）
    int transform_bypass;              ///< qpprime_y_zero_transform_bypass_flag
    //slice header 里 frame_num 的比特数，范围 4~16
    int log2_max_frame_num;            ///< log2_max_frame_num_minus4 + 4
    //POC 算法类型：0 最常见，1 查表，2 等于 frame_num  作用就是帮助解码器确定图片的显示顺序。
    int poc_type;                      ///< pic_order_cnt_type
    //poc_type==0 时，slice 里 pic_order_cnt_lsb 的比特数
    int log2_max_poc_lsb;              ///< log2_max_pic_order_cnt_lsb_minus4
    //poc_type==1：底场 POC 差是否恒为 0
    int delta_pic_order_always_zero_flag;
    //poc_type==1：非参考帧的 POC 偏移
    int offset_for_non_ref_pic;
    //poc_type==1：顶场到底场的 POC 差
    int offset_for_top_to_bottom_field;
    //poc_type==1：循环表长度
    int poc_cycle_length;              ///< num_ref_frames_in_pic_order_cnt_cycle
    //最大参考帧数（1~16），决定 DPB 大小
    int ref_frame_count;               ///< num_ref_frames
    //是否允许 frame_num 不连续（跳号）
    int gaps_in_frame_num_allowed_flag;
    //水平宏块数
    int mb_width;                      ///< pic_width_in_mbs_minus1 + 1
    ///< (pic_height_in_map_units_minus1 + 1) * (2 - frame_mbs_only_flag)
    //垂直宏块数（场编码时会 ×2）
    int mb_height;
    //1=纯帧编码，0=允许场编码
    int frame_mbs_only_flag;
    // 这一整张 picture，到底按 frame 编码还是按两个 field 编码，由编码器决定。    Picture-Adaptive Frame/Field
    //桢编码下  每一对宏块（macroblock pair）可以自己决定 Frame coding 还是 Field coding。 Macroblock-Adaptive Frame/Field
    int mb_aff;                        ///< mb_adaptive_frame_field_flag
    //B 帧 direct 模式是否用 8×8 推断
    int direct_8x8_inference_flag;
    
    //	是否有裁剪
    int crop;                          ///< frame_cropping_flag
    /* those 4 are already in luma samples */
    unsigned int crop_left;            ///< frame_cropping_rect_left_offset
    unsigned int crop_right;           ///< frame_cropping_rect_right_offset
    unsigned int crop_top;             ///< frame_cropping_rect_top_offset
    unsigned int crop_bottom;          ///< frame_cropping_rect_bottom_offset

    //是否有 VUI 块
    int vui_parameters_present_flag;
    H2645VUI vui;
    //是否有时钟信息
    int timing_info_present_flag;
    //时钟 tick 数
    uint32_t num_units_in_tick;
    //每秒 tick 数
    uint32_t time_scale;
    //是否固定帧率
    int fixed_frame_rate_flag;
    int32_t offset_for_ref_frame[256];
    //是否有码流限制信息
    int bitstream_restriction_flag;
    //显示重排序缓冲最多允许多少帧，B 帧场景用 ref_frame_count=4 表示最多同时保留 4 个参考帧。
    int num_reorder_frames;
    //是否使用自定义量化矩阵
    int scaling_matrix_present;
    //6 组 4×4 量化缩放矩阵
    uint8_t scaling_matrix4[6][16];
    //6 组 8×8 量化缩放矩阵（High Profile + 8×8 变换）
    uint8_t scaling_matrix8[6][64];
    //NAL 层 HRD 参数是否存在
    int nal_hrd_parameters_present_flag;
    //VCL 层 HRD 参数是否存在
    int vcl_hrd_parameters_present_flag;
    //SEI picture_timing 是否带 pic_struct
    int pic_struct_present_flag;
    //时间偏移的比特长度（默认 24）
    int time_offset_length;
    //CPB 个数
    int cpb_cnt;                          ///< See H.264 E.1.2
    //初始 CPB 移除延迟的比特长度
    int initial_cpb_removal_delay_length; ///< initial_cpb_removal_delay_length_minus1 + 1
    //CPB 移除延迟的比特长度
    int cpb_removal_delay_length;         ///< cpb_removal_delay_length_minus1 + 1
    //DPB 输出延迟的比特长度
    int dpb_output_delay_length;          ///< dpb_output_delay_length_minus1 + 1
    //亮度位深，通常 8，High10 为 10
    int bit_depth_luma;                   ///< bit_depth_luma_minus8 + 8
    //色度位深，一般与亮度相同
    int bit_depth_chroma;                 ///< bit_depth_chroma_minus8 + 8
    //4:4:4 独立色度平面，FFmpeg 不支持
    int residual_color_transform_flag;    ///< residual_colour_transform_flag
    //6 个约束标志，表示是否满足某些 profile 子集要求
    int constraint_set_flags;             ///< constraint_set[0-3]_flag
    uint8_t data[4096];
    size_t data_size;
} SPS;

/**
 * Picture parameter set
 */
typedef struct PPS {
    unsigned int sps_id;
    //熵编码类型  entropy_coding_mode_flag  0=CAVLC，1=CABAC
    int cabac;                  ///< entropy_coding_mode_flag
    //控制 delta_pic_order_bottom 是否在slice header里 
    int pic_order_present;      ///< pic_order_present_flag
    //一帧里的宏块划分成多少组，主要用于帧内预测，一个宏块组内的宏块采用相同的预测模式。
    int slice_group_count;      ///< num_slice_groups_minus1 + 1
    //这些 Macroblock 到底按照什么规则分组， 
    /*
    0 → Interleaved
    1 → Dispersed
    2 → Foreground + Leftover
    3 → Box-out
    4 → Raster Scan
    5 → Wipe
    6 → Explicit
    */
    int mb_slice_group_map_type;
    //ref_count[0] L0 参考列表默认最大参考数   ref_count[1] L1 参考列表 
    // P 帧：主要用 ref_count[0]  B 帧：L0 前向 + L1 后向，两个都用
    unsigned int ref_count[2];  ///< num_ref_idx_l0/1_active_minus1 + 1
    //P 帧是否允许加权预测
    int weighted_pred;          ///< weighted_pred_flag
    //B 帧双向加权模式：0=不用，1=隐式，2=显式
    int weighted_bipred_idc;

    //量化参数初始值
    int init_qp;                ///< pic_init_qp_minus26 + 26
    // SP/SI Slice 量化参数初始值
    int init_qs;                ///< pic_init_qs_minus26 + 26
    int chroma_qp_index_offset[2];

    //slice header 是否带环路滤波参数
    int deblocking_filter_parameters_present; ///< deblocking_filter_parameters_present_flag
    //帧内宏块只能用帧内邻居做预测
    int constrained_intra_pred;     ///< constrained_intra_pred_flag
    //slice header 是否带 redundant_pic_cnt（冗余图像计数，极少见）
    int redundant_pic_cnt_present;  ///< redundant_pic_cnt_present_flag
    //是否允许 8×8 整数变换（High Profile）
    int transform_8x8_mode;         ///< transform_8x8_mode_flag
    //6 组 4×4 量化缩放矩阵
    uint8_t scaling_matrix4[6][16];
    // 6 组 8×8 量化缩放矩阵（transform_8x8_mode=1 时用）
    uint8_t scaling_matrix8[6][64];
    //色度量化表
    uint8_t chroma_qp_table[2][QP_MAX_NUM+1];  ///< pre-scaled (with chroma_qp_index_offset) version of qp_table
    int chroma_qp_diff;
    uint8_t data[4096];
    size_t data_size;
    //反量化 
    uint32_t dequant4_buffer[6][QP_MAX_NUM + 1][16];
    uint32_t dequant8_buffer[6][QP_MAX_NUM + 1][64];
    uint32_t(*dequant4_coeff[6])[16];
    uint32_t(*dequant8_coeff[6])[64];

    AVBufferRef *sps_ref;
    const SPS   *sps;
} PPS;

typedef struct H264ParamSets {
    AVBufferRef *sps_list[MAX_SPS_COUNT];
    AVBufferRef *pps_list[MAX_PPS_COUNT];

    AVBufferRef *pps_ref;
    /* currently active parameters sets */
    const PPS *pps;
    const SPS *sps;

    int overread_warning_printed[2];
} H264ParamSets;

/**
 * compute profile from sps
 */
int ff_h264_get_profile(const SPS *sps);

/**
 * Decode SPS
 */
int ff_h264_decode_seq_parameter_set(GetBitContext *gb, AVCodecContext *avctx,
                                     H264ParamSets *ps, int ignore_truncation);

/**
 * Decode PPS
 */
int ff_h264_decode_picture_parameter_set(GetBitContext *gb, AVCodecContext *avctx,
                                         H264ParamSets *ps, int bit_length);

/**
 * Uninit H264 param sets structure.
 */
void ff_h264_ps_uninit(H264ParamSets *ps);

#endif /* AVCODEC_H264_PS_H */
