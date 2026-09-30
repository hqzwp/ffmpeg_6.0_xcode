/**
 * H.264编码调试示例
 * 
 * 这个文件展示了如何使用FFmpeg API进行H.264编码
 * 可以在Xcode中设置断点进行调试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"

/**
 * 编码一帧YUV数据
 */
static int encode_frame(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt, FILE *outfile)
{
    int ret;

    /* 发送帧到编码器 */
    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending frame to encoder\n");
        return ret;
    }

    /* 从编码器读取所有可用的输出包 */
    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;
        else if (ret < 0) {
            fprintf(stderr, "Error encoding frame\n");
            return ret;
        }

        printf("编码帧 %3"PRId64" (大小=%5d bytes, 类型=%c)\n",
               pkt->pts, pkt->size,
               (pkt->flags & AV_PKT_FLAG_KEY) ? 'I' : 'P');
        
        /* 写入输出文件 */
        fwrite(pkt->data, 1, pkt->size, outfile);
        av_packet_unref(pkt);
    }

    return 0;
}

/**
 * H.264编码示例主函数
 */
int main(int argc, char **argv)
{
    const char *filename, *codec_name;
    const AVCodec *codec;
    AVCodecContext *codec_ctx = NULL;
    AVFrame *frame;
    AVPacket *pkt;
    FILE *f;
    int i, ret;
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };

    if (argc <= 2) {
        fprintf(stderr, "用法: %s <输出文件> <编码器名称>\n"
                "示例: %s output.h264 libx264\n", argv[0], argv[0]);
        exit(0);
    }
    filename = argv[1];
    codec_name = argv[2];

    /* 查找编码器 */
    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        fprintf(stderr, "找不到编码器 '%s'\n", codec_name);
        exit(1);
    }

    /* 分配编码器上下文 */
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "无法分配编码器上下文\n");
        exit(1);
    }

    /* 设置编码参数 */
    codec_ctx->bit_rate = 400000;           // 码率 400kbps
    codec_ctx->width = 352;                 // 宽度
    codec_ctx->height = 288;                // 高度 (CIF格式)
    codec_ctx->time_base = (AVRational){1, 25};  // 帧率 25fps
    codec_ctx->framerate = (AVRational){25, 1};
    
    /* GOP大小 - 每25帧一个I帧 */
    codec_ctx->gop_size = 25;
    codec_ctx->max_b_frames = 2;            // 最多2个连续B帧
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    /* 设置H.264特定参数 */
    if (codec->id == AV_CODEC_ID_H264) {
        // 设置preset为medium（平衡速度和质量）
        av_opt_set(codec_ctx->priv_data, "preset", "medium", 0);
        
        // 设置profile为high
        av_opt_set(codec_ctx->priv_data, "profile", "high", 0);
        
        // 设置tune为zerolatency（低延迟）
        // av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
        
        // 使用CRF模式，质量因子23
        av_opt_set(codec_ctx->priv_data, "crf", "23", 0);
    }

    /* 打开编码器 */
    ret = avcodec_open2(codec_ctx, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "无法打开编码器\n");
        exit(1);
    }

    /* 打开输出文件 */
    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "无法打开文件 %s\n", filename);
        exit(1);
    }

    /* 分配帧和包 */
    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "无法分配AVPacket\n");
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "无法分配AVFrame\n");
        exit(1);
    }
    frame->format = codec_ctx->pix_fmt;
    frame->width  = codec_ctx->width;
    frame->height = codec_ctx->height;

    /* 分配图像缓冲区 */
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "无法分配帧缓冲区\n");
        exit(1);
    }

    /* 编码100帧 */
    for (i = 0; i < 100; i++) {
        fflush(stdout);

        /* 确保帧数据可写 */
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

        /* 生成测试图像（Y分量） */
        for (int y = 0; y < codec_ctx->height; y++) {
            for (int x = 0; x < codec_ctx->width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
            }
        }

        /* 生成Cb和Cr分量 */
        for (int y = 0; y < codec_ctx->height/2; y++) {
            for (int x = 0; x < codec_ctx->width/2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        frame->pts = i;

        /* 编码帧 - 在这里可以设置断点调试 */
        encode_frame(codec_ctx, frame, pkt, f);
    }

    /* 刷新编码器 */
    encode_frame(codec_ctx, NULL, pkt, f);

    /* 添加序列结束码（可选） */
    if (codec->id == AV_CODEC_ID_MPEG1VIDEO || codec->id == AV_CODEC_ID_MPEG2VIDEO)
        fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    /* 清理 */
    avcodec_free_context(&codec_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    printf("\n编码完成！输出文件: %s\n", filename);
    printf("可以使用以下命令播放:\n");
    printf("  ffplay %s\n", filename);
    
    return 0;
}

