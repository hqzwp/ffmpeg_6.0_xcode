/*
 * Demo / unit-style tests for AVIOContext buffering, read, seek, and dyn write.
 *
 * Covers:
 *   - custom read_packet / seek callbacks over an in-memory blob
 *   - buffer window fields: buffer, buffer_size, buf_ptr, buf_end, pos
 *   - sequential avio_read / avio_r8 / avio_rl32
 *   - avio_seek / avio_skip / avio_tell / avio_feof / avio_size
 *   - avio_open_dyn_buf write path
 *   - optional real-file open via argv[1]
 *
 * Build (from ffmpeg-6.0 with installed/built libs), example:
 *   cc -o avio_demo fftools/avio_demo.c \
 *      -I. -Llibavformat -Llibavutil \
 *      -lavformat -lavutil -lm
 *
 * Or add this file to an Xcode target that already links libavformat/libavutil.
 *
 * Run:
 *   ./avio_demo
 *   ./avio_demo /path/to/any.file
 */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

#include "libavutil/common.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"
#include "libavformat/avio.h"
#include "libavformat/avformat.h"

/* Intentionally tiny so fill_buffer is hit many times. */
#define DEMO_IO_BUF_SIZE 16

struct buffer_data {
    uint8_t *base;   /* start of whole blob */
    size_t   total;  /* total bytes */
    uint8_t *ptr;    /* current read position */
    size_t   size;   /* bytes left from ptr */
    int      read_calls;
};

static int g_fail = 0;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fflush(stderr);
        g_fail++;
    } else {
        printf("  OK: %s\n", msg);
        fflush(stdout);
    }
}

static void dump_avio(const char *tag, AVIOContext *s)
{
    int used = s->buffer ? (int)(s->buf_ptr - s->buffer) : -1;
    int valid = s->buffer ? (int)(s->buf_end - s->buffer) : -1;
    int unread = s->buf_end && s->buf_ptr ? (int)(s->buf_end - s->buf_ptr) : 0;

    printf("---- %s ----\n", tag);
    printf("  buffer_size=%d  pos=%"PRId64"  tell=%"PRId64"  eof=%d  seekable=0x%x\n",
           s->buffer_size, s->pos, avio_tell(s), avio_feof(s), s->seekable);
    printf("  window: used=%d  valid=%d  unread=%d  (buf_ptr-buffer / buf_end-buffer)\n",
           used, valid, unread);
    if (s->protocol_whitelist)
        printf("  protocol_whitelist=%s\n", s->protocol_whitelist);
}

static int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    struct buffer_data *bd = opaque;
    int n;

    bd->read_calls++;
    n = FFMIN(buf_size, (int)bd->size);
    if (n <= 0)
        return AVERROR_EOF;

    memcpy(buf, bd->ptr, n);
    bd->ptr  += n;
    bd->size -= n;

    printf("  [read_packet #%d] asked=%d gave=%d  left=%zu  offset=%zu\n",
           bd->read_calls, buf_size, n, bd->size,
           (size_t)(bd->ptr - bd->base));
    return n;
}

static int64_t seek_packet(void *opaque, int64_t offset, int whence)
{
    struct buffer_data *bd = opaque;
    int64_t new_pos;

    if (whence == AVSEEK_SIZE)
        return (int64_t)bd->total;

    if (whence == SEEK_CUR)
        offset += (int64_t)(bd->ptr - bd->base);
    else if (whence == SEEK_END)
        offset += (int64_t)bd->total;
    else if (whence != SEEK_SET)
        return AVERROR(EINVAL);

    if (offset < 0 || (size_t)offset > bd->total)
        return AVERROR(EINVAL);

    new_pos = offset;
    bd->ptr  = bd->base + new_pos;
    bd->size = bd->total - (size_t)new_pos;

    printf("  [seek_packet] whence=%d -> offset=%"PRId64" left=%zu\n",
           whence == AVSEEK_SIZE ? -1 : whence, new_pos, bd->size);
    return new_pos;
}

static void reset_bd(struct buffer_data *bd, uint8_t *data, size_t len)
{
    bd->base = data;
    bd->total = len;
    bd->ptr = data;
    bd->size = len;
    bd->read_calls = 0;
}

static AVIOContext *open_mem_avio(struct buffer_data *bd, int buf_size, int with_seek)
{
    uint8_t *iobuf = av_malloc(buf_size);
    AVIOContext *s;

    if (!iobuf)
        return NULL;

    s = avio_alloc_context(iobuf, buf_size, 0, bd,
                           read_packet, NULL,
                           with_seek ? seek_packet : NULL);
    if (!s) {
        av_freep(&iobuf);
        return NULL;
    }
    /* With seek callback, avio_size / random access work. */
    s->seekable = with_seek ? AVIO_SEEKABLE_NORMAL : 0;
    return s;
}

