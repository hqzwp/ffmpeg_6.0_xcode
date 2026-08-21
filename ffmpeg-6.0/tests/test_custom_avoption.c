/*
 * test_custom_avoption.c — 自定义 AVOption 与 unit 关联测试
 *
 * FFmpeg 中，父选项（如 AV_OPT_TYPE_INT / AV_OPT_TYPE_FLAGS）可通过 .unit 字符串
 * 与若干 AV_OPT_TYPE_CONST 子常量关联：父项的 unit 与子常量的 unit 相同，
 * 则 av_opt_set(ctx, "mode", "fast", 0) 会解析为对应 CONST 的 default_val。
 * 另含 AV_OPT_TYPE_STRING（name）等常用字段的 set/校验。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>

#include "libavutil/opt.h"
#include "libavutil/mem.h"
#include "libavutil/log.h"

typedef struct MyContext {
    const AVClass *class;
    int mode;
    int flags;
    int quality;
    char *name;
} MyContext;

#define OFFSET(x) offsetof(MyContext, x)

static const AVOption my_options[] = {
    { "mode", "processing mode", OFFSET(mode), AV_OPT_TYPE_INT,
      { .i64 = 0 }, 0, INT_MAX, 0, "mode" },
    { "auto", "automatic mode", 0, AV_OPT_TYPE_CONST,
      { .i64 = 0 }, 0, 0, 0, "mode" },
    { "fast", "fast mode", 0, AV_OPT_TYPE_CONST,
      { .i64 = 1 }, 0, 0, 0, "mode" },
    { "slow", "slow mode", 0, AV_OPT_TYPE_CONST,
      { .i64 = 2 }, 0, 0, 0, "mode" },

    { "flags", "feature flags", OFFSET(flags), AV_OPT_TYPE_FLAGS,
      { .i64 = 0 }, 0, INT_MAX, 0, "flags" },
    { "enable_a", "enable feature A", 0, AV_OPT_TYPE_CONST,
      { .i64 = 1 }, 0, 0, 0, "flags" },
    { "enable_b", "enable feature B", 0, AV_OPT_TYPE_CONST,
      { .i64 = 2 }, 0, 0, 0, "flags" },
    { "enable_c", "enable feature C", 0, AV_OPT_TYPE_CONST,
      { .i64 = 4 }, 0, 0, 0, "flags" },

    { "quality", "quality level", OFFSET(quality), AV_OPT_TYPE_INT,
      { .i64 = 50 }, 0, 100, 0 },

    { "name", "object or stream name", OFFSET(name), AV_OPT_TYPE_STRING },

    { NULL },
};

static const char *my_item_name(void *ctx)
{
    return "MyContext";
}

static const AVClass my_class = {
    .class_name = "MyContext",
    .item_name  = my_item_name,
    .option     = my_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int tests_run;
static int tests_failed;

static void expect_int(const char *label, int got, int expected)
{
    tests_run++;
    if (got == expected) {
        printf("PASS: %s (got %d)\n", label, got);
    } else {
        printf("FAIL: %s (got %d, expected %d)\n", label, got, expected);
        tests_failed++;
    }
}

static void expect_ok(const char *label, int ret)
{
    tests_run++;
    if (ret >= 0) {
        printf("PASS: %s (ret=%d)\n", label, ret);
    } else {
        printf("FAIL: %s (ret=%d)\n", label, ret);
        tests_failed++;
    }
}


static void expect_str(const char *label, const char *got, const char *expected)
{
    const char *g = got ? got : "";
    const char *e = expected ? expected : "";

    tests_run++;
    if (!strcmp(g, e)) {
        printf("PASS: %s (got \"%s\")\n", label, g);
    } else {
        printf("FAIL: %s (got \"%s\", expected \"%s\")\n", label, g, e);
        tests_failed++;
    }
}

static void expect_fail(const char *label, int ret)
{
    tests_run++;
    if (ret < 0) {
        printf("PASS: %s (ret=%d, correctly failed)\n", label, ret);
    } else {
        printf("FAIL: %s (ret=%d, expected failure)\n", label, ret);
        tests_failed++;
    }
}

static void dump_consts_for_unit(void *obj, const char *unit)
{
    const AVOption *o;

    printf("--- CONST options with unit=\"%s\" ---\n", unit);
    for (o = NULL; (o = av_opt_next(obj, o)); ) {
        if (o->type == AV_OPT_TYPE_CONST && o->unit && !strcmp(o->unit, unit))
            printf("  name=%-12s default_val.i64=%lld\n",
                   o->name, (long long)o->default_val.i64);
    }
}

static MyContext *alloc_ctx(void)
{
    MyContext *ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return NULL;
    ctx->class = &my_class;
    av_opt_set_defaults(ctx);
    return ctx;
}

int main(void)
{
    MyContext *ctx;
    const AVOption *opt;
    int ret;

    av_log_set_level(AV_LOG_WARNING);

    ctx = alloc_ctx();
    if (!ctx) {
        fprintf(stderr, "FAIL: av_mallocz MyContext\n");
        return 1;
    }

    opt = av_opt_find(ctx, "mode", NULL, 0, 0);
    if (opt && opt->type == AV_OPT_TYPE_INT && opt->unit && !strcmp(opt->unit, "mode"))
        printf("PASS: av_opt_find(\"mode\") -> type=INT unit=mode\n");
    else {
        printf("FAIL: av_opt_find(\"mode\")\n");
        tests_failed++;
    }
    tests_run++;

    /* CONST 项须带 unit 查找；unit=NULL 时 av_opt_find 只匹配非 CONST */
    opt = av_opt_find(ctx, "fast", "mode", 0, 0);
    if (opt && opt->type == AV_OPT_TYPE_CONST && opt->default_val.i64 == 1)
        printf("PASS: av_opt_find(\"fast\", unit=mode) -> CONST i64=1\n");
    else {
        printf("FAIL: av_opt_find(\"fast\", unit=mode)\n");
        tests_failed++;
    }
    tests_run++;

    opt = av_opt_find(ctx, "fast", NULL, 0, 0);
    if (!opt)
        printf("PASS: av_opt_find(\"fast\", unit=NULL) correctly NULL (CONST hidden)\n");
    else {
        printf("FAIL: av_opt_find(\"fast\", unit=NULL) should be NULL\n");
        tests_failed++;
    }
    tests_run++;

    ret = av_opt_set(ctx, "mode", "fast", 0);
    expect_ok("av_opt_set mode=fast", ret);
    expect_int("mode after fast", ctx->mode, 1);

    ret = av_opt_set(ctx, "mode", "2", 0);
    expect_ok("av_opt_set mode=2", ret);
    expect_int("mode after 2", ctx->mode, 2);

    ret = av_opt_set(ctx, "flags", "+enable_a+enable_b", 0);
    expect_ok("av_opt_set flags +enable_a+enable_b", ret);
    expect_int("flags after +a+b", ctx->flags, 3);

    ret = av_opt_set(ctx, "flags", "-enable_a", 0);
    expect_ok("av_opt_set flags -enable_a", ret);
    expect_int("flags after -enable_a", ctx->flags, 2);

    ret = av_opt_set(ctx, "quality", "90", 0);
    expect_ok("av_opt_set quality=90", ret);
    expect_int("quality after 90", ctx->quality, 90);


    ret = av_opt_set(ctx, "name", "hello", 0);
    expect_ok("av_opt_set name=hello", ret);
    expect_str("name after hello", ctx->name, "hello");

    ret = av_opt_set(ctx, "name", "", 0);
    expect_ok("av_opt_set name=empty", ret);
    expect_str("name after empty", ctx->name, "");

    ret = av_opt_set(ctx, "name", "world", 0);
    expect_ok("av_opt_set name=world", ret);
    expect_str("name after world", ctx->name, "world");

    ret = av_opt_set(ctx, "mode", "not_a_valid_mode", 0);
    expect_fail("av_opt_set mode=not_a_valid_mode", ret);

    opt = av_opt_find(ctx, "mode", NULL, 0, 0);
    if (opt && opt->unit)
        dump_consts_for_unit(ctx, opt->unit);
    else
        printf("FAIL: could not get mode unit for CONST dump\n");

    opt = av_opt_find(ctx, "flags", NULL, 0, 0);
    if (opt && opt->unit)
        dump_consts_for_unit(ctx, opt->unit);

    av_opt_free(ctx);
    av_free(ctx);

    
    printf("--- summary: %d run, %d failed ---\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
