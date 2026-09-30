/*
 * ISO Media common code
 * copyright (c) 2001 Fabrice Bellard
 * copyright (c) 2002 Francois Revol <revol@free.fr>
 * copyright (c) 2006 Baptiste Coudurier <baptiste.coudurier@free.fr>
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

#ifndef AVFORMAT_ISOM_H
#define AVFORMAT_ISOM_H

#include <stddef.h>
#include <stdint.h>

#include "libavutil/encryption_info.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/spherical.h"
#include "libavutil/stereo3d.h"

#include "avio.h"
#include "internal.h"
#include "dv.h"

/* isom.c */
extern const AVCodecTag ff_mp4_obj_type[];
extern const AVCodecTag ff_codec_movvideo_tags[];
extern const AVCodecTag ff_codec_movaudio_tags[];
extern const AVCodecTag ff_codec_movsubtitle_tags[];
extern const AVCodecTag ff_codec_movdata_tags[];

int ff_mov_iso639_to_lang(const char lang[4], int mp4);
int ff_mov_lang_to_iso639(unsigned code, char to[4]);

struct AVAESCTR;

/* the QuickTime file format is quite convoluted...
 * it has lots of index tables, each indexing something in another one...
 * Here we just use what is needed to read the chunks
 用来计算DTS
 stts box 中可能存在多对{sample_count1 ..., sample_delta1 ...}
 eg: [10,20], [30,40]
 总共有30个sample 前10个sample间隔30  后20个sample间隔40
 DTS 计算 0 ,30 ,60,90,120,150,180,210,240,270, 310, 350, 390,
 */
typedef struct MOVStts {
    unsigned int count;
    unsigned int duration;
} MOVStts;

//上面的DTS + 这里的count = PTS
typedef struct MOVCtts {
    unsigned int count;
    int duration;
} MOVCtts;

//sample跟chunk的关系
/*
 eg:
 firstChunk             [1, 2]
 samplesPerChunk        [2, 1]
 sampleDescriptionIndex [1, 1]
 
 共两列 也就是两个chunk
 firstChunk 1表示从第一个chunk开始 每一个chunk对应2个sample  指定这个 Chunk 里的 Sample 使用 stsd（Sample Description Box）中的第1个描述。
 firstChunk 2表示从第二chunk个开始 每一个chunk对应1个sample  指定这个 Chunk 里的 Sample 使用 stsd（Sample Description Box）中的第1个描述。
 
 */
typedef struct MOVStsc {
    int first;
    int count;
    int id;
} MOVStsc;

typedef struct MOVElst {
    int64_t duration;
    int64_t time;
    float rate;
} MOVElst;

typedef struct MOVDref {
    uint32_t type;
    char *path;
    char *dir;
    char volume[28];
    char filename[64];
    int16_t nlvl_to, nlvl_from;
} MOVDref;

typedef struct MOVAtom {
    uint32_t type;
    int64_t size; /* total size (excluding the size and type fields) */
} MOVAtom;

struct MOVParseTableEntry;

typedef struct MOVFragment {
    int found_tfhd;
    unsigned track_id;
    uint64_t base_data_offset;
    uint64_t moof_offset;
    uint64_t implicit_offset;
    unsigned stsd_id;
    unsigned duration;
    unsigned size;
    unsigned flags;
} MOVFragment;

typedef struct MOVTrackExt {
    unsigned track_id;
    unsigned stsd_id;
    unsigned duration;
    unsigned size;
    unsigned flags;
} MOVTrackExt;

typedef struct MOVSbgp {
    unsigned int count;
    unsigned int index;
} MOVSbgp;

typedef struct MOVEncryptionIndex {
    // Individual encrypted samples.  If there are no elements, then the default
    // settings will be used.
    unsigned int nb_encrypted_samples;
    AVEncryptionInfo **encrypted_samples;

    uint8_t* auxiliary_info_sizes;
    size_t auxiliary_info_sample_count;
    uint8_t auxiliary_info_default_size;
    uint64_t* auxiliary_offsets;  ///< Absolute seek position
    size_t auxiliary_offsets_count;
} MOVEncryptionIndex;

typedef struct MOVFragmentStreamInfo {
    int id;
    int64_t sidx_pts;
    int64_t first_tfra_pts;
    int64_t tfdt_dts;
    int64_t next_trun_dts;
    // Index of the first sample/trun in the fragment.
    int index_base;
    int index_entry;
    MOVEncryptionIndex *encryption_index;
    int stsd_id; // current fragment stsd_id
} MOVFragmentStreamInfo;