static void close_mem_avio(AVIOContext **ps)
{
    AVIOContext *s = *ps;
    if (!s)
        return;
    av_freep(&s->buffer);
    avio_context_free(ps);
}

/* ---------- tests ---------- */

static void test_buffer_window(void)
{
    /* 48 bytes of data, 16-byte AVIO buffer => at least 3 fill_buffer calls. */
    uint8_t data[48];
    struct buffer_data bd;
    AVIOContext *s;
    uint8_t out[48];
    int n, i;

    printf("\n=== TEST: buffer window / sequential read ===\n");
    for (i = 0; i < (int)sizeof(data); i++)
        data[i] = (uint8_t)i;

    reset_bd(&bd, data, sizeof(data));
    s = open_mem_avio(&bd, DEMO_IO_BUF_SIZE, 1);
    expect(s != NULL, "avio_alloc_context");
    if (!s)
        return;

    dump_avio("after alloc (empty window)", s);
    expect(s->buffer_size == DEMO_IO_BUF_SIZE, "buffer_size == DEMO_IO_BUF_SIZE");

    n = avio_read(s, out, 10);
    expect(n == 10, "first avio_read 10 bytes");
    expect(out[0] == 0 && out[9] == 9, "payload bytes 0..9");
    dump_avio("after read 10 (may still have unread in window)", s);
    expect(bd.read_calls >= 1, "read_packet called at least once");

    n = avio_read(s, out, 30);
    expect(n == 30, "second avio_read 30 bytes");
    expect(out[0] == 10 && out[29] == 39, "continues from previous end");
    dump_avio("after read 30 more", s);

    n = avio_read(s, out, 16);
    expect(n == 8, "last partial read returns remaining 8");

    n = avio_read(s, out, 1);
    expect(n == AVERROR_EOF || n == 0 || avio_feof(s),
           "further read hits EOF");
    /* One more r8 to set eof_reached if not already. */
    if (!avio_feof(s))
        (void)avio_r8(s);
    dump_avio("at EOF", s);
    expect(avio_tell(s) == (int64_t)sizeof(data), "tell == total size at EOF");
    printf("  read_packet total calls=%d (expect >1 due to small buffer)\n", bd.read_calls);
    expect(bd.read_calls > 1, "multiple fill_buffer / read_packet due to small buffer");

    close_mem_avio(&s);
}

static void test_typed_and_seek(void)
{
    /* little-endian layout: u8=0x11, u32le=0x04030201, then 'ABCD' */
    uint8_t data[] = {
        0x11,
        0x01, 0x02, 0x03, 0x04,
        'A', 'B', 'C', 'D',
        0xAA, 0xBB
    };
    struct buffer_data bd;
    AVIOContext *s;
    unsigned v;
    int64_t sz, pos;
    uint8_t four[4];

    printf("\n=== TEST: typed reads + seek/skip ===\n");
    reset_bd(&bd, data, sizeof(data));
    s = open_mem_avio(&bd, DEMO_IO_BUF_SIZE, 1);
    expect(s != NULL, "alloc with seek");
    if (!s)
        return;

    sz = avio_size(s);
    expect(sz == (int64_t)sizeof(data), "avio_size via AVSEEK_SIZE");

    expect(avio_r8(s) == 0x11, "avio_r8");
    v = avio_rl32(s);
    expect(v == 0x04030201u, "avio_rl32");
    expect(avio_read(s, four, 4) == 4 && !memcmp(four, "ABCD", 4), "read ABCD");
    dump_avio("after typed reads", s);

    pos = avio_seek(s, 1, SEEK_SET);
    expect(pos == 1, "seek SET to 1");
    expect(avio_rl32(s) == 0x04030201u, "re-read u32 after seek");

    pos = avio_seek(s, 2, SEEK_CUR);
    expect(pos == 7, "seek CUR +2 from after u32 (pos was 5) -> 7");
    expect(avio_r8(s) == 'C', "byte at offset 7 is 'C'");

    /* avio_seek only accepts SEEK_SET / SEEK_CUR (not SEEK_END). */
    pos = avio_seek(s, -2, SEEK_END);
    expect(pos == AVERROR(EINVAL), "SEEK_END rejected by avio_seek");
    pos = avio_seek(s, (int64_t)sizeof(data) - 2, SEEK_SET);
    expect(pos == (int64_t)sizeof(data) - 2, "seek SET to size-2");
    expect(avio_r8(s) == 0xAA && avio_r8(s) == 0xBB, "last two bytes");

    avio_seek(s, 0, SEEK_SET);
    expect(avio_skip(s, 5) == 5, "avio_skip 5 returns new pos 5");
    expect(avio_tell(s) == 5, "tell after skip");
    expect(avio_r8(s) == 'A', "after skip lands on 'A'");

    close_mem_avio(&s);
}