typedef struct MOVFragmentIndexItem {
    int64_t moof_offset;
    int headers_read;
    int current;
    int nb_stream_info;
    MOVFragmentStreamInfo * stream_info;
} MOVFragmentIndexItem;

typedef struct MOVFragmentIndex {
    int allocated_size;
    int complete;
    int current;
    int nb_items;
    MOVFragmentIndexItem * item;
} MOVFragmentIndex;

typedef struct MOVIndexRange {
    int64_t start;
    int64_t end;
} MOVIndexRange;

typedef struct HEICItem {
    int item_id;
    uint64_t pos;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    int is_idat_relative;
    int type;
} HEICItem;

typedef struct MOVStreamContext {
    AVIOContext *pb;
    int pb_is_copied;
    int ffindex;          ///< AVStream index
    int next_chunk;
    unsigned int chunk_count;
    int64_t *chunk_offsets;
    unsigned int stts_count;
    MOVStts *stts_data;
    unsigned int sdtp_count;
    uint8_t *sdtp_data;
    unsigned int ctts_count;
    unsigned int ctts_allocated_size;
    MOVCtts *ctts_data;
    unsigned int stsc_count;
    MOVStsc *stsc_data;
    unsigned int stsc_index;
    int stsc_sample;
    unsigned int stps_count;
    unsigned *stps_data;  ///< partial sync sample for mpeg-2 open gop
    MOVElst *elst_data;
    unsigned int elst_count;
    int ctts_index;
    int ctts_sample;
    unsigned int sample_size; ///< may contain value calculated from stsd or value from stsz atom
    unsigned int stsz_sample_size; ///< always contains sample size from stsz atom
    ///<
    unsigned int sample_count;
    int *sample_sizes;
    int keyframe_absent;
    unsigned int keyframe_count; //关键帧
    int *keyframes; //关键帧索引
    int time_scale;
    int64_t time_offset;  ///< time offset of the edit list entries
    int64_t min_corrected_pts;  ///< minimum Composition time shown by the edits excluding empty edits.
    int current_sample;
    int64_t current_index;
    MOVIndexRange* index_ranges;
    MOVIndexRange* current_index_range;
    unsigned int bytes_per_frame;
    unsigned int samples_per_frame;
    int dv_audio_container;
    int pseudo_stream_id; ///< -1 means demux all ids
    int16_t audio_cid;    ///< stsd audio compression id
    unsigned drefs_count;
    MOVDref *drefs;
    int dref_id;
    int timecode_track;
    int width;            ///< tkhd width
    int height;           ///< tkhd height
    int dts_shift;        ///< dts shift when ctts is negative
    uint32_t palette[256];
    int has_palette;
    int64_t data_size;
    uint32_t tmcd_flags;  ///< tmcd track flags
    uint8_t tmcd_nb_frames;  ///< tmcd number of frames per tick / second
    int64_t track_end;    ///< used for dts generation in fragmented movie files
    int start_pad;        ///< amount of samples to skip due to enc-dec delay
    unsigned int rap_group_count;
    MOVSbgp *rap_group;
    unsigned int sync_group_count;
    MOVSbgp *sync_group;
    uint8_t *sgpd_sync;
    uint32_t sgpd_sync_count;
    int32_t *sample_offsets;
    int sample_offsets_count;
    int *open_key_samples;
    int open_key_samples_count;
    uint32_t min_sample_duration;

    int nb_frames_for_fps;
    int64_t duration_for_fps;

    /** extradata array (and size) for multiple stsd */
    uint8_t **extradata;
    int *extradata_size;
    int last_stsd_index;
    int stsd_count;
    int stsd_version;

    int32_t *display_matrix;
    AVStereo3D *stereo3d;
    AVSphericalMapping *spherical;
    size_t spherical_size;
    AVMasteringDisplayMetadata *mastering;
    AVContentLightMetadata *coll;
    size_t coll_size;

    uint32_t format;

    int has_sidx;  // If there is an sidx entry for this stream.
    struct {
        struct AVAESCTR* aes_ctr;
        struct AVAES *aes_ctx;
        unsigned int per_sample_iv_size;  // Either 0, 8, or 16.
        AVEncryptionInfo *default_encrypted_sample;
        MOVEncryptionIndex *encryption_index;
    } cenc;
} MOVStreamContext;

typedef struct MOVContext {
    const AVClass *class; ///< class for private options
    AVFormatContext *fc;
    int time_scale;
    int64_t duration;     ///< duration of the longest track
    int found_moov;       ///< 'moov' atom has been found
    int found_mdat;       ///< 'mdat' atom has been found
    int found_hdlr_mdta;  ///< 'hdlr' atom with type 'mdta' has been found
    int trak_index;       ///< Index of the current 'trak'
    char **meta_keys;
    unsigned meta_keys_count;
    DVDemuxContext *dv_demux;
    AVFormatContext *dv_fctx;
    int isom;             ///< 1 if file is ISO Media (mp4/3gp)
    MOVFragment fragment; ///< current fragment in moof atom
    MOVTrackExt *trex_data;
    unsigned trex_count;
    int itunes_metadata;  ///< metadata are itunes style
    int handbrake_version;
    int *chapter_tracks;
    unsigned int nb_chapter_tracks;
    int use_absolute_path;
    int ignore_editlist;
    int advanced_editlist;
    int advanced_editlist_autodisabled;
    int ignore_chapters;
    int seek_individually;
    int64_t next_root_atom; ///< offset of the next root atom
    int export_all;
    int export_xmp;
    int *bitrates;          ///< bitrates read before streams creation
    int bitrates_count;
    int moov_retry;
    int use_mfra_for;
    int has_looked_for_mfra;
    int use_tfdt;
    //MOVFragmentIndex 是 fMP4（分片 MP4）的分片索引表，挂在 MOVContext.frag_index 上。普通 MP4 一个 moov 管全文件，不需要它；DASH / HLS 的 .m4s、带 moof 的 mp4 才用。
    MOVFragmentIndex frag_index;
    int atom_depth;
    unsigned int aax_mode;  ///< 'aax' file has been detected
    uint8_t file_key[20];
    uint8_t file_iv[20];
    void *activation_bytes;
    int activation_bytes_size;
    void *audible_fixed_key;
    int audible_fixed_key_size;
    void *audible_key;
    int audible_key_size;
    void *audible_iv;
    int audible_iv_size;
    struct AVAES *aes_decrypt;
    uint8_t *decryption_key;
    int decryption_key_len;
    int enable_drefs;
    int32_t movie_display_matrix[3][3]; ///< display matrix from mvhd
    int have_read_mfra_size;
    uint32_t mfra_size;
    uint32_t max_stts_delta;
    int is_still_picture_avif;
    int primary_item_id;
    struct {
        int item_id;
        int extent_length;
        int64_t extent_offset;
    } *avif_info;
    int avif_info_size;
} MOVContext;

int ff_mp4_read_descr_len(AVIOContext *pb);
int ff_mp4_read_descr(AVFormatContext *fc, AVIOContext *pb, int *tag);
int ff_mp4_read_dec_config_descr(AVFormatContext *fc, AVStream *st, AVIOContext *pb);
void ff_mp4_parse_es_descr(AVIOContext *pb, int *es_id);

#define MP4ODescrTag                    0x01
#define MP4IODescrTag                   0x02
#define MP4ESDescrTag                   0x03
#define MP4DecConfigDescrTag            0x04
#define MP4DecSpecificDescrTag          0x05
#define MP4SLDescrTag                   0x06

#define MOV_TFHD_BASE_DATA_OFFSET       0x01
#define MOV_TFHD_STSD_ID                0x02
#define MOV_TFHD_DEFAULT_DURATION       0x08
#define MOV_TFHD_DEFAULT_SIZE           0x10
#define MOV_TFHD_DEFAULT_FLAGS          0x20
#define MOV_TFHD_DURATION_IS_EMPTY  0x010000
#define MOV_TFHD_DEFAULT_BASE_IS_MOOF 0x020000

#define MOV_TRUN_DATA_OFFSET            0x01
#define MOV_TRUN_FIRST_SAMPLE_FLAGS     0x04
#define MOV_TRUN_SAMPLE_DURATION       0x100
#define MOV_TRUN_SAMPLE_SIZE           0x200
#define MOV_TRUN_SAMPLE_FLAGS          0x400
#define MOV_TRUN_SAMPLE_CTS            0x800

#define MOV_FRAG_SAMPLE_FLAG_DEGRADATION_PRIORITY_MASK 0x0000ffff
#define MOV_FRAG_SAMPLE_FLAG_IS_NON_SYNC               0x00010000
#define MOV_FRAG_SAMPLE_FLAG_PADDING_MASK              0x000e0000
#define MOV_FRAG_SAMPLE_FLAG_REDUNDANCY_MASK           0x00300000
#define MOV_FRAG_SAMPLE_FLAG_DEPENDED_MASK             0x00c00000
#define MOV_FRAG_SAMPLE_FLAG_DEPENDS_MASK              0x03000000