static void test_no_seek_sequential_only(void)
{
    uint8_t data[32];
    struct buffer_data bd;
    AVIOContext *s;
    int i;
    int64_t ret;

    printf("\n=== TEST: no seek callback (sequential only) ===\n");
    for (i = 0; i < 32; i++)
        data[i] = (uint8_t)(0x80 + i);

    reset_bd(&bd, data, sizeof(data));
    s = open_mem_avio(&bd, 8, 0);
    expect(s != NULL, "alloc without seek");
    if (!s)
        return;

    expect(avio_r8(s) == 0x80, "first byte");
    ret = avio_seek(s, 1, SEEK_SET);
    /* Without seek callback, backward seek outside buffer typically fails. */
    expect(avio_r8(s) == 0x80+1, "0x81");
    printf("  seek(0, SEEK_SET) returned %"PRId64" (%s)\n",
           ret, ret < 0 ? av_err2str(ret) : "ok");
    dump_avio("after attempted rewind", s);

    close_mem_avio(&s);
}

static void test_dyn_write(void)
{
    AVIOContext *s = NULL;
    uint8_t *out = NULL;
    int len;
    int ret;

    printf("\n=== TEST: avio_open_dyn_buf write ===\n");
    ret = avio_open_dyn_buf(&s);
    expect(ret == 0 && s, "avio_open_dyn_buf");
    if (ret < 0 || !s)
        return;

    avio_w8(s, 0xDE);
    avio_wl32(s, 0x12345678);
    avio_write(s, (const unsigned char *)"XYZ", 3);
    dump_avio("dyn write buffer", s);

    len = avio_close_dyn_buf(s, &out);
    expect(len == 1 + 4 + 3, "dyn buf length 8");
    if (out && len >= 8) {
        expect(out[0] == 0xDE, "first byte DE");
        expect(out[1] == 0x78 && out[4] == 0x12, "wl32 little-endian");
        expect(!memcmp(out + 5, "XYZ", 3), "trailing XYZ");
    }
    av_free(out);
}

static void test_file_optional(const char *path)
{
    AVIOContext *s = NULL;
    int ret;
    int64_t sz;
    uint8_t buf[32];
    int n;

    printf("\n=== TEST: avio_open2 real file ===\n");
    ret = avio_open2(&s, path, AVIO_FLAG_READ, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "  skip/fail open %s: %s\n", path, av_err2str(ret));
        g_fail++;
        return;
    }

    dump_avio("file opened", s);
    sz = avio_size(s);
    printf("  file size=%"PRId64"\n", sz);
    expect(sz >= 0, "avio_size >= 0");

    n = avio_read(s, buf, sizeof(buf));
    expect(n > 0 || sz == 0, "read some bytes (or empty file)");
    printf("  first read n=%d\n", n);
    if (n > 0) {
        int i;
        printf("  hex:");
        for (i = 0; i < n && i < 16; i++)
            printf(" %02x", buf[i]);
        printf("\n");
    }

    if (s->protocol_whitelist)
        printf("  (whitelist came from URL/protocol layer)\n");

    avio_closep(&s);
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [optional_input_file]\n"
            "  Runs AVIOContext self-tests. If a file path is given, also\n"
            "  exercises avio_open2() on that file.\n",
            argv0);
}

int main(int argc, char **argv)
{
    printf("AVIOContext detailed demo / self-test\n");
    printf("IO buffer size used in mem tests: %d\n", DEMO_IO_BUF_SIZE);

//    if (argc > 2) {
//        usage(argv[0]);
//        return 1;
//    }

    avformat_network_init();

//    test_buffer_window();
//    test_typed_and_seek();
//    test_no_seek_sequential_only();
    test_dyn_write();
//
//    if (argc == 2)
//        test_file_optional(argv[1]);
//    else
//        printf("\n(optional) pass a file path to also test avio_open2\n");
//
//    avformat_network_deinit();
//
//    printf("\n=== SUMMARY: %s (%d failure(s)) ===\n",
//           g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