#define MOV_FRAG_SAMPLE_FLAG_DEPENDS_NO                0x02000000
#define MOV_FRAG_SAMPLE_FLAG_DEPENDS_YES               0x01000000

#define MOV_TKHD_FLAG_ENABLED       0x0001
#define MOV_TKHD_FLAG_IN_MOVIE      0x0002
#define MOV_TKHD_FLAG_IN_PREVIEW    0x0004
#define MOV_TKHD_FLAG_IN_POSTER     0x0008

#define MOV_SAMPLE_DEPENDENCY_UNKNOWN 0x0
#define MOV_SAMPLE_DEPENDENCY_YES     0x1
#define MOV_SAMPLE_DEPENDENCY_NO      0x2


#define TAG_IS_AVCI(tag)                    \
    ((tag) == MKTAG('a', 'i', '5', 'p') ||  \
     (tag) == MKTAG('a', 'i', '5', 'q') ||  \
     (tag) == MKTAG('a', 'i', '5', '2') ||  \
     (tag) == MKTAG('a', 'i', '5', '3') ||  \
     (tag) == MKTAG('a', 'i', '5', '5') ||  \
     (tag) == MKTAG('a', 'i', '5', '6') ||  \
     (tag) == MKTAG('a', 'i', '1', 'p') ||  \
     (tag) == MKTAG('a', 'i', '1', 'q') ||  \
     (tag) == MKTAG('a', 'i', '1', '2') ||  \
     (tag) == MKTAG('a', 'i', '1', '3') ||  \
     (tag) == MKTAG('a', 'i', '1', '5') ||  \
     (tag) == MKTAG('a', 'i', '1', '6') ||  \
     (tag) == MKTAG('a', 'i', 'v', 'x') ||  \
     (tag) == MKTAG('A', 'V', 'i', 'n'))


int ff_mov_read_esds(AVFormatContext *fc, AVIOContext *pb);

int ff_mov_read_stsd_entries(MOVContext *c, AVIOContext *pb, int entries);
void ff_mov_write_chan(AVIOContext *pb, int64_t channel_layout);

#define FF_MOV_FLAG_MFRA_AUTO -1
#define FF_MOV_FLAG_MFRA_DTS 1
#define FF_MOV_FLAG_MFRA_PTS 2

/**
 * Compute codec id for 'lpcm' tag.
 * See CoreAudioTypes and AudioStreamBasicDescription at Apple.
 */
static inline enum AVCodecID ff_mov_get_lpcm_codec_id(int bps, int flags)
{
    /* lpcm flags:
     * 0x1 = float
     * 0x2 = big-endian
     * 0x4 = signed
     */
    return ff_get_pcm_codec_id(bps, flags & 1, flags & 2, flags & 4 ? -1 : 0);
}

#define MOV_ISMV_TTML_TAG MKTAG('d', 'f', 'x', 'p')
#define MOV_MP4_TTML_TAG  MKTAG('s', 't', 'p', 'p')

struct MP4TrackKindValueMapping {
    int         disposition;
    const char *value;
};

struct MP4TrackKindMapping {
    const char   *scheme_uri;
    const struct  MP4TrackKindValueMapping *value_maps;
};

extern const struct MP4TrackKindMapping ff_mov_track_kind_table[];

#endif /* AVFORMAT_ISOM_H */


/*
 +--------------------------------------------------------------------------------+
 | ftyp                                   | 文件类型(File Type)                    |
 |                                        | 指定 MP4 品牌、兼容标准                |
 +--------------------------------------------------------------------------------+
 | free (可选)                            | 空闲空间                              |
 |                                        | 用于后续修改文件，播放器忽略           |
 +--------------------------------------------------------------------------------+
 | mdat                                   | Media Data                            |
 |                                        | 存放音频、视频、字幕等原始编码数据      |
 +--------------------------------------------------------------------------------+
 | moov                                   | Movie Box                             |
 |                                        | 保存整个文件的元数据、索引、时间轴      |
 |                                                                                |
 |   + mvhd                               | Movie Header                          |
 |   |                                    | 整个文件时间基、总时长、播放速率等      |
 |   |                                                                           |
 |   + trak (Video Track)                 | 一路视频轨                            |
 |   |                                                                           |
 |   |   + tkhd                           | Track Header                          |
 |   |   |                                | Track ID、宽高、时长、Layer 等信息     |
 |   |                                                                       |
 |   |   + mdia                           | Media Box                             |
 |   |       |                            | 描述该 Track 的媒体信息               |
 |   |                                                                       |
 |   |       + mdhd                       | Media Header                          |
 |   |       |                            | Track 时间基(timescale)、时长          |
 |   |                                                                       |
 |   |       + hdlr                       | Handler Reference                     |
 |   |       |                            | Track 类型(vide/soun/subt...)         |
 |   |                                                                       |
 |   |       + minf                       | Media Information                     |
 |   |           |                        | 视频/音频相关信息                     |
 |   |                                                                   |
 |   |           + vmhd                   | Video Media Header                    |
 |   |           |                        | 视频播放属性                          |
 |   |           |                        | (音频对应 smhd)                       |
 |   |                                                                   |
 |   |           + dinf                   | Data Information                      |
 |   |           |                        | 数据引用(Data Reference)              |
 |   |                                                                   |
 |   |           + stbl                   | Sample Table                          |
 |   |                |                   | Sample 索引表，是 MP4 最重要部分       |
 |   |                                                               |
 |   |                + stsd              | Sample Description                    |
 |   |                |                   | 编码格式(avc1/hvc1/mp4a...)           |
 |   |                | └── avc1
 
 │                   └── avcC

 │                       ├── SPS

 │                       └── PPS                  | SPS/PPS、AudioSpecificConfig 等       |
 |   |                                                               |
 |   |                + stts              | Decoding Time To Sample               |
 |   |                |                   | DTS 时间表                            |
 |   |                |                   | 每个 Sample 的解码时间间隔            |
 |   |                                                               |
 |   |                + ctts (可选)       | Composition Time To Sample            |
 |   |                |                   | PTS-DTS 偏移                          |
 |   |                |                   | B Frame 时用于计算显示时间            |
 |   |                                                               |
 |   |                + stss (可选)       | Sync Sample                           |
 |   |                |                   | 关键帧(I Frame)索引                   |
 |   |                |                   | Seek 时首先查询                       |
 |   |                                                               |
 |   |                + stsc              | Sample To Chunk                       |
 |   |                |                   | Sample 与 Chunk 的对应关系            |
 |   |                                                               |
 |   |                + stsz / stz2       | Sample Size                           |
 |   |                |                   | 每个 Sample 的大小(Byte)              |
 |   |                                                               |
 |   |                + stco / co64       | Chunk Offset                          |
 |   |                |                   | 每个 Chunk 在 mdat 中的文件偏移        |
 |   |                |                   | co64 为 64 位偏移                     |
 |   |                                                               |
 |   |                + sgpd (可选)       | Sample Group Description              |
 |   |                |                   | Sample 分组描述                       |
 |   |                                                               |
 |   |                + sbgp (可选)       | Sample To Group                       |
 |   |                |                   | Sample 与 Group 的映射                |
 |   |                                                               |
 |   |                + sdtp (可选)       | Sample Dependency Type                |
 |   |                |                   | Sample 依赖关系                       |
 |   |                                                               |
 |   |                + stdp (可选)       | Sample Degradation Priority           |
 |   |                |                   | Sample 优先级                         |
 |   |                                                               |
 |   |                + padb (可选)       | Sample Padding Bits                   |
 |   |                |                   | Padding 信息                          |
 |   |                                                               |
 |   |                + subs (可选)       | Subsample Information                 |
 |   |                |                   | 子 Sample 信息(常见于 HEVC、加密等)    |
 |   |                                                               |
 |   + trak (Audio Track)                 | 音频 Track                            |
 |       |                                | 与视频结构基本一致                    |
 |       |                                | vmhd → smhd                           |
 |       |                                | stsd 中通常为 mp4a                    |
 |       |                                | AudioSpecificConfig 保存 AAC 参数     |
 +--------------------------------------------------------------------------------+
 
 
 
 HEIF
 
 ┌──────────────────────────────────────────┐
 │ ftyp                                     │
 │   文件类型 / brand                        │
 ├──────────────────────────────────────────┤
 │ meta                                     │
 │   ├── hdlr                               │
 │   ├── pitm                               │
 │   ├── iloc                               │
 │   ├── iinf                               │
 │   ├── iprp                               │
 │   │    ├── ipco                           │
 │   │    └── ipma                           │
 │   └── ...                                │
 ├──────────────────────────────────────────┤
 │ mdat                                     │
 │   └── HEVC image data                    │
 └──────────────────────────────────────────┘
 
 
 */
