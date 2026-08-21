/*
 * URL utility functions
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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


#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/avassert.h"

#include "avformat.h"
#include "internal.h"
#include "config.h"
#include "url.h"

#if CONFIG_NETWORK
#include "network.h"
#endif
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"



/** @name Logging context. */
/*@{*/
static const char *urlcontext_to_name(void *ptr)
{
    URLContext *h = (URLContext *)ptr;
    if (h->prot)
        return h->prot->name;
    else
        return "NULL";
}

static void *urlcontext_child_next(void *obj, void *prev)
{
    URLContext *h = obj;
    if (!prev && h->priv_data && h->prot->priv_data_class)
        return h->priv_data;
    return NULL;
}

#define OFFSET(x) offsetof(URLContext,x)
#define E AV_OPT_FLAG_ENCODING_PARAM
#define D AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    {"protocol_whitelist", "List of protocols that are allowed to be used", OFFSET(protocol_whitelist), AV_OPT_TYPE_STRING, { .str = NULL },  0, 0, D },
    {"protocol_blacklist", "List of protocols that are not allowed to be used", OFFSET(protocol_blacklist), AV_OPT_TYPE_STRING, { .str = NULL },  0, 0, D },
    {"rw_timeout", "Timeout for IO operations (in microseconds)", offsetof(URLContext, rw_timeout), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_DECODING_PARAM },
    { NULL }
};

const AVClass ffurl_context_class = {
    .class_name       = "URLContext",
    .item_name        = urlcontext_to_name,
    .option           = options,
    .version          = LIBAVUTIL_VERSION_INT,
    .child_next       = urlcontext_child_next,
    .child_class_iterate = ff_urlcontext_child_class_iterate,
};
/*@}*/

static int url_alloc_for_protocol(URLContext **puc, const URLProtocol *up,
                                  const char *filename, int flags,
                                  const AVIOInterruptCB *int_cb)
{
    URLContext *uc;
    int err;

#if CONFIG_NETWORK
    if (up->flags & URL_PROTOCOL_FLAG_NETWORK && !ff_network_init())
        return AVERROR(EIO);
#endif
    if ((flags & AVIO_FLAG_READ) && !up->url_read) {
        av_log(NULL, AV_LOG_ERROR,
               "Impossible to open the '%s' protocol for reading\n", up->name);
        return AVERROR(EIO);
    }
    if ((flags & AVIO_FLAG_WRITE) && !up->url_write) {
        av_log(NULL, AV_LOG_ERROR,
               "Impossible to open the '%s' protocol for writing\n", up->name);
        return AVERROR(EIO);
    }
    uc = av_mallocz(sizeof(URLContext) + strlen(filename) + 1);
    if (!uc) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    uc->av_class = &ffurl_context_class;
    uc->filename = (char *)&uc[1];
    strcpy(uc->filename, filename);
    uc->prot            = up;
    uc->flags           = flags;
    uc->is_streamed     = 0; /* default = not streamed */
    uc->max_packet_size = 0; /* default: stream file */
    if (up->priv_data_size) {
        uc->priv_data = av_mallocz(up->priv_data_size);
        if (!uc->priv_data) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        if (up->priv_data_class) {
            char *start;
            *(const AVClass **)uc->priv_data = up->priv_data_class;
            av_opt_set_defaults(uc->priv_data);
            if (av_strstart(uc->filename, up->name, (const char**)&start) && *start == ',') {
                int ret= 0;
                char *p= start;
                char sep= *++p;
                char *key, *val;
                p++;

                if (strcmp(up->name, "subfile"))
                    ret = AVERROR(EINVAL);

                while(ret >= 0 && (key= strchr(p, sep)) && p<key && (val = strchr(key+1, sep))){
                    *val= *key= 0;
                    if (strcmp(p, "start") && strcmp(p, "end")) {
                        ret = AVERROR_OPTION_NOT_FOUND;
                    } else
                        ret= av_opt_set(uc->priv_data, p, key+1, 0);
                    if (ret == AVERROR_OPTION_NOT_FOUND)
                        av_log(uc, AV_LOG_ERROR, "Key '%s' not found.\n", p);
                    *val= *key= sep;
                    p= val+1;
                }
                if(ret<0 || p!=key){
                    av_log(uc, AV_LOG_ERROR, "Error parsing options string %s\n", start);
                    av_freep(&uc->priv_data);
                    av_freep(&uc);
                    err = AVERROR(EINVAL);
                    goto fail;
                }
                memmove(start, key+1, strlen(key));
            }
        }
    }
    if (int_cb)
        uc->interrupt_callback = *int_cb;

    *puc = uc;
    return 0;
fail:
    *puc = NULL;
    if (uc)
        av_freep(&uc->priv_data);
    av_freep(&uc);
#if CONFIG_NETWORK
    if (up->flags & URL_PROTOCOL_FLAG_NETWORK)
        ff_network_close();
#endif
    return err;
}

int ffurl_connect(URLContext *uc, AVDictionary **options)
{
    int err;
    AVDictionary *tmp_opts = NULL;
    AVDictionaryEntry *e;

    if (!options)
        options = &tmp_opts;

    // Check that URLContext was initialized correctly and lists are matching if set
    av_assert0(!(e=av_dict_get(*options, "protocol_whitelist", NULL, 0)) ||
               (uc->protocol_whitelist && !strcmp(uc->protocol_whitelist, e->value)));
    av_assert0(!(e=av_dict_get(*options, "protocol_blacklist", NULL, 0)) ||
               (uc->protocol_blacklist && !strcmp(uc->protocol_blacklist, e->value)));

    if (uc->protocol_whitelist && av_match_list(uc->prot->name, uc->protocol_whitelist, ',') <= 0) {
        av_log(uc, AV_LOG_ERROR, "Protocol '%s' not on whitelist '%s'!\n", uc->prot->name, uc->protocol_whitelist);
        return AVERROR(EINVAL);
    }

    if (uc->protocol_blacklist && av_match_list(uc->prot->name, uc->protocol_blacklist, ',') > 0) {
        av_log(uc, AV_LOG_ERROR, "Protocol '%s' on blacklist '%s'!\n", uc->prot->name, uc->protocol_blacklist);
        return AVERROR(EINVAL);
    }

    if (!uc->protocol_whitelist && uc->prot->default_whitelist) {
        av_log(uc, AV_LOG_DEBUG, "Setting default whitelist '%s'\n", uc->prot->default_whitelist);
        uc->protocol_whitelist = av_strdup(uc->prot->default_whitelist);
        if (!uc->protocol_whitelist) {
            return AVERROR(ENOMEM);
        }
    } else if (!uc->protocol_whitelist)
        av_log(uc, AV_LOG_DEBUG, "No default whitelist set\n"); // This should be an error once all declare a default whitelist

    if ((err = av_dict_set(options, "protocol_whitelist", uc->protocol_whitelist, 0)) < 0)
        return err;
    if ((err = av_dict_set(options, "protocol_blacklist", uc->protocol_blacklist, 0)) < 0)
        return err;

    err =
        uc->prot->url_open2 ? uc->prot->url_open2(uc,
                                                  uc->filename,
                                                  uc->flags,
                                                  options) :
        uc->prot->url_open(uc, uc->filename, uc->flags);

    av_dict_set(options, "protocol_whitelist", NULL, 0);
    av_dict_set(options, "protocol_blacklist", NULL, 0);

    if (err)
        return err;
    uc->is_connected = 1;
    /* We must be careful here as ffurl_seek() could be slow,
     * for example for http */
    if ((uc->flags & AVIO_FLAG_WRITE) || !strcmp(uc->prot->name, "file"))
        if (!uc->is_streamed && ffurl_seek(uc, 0, SEEK_SET) < 0)
            uc->is_streamed = 1;
    return 0;
}

int ffurl_accept(URLContext *s, URLContext **c)
{
    av_assert0(!*c);
    if (s->prot->url_accept)
        return s->prot->url_accept(s, c);
    return AVERROR(EBADF);
}

int ffurl_handshake(URLContext *c)
{
    int ret;
    if (c->prot->url_handshake) {
        ret = c->prot->url_handshake(c);
        if (ret)
            return ret;
    }
    c->is_connected = 1;
    return 0;
}

#define URL_SCHEME_CHARS                        \
    "abcdefghijklmnopqrstuvwxyz"                \
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"                \
    "0123456789+-."

static const struct URLProtocol *url_find_protocol(const char *filename)
{
    const URLProtocol **protocols;
    char proto_str[128], proto_nested[128], *ptr;
    size_t proto_len = strspn(filename, URL_SCHEME_CHARS);
    int i;

    if (filename[proto_len] != ':' &&
        (strncmp(filename, "subfile,", 8) || !strchr(filename + proto_len + 1, ':')) ||
        is_dos_path(filename))
        strcpy(proto_str, "file");
    else
        av_strlcpy(proto_str, filename,
                   FFMIN(proto_len + 1, sizeof(proto_str)));

    av_strlcpy(proto_nested, proto_str, sizeof(proto_nested));
    if ((ptr = strchr(proto_nested, '+')))
        *ptr = '\0';

    protocols = ffurl_get_protocols(NULL, NULL);
    if (!protocols)
        return NULL;
    for (i = 0; protocols[i]; i++) {
            const URLProtocol *up = protocols[i];
        if (!strcmp(proto_str, up->name)) {
            av_freep(&protocols);
            return up;
        }
        if (up->flags & URL_PROTOCOL_FLAG_NESTED_SCHEME &&
            !strcmp(proto_nested, up->name)) {
            av_freep(&protocols);
            return up;
        }
    }
    av_freep(&protocols);
    if (av_strstart(filename, "https:", NULL) || av_strstart(filename, "tls:", NULL))
        av_log(NULL, AV_LOG_WARNING, "https protocol not found, recompile FFmpeg with "
                                     "openssl, gnutls or securetransport enabled.\n");

    return NULL;
}

int ffurl_alloc(URLContext **puc, const char *filename, int flags,
                const AVIOInterruptCB *int_cb)
{
    const URLProtocol *p = NULL;

    p = url_find_protocol(filename);
    if (p)
       return url_alloc_for_protocol(puc, p, filename, flags, int_cb);

    *puc = NULL;
    return AVERROR_PROTOCOL_NOT_FOUND;
}

int ffurl_open_whitelist(URLContext **puc, const char *filename, int flags,
                         const AVIOInterruptCB *int_cb, AVDictionary **options,
                         const char *whitelist, const char* blacklist,
                         URLContext *parent)
{
    AVDictionary *tmp_opts = NULL;
    AVDictionaryEntry *e;
    int ret = ffurl_alloc(puc, filename, flags, int_cb);
    if (ret < 0)
        return ret;
    if (parent) {
        ret = av_opt_copy(*puc, parent);
        if (ret < 0)
            goto fail;
    }
    if (options &&
        (ret = av_opt_set_dict(*puc, options)) < 0)
        goto fail;
    if (options && (*puc)->prot->priv_data_class &&
        (ret = av_opt_set_dict((*puc)->priv_data, options)) < 0)
        goto fail;

    if (!options)
        options = &tmp_opts;

    av_assert0(!whitelist ||
               !(e=av_dict_get(*options, "protocol_whitelist", NULL, 0)) ||
               !strcmp(whitelist, e->value));
    av_assert0(!blacklist ||
               !(e=av_dict_get(*options, "protocol_blacklist", NULL, 0)) ||
               !strcmp(blacklist, e->value));

    if ((ret = av_dict_set(options, "protocol_whitelist", whitelist, 0)) < 0)
        goto fail;

    if ((ret = av_dict_set(options, "protocol_blacklist", blacklist, 0)) < 0)
        goto fail;

    if ((ret = av_opt_set_dict(*puc, options)) < 0)
        goto fail;

    ret = ffurl_connect(*puc, options);

    if (!ret)
        return 0;
fail:
    ffurl_closep(puc);
    return ret;
}
/**
 * @file
 * URL utility functions.
 */

int ff_url_join(char *str, int size, const char *proto,
                const char *authorization, const char *hostname,
                int port, const char *fmt, ...)
{
#if CONFIG_NETWORK
    struct addrinfo hints = { 0 }, *ai;
#endif

    str[0] = '\0';
    if (proto)
        av_strlcatf(str, size, "%s://", proto);
    if (authorization && authorization[0])
        av_strlcatf(str, size, "%s@", authorization);
#if CONFIG_NETWORK && defined(AF_INET6)
    /* Determine if hostname is a numerical IPv6 address,
     * properly escape it within [] in that case. */
    hints.ai_flags = AI_NUMERICHOST;
    if (!getaddrinfo(hostname, NULL, &hints, &ai)) {
        if (ai->ai_family == AF_INET6) {
            av_strlcat(str, "[", size);
            av_strlcat(str, hostname, size);
            av_strlcat(str, "]", size);
        } else {
            av_strlcat(str, hostname, size);
        }
        freeaddrinfo(ai);
    } else
#endif
        /* Not an IPv6 address, just output the plain string. */
        av_strlcat(str, hostname, size);

    if (port >= 0)
        av_strlcatf(str, size, ":%d", port);
    if (fmt) {
        va_list vl;
        size_t len = strlen(str);

        va_start(vl, fmt);
        vsnprintf(str + len, size > len ? size - len : 0, fmt, vl);
        va_end(vl);
    }
    return strlen(str);
}

static const char *find_delim(const char *delim, const char *cur, const char *end)
{
    while (cur < end && !strchr(delim, *cur))
        cur++;
    return cur;
}

int ff_url_decompose(URLComponents *uc, const char *url, const char *end)
{
    const char *cur, *aend, *p;

    av_assert0(url);
    if (!end)
        end = url + strlen(url);
    cur = uc->url = url;

    /* scheme */
    uc->scheme = cur;
    p = find_delim(":/?#", cur, end); /* lavf "schemes" can contain options but not some RFC 3986 delimiters */
    if (*p == ':')
        cur = p + 1;

    /* authority */
    uc->authority = cur;
    if (end - cur >= 2 && cur[0] == '/' && cur[1] == '/') {
        cur += 2;
        aend = find_delim("/?#", cur, end);

        /* userinfo */
        uc->userinfo = cur;
        p = find_delim("@", cur, aend);
        if (*p == '@')
            cur = p + 1;

        /* host */
        uc->host = cur;
        if (*cur == '[') { /* hello IPv6, thanks for using colons! */
            p = find_delim("]", cur, aend);
            if (*p != ']')
                return AVERROR(EINVAL);
            if (p + 1 < aend && p[1] != ':')
                return AVERROR(EINVAL);
            cur = p + 1;
        } else {
            cur = find_delim(":", cur, aend);
        }

        /* port */
        uc->port = cur;
        cur = aend;
    } else {
        uc->userinfo = uc->host = uc->port = cur;
    }

    /* path */
    uc->path = cur;
    cur = find_delim("?#", cur, end);

    /* query */
    uc->query = cur;
    if (*cur == '?')
        cur = find_delim("#", cur, end);

    /* fragment */
    uc->fragment = cur;

    uc->end = end;
    return 0;
}

static int is_fq_dos_path(const char *path)
{
    if ((path[0] >= 'a' && path[0] <= 'z' || path[0] >= 'A' && path[0] <= 'Z') &&
         path[1] == ':' &&
        (path[2] == '/' || path[2] == '\\'))
        return 1;
    if ((path[0] == '/' || path[0] == '\\') &&
        (path[1] == '/' || path[1] == '\\'))
        return 1;
    return 0;
}

static int append_path(char *root, char *out_end, char **rout,
                       const char *in, const char *in_end)
{
    char *out = *rout;
    const char *d, *next;

    if (in < in_end && *in == '/')
        in++; /* already taken care of */
    while (in < in_end) {
        d = find_delim("/", in, in_end);
        next = d + (d < in_end && *d == '/');
        if (d - in == 1 && in[0] == '.') {
            /* skip */
        } else if (d - in == 2 && in[0] == '.' && in[1] == '.') {
            av_assert1(out[-1] == '/');
            if (out - root > 1)
                while (out > root && (--out)[-1] != '/');
        } else {
            if (out_end - out < next - in)
                return AVERROR(ENOMEM);
            memmove(out, in, next - in);
            out += next - in;
        }
        in = next;
    }
    *rout = out;
    return 0;
}

int ff_make_absolute_url2(char *buf, int size, const char *base,
                          const char *rel, int handle_dos_paths)
{
    URLComponents ub, uc;
    char *out, *out_end, *path;
    const char *keep, *base_path_end;
    int use_base_path, simplify_path = 0, ret;
    const char *base_separators = "/";

    /* This is tricky.
       For HTTP, http://server/site/page + ../media/file
       should resolve into http://server/media/file
       but for filesystem access, dir/playlist + ../media/file
       should resolve into dir/../media/file
       because dir could be a symlink, and .. points to
       the actual parent of the target directory.

       We'll consider that URLs with an actual scheme and authority,
       i.e. starting with scheme://, need parent dir simplification,
       while bare paths or pseudo-URLs starting with proto: without
       the double slash do not.

       For real URLs, the processing is similar to the algorithm described
       here:
       https://tools.ietf.org/html/rfc3986#section-5
     */

    if (!size)
        return AVERROR(ENOMEM);
    out = buf;
    out_end = buf + size - 1;

    if (!base)
        base = "";
    if (handle_dos_paths) {
        if ((ret = ff_url_decompose(&ub, base, NULL)) < 0)
            goto error;
        if (is_fq_dos_path(base) || av_strstart(base, "file:", NULL) || ub.path == ub.url) {
            base_separators = "/\\";
            if (is_fq_dos_path(rel))
                base = "";
        }
    }
    if ((ret = ff_url_decompose(&ub, base, NULL)) < 0 ||
        (ret = ff_url_decompose(&uc, rel,  NULL)) < 0)
        goto error;

    keep = ub.url;
#define KEEP(component, also) do { \
        if (uc.url_component_end_##component == uc.url && \
            ub.url_component_end_##component > keep) { \
            keep = ub.url_component_end_##component; \
            also \
        } \
    } while (0)
    KEEP(scheme, );
    KEEP(authority_full, simplify_path = 1;);
    KEEP(path,);
    KEEP(query,);
    KEEP(fragment,);
#undef KEEP
#define COPY(start, end) do { \
        size_t len = end - start; \
        if (len > out_end - out) { \
            ret = AVERROR(ENOMEM);  \
            goto error; \
        } \
        memmove(out, start, len); \
        out += len; \
    } while (0)
    COPY(ub.url, keep);
    COPY(uc.url, uc.path);

    use_base_path = URL_COMPONENT_HAVE(ub, path) && keep <= ub.path;
    if (uc.path > uc.url)
        use_base_path = 0;
    if (URL_COMPONENT_HAVE(uc, path) && uc.path[0] == '/')
        use_base_path = 0;
    if (use_base_path) {
        base_path_end = ub.url_component_end_path;
        if (URL_COMPONENT_HAVE(uc, path))
            while (base_path_end > ub.path && !strchr(base_separators, base_path_end[-1]))
                base_path_end--;
    }
    if (keep > ub.path)
        simplify_path = 0;
    if (URL_COMPONENT_HAVE(uc, scheme))
        simplify_path = 0;
    if (URL_COMPONENT_HAVE(uc, authority))
        simplify_path = 1;
    /* No path at all, leave it */
    if (!use_base_path && !URL_COMPONENT_HAVE(uc, path))
        simplify_path = 0;

    if (simplify_path) {
        const char *root = "/";
        COPY(root, root + 1);
        path = out;
        if (use_base_path) {
            ret = append_path(path, out_end, &out, ub.path, base_path_end);
            if (ret < 0)
                goto error;
        }
        if (URL_COMPONENT_HAVE(uc, path)) {
            ret = append_path(path, out_end, &out, uc.path, uc.url_component_end_path);
            if (ret < 0)
                goto error;
        }
    } else {
        if (use_base_path)
            COPY(ub.path, base_path_end);
        COPY(uc.path, uc.url_component_end_path);
    }

    COPY(uc.url_component_end_path, uc.end);
#undef COPY
    *out = 0;
    return 0;

error:
    snprintf(buf, size, "invalid:%s",
             ret == AVERROR(ENOMEM) ? "truncated" :
             ret == AVERROR(EINVAL) ? "syntax_error" : "");
    return ret;
}

int ff_make_absolute_url(char *buf, int size, const char *base,
                         const char *rel)
{
    return ff_make_absolute_url2(buf, size, base, rel, HAVE_DOS_PATHS);
}

AVIODirEntry *ff_alloc_dir_entry(void)
{
    AVIODirEntry *entry = av_mallocz(sizeof(AVIODirEntry));
    if (entry) {
        entry->type = AVIO_ENTRY_UNKNOWN;
        entry->size = -1;
        entry->modification_timestamp = -1;
        entry->access_timestamp = -1;
        entry->status_change_timestamp = -1;
        entry->user_id = -1;
        entry->group_id = -1;
        entry->filemode = -1;
    }
    return entry;
}


int64_t ffurl_size(URLContext *h)
{
    int64_t pos, size;

    size = ffurl_seek(h, 0, AVSEEK_SIZE);
    if (size < 0) {
        pos = ffurl_seek(h, 0, SEEK_CUR);
        if ((size = ffurl_seek(h, -1, SEEK_END)) < 0)
            return size;
        size++;
        ffurl_seek(h, pos, SEEK_SET);
    }
    return size;
}
//for tcp fd
int ffurl_get_file_handle(URLContext *h)
{
    if (!h || !h->prot || !h->prot->url_get_file_handle)
        return -1;
    return h->prot->url_get_file_handle(h);
}

int ffurl_get_multi_file_handle(URLContext *h, int **handles, int *numhandles)
{
    if (!h || !h->prot)
        return AVERROR(ENOSYS);
    if (!h->prot->url_get_multi_file_handle) {
        if (!h->prot->url_get_file_handle)
            return AVERROR(ENOSYS);
        *handles = av_malloc(sizeof(**handles));
        if (!*handles)
            return AVERROR(ENOMEM);
        *numhandles = 1;
        *handles[0] = h->prot->url_get_file_handle(h);
        return 0;
    }
    return h->prot->url_get_multi_file_handle(h, handles, numhandles);
}

int ffurl_get_short_seek(URLContext *h)
{
    if (!h || !h->prot || !h->prot->url_get_short_seek)
        return AVERROR(ENOSYS);
    return h->prot->url_get_short_seek(h);
}

int ffurl_shutdown(URLContext *h, int flags)
{
    if (!h || !h->prot || !h->prot->url_shutdown)
        return AVERROR(ENOSYS);
    return h->prot->url_shutdown(h, flags);
}

int ffurl_move(const char *url_src, const char *url_dst)
{
    URLContext *h_src, *h_dst;
    int ret = ffurl_alloc(&h_src, url_src, AVIO_FLAG_READ_WRITE, NULL);
    if (ret < 0)
        return ret;
    ret = ffurl_alloc(&h_dst, url_dst, AVIO_FLAG_WRITE, NULL);
    if (ret < 0) {
        ffurl_close(h_src);
        return ret;
    }

    if (h_src->prot == h_dst->prot && h_src->prot->url_move)
        ret = h_src->prot->url_move(h_src, h_dst);
    else
        ret = AVERROR(ENOSYS);

    ffurl_close(h_src);
    ffurl_close(h_dst);
    return ret;
}

int ffurl_delete(const char *url)
{
    URLContext *h;
    int ret = ffurl_alloc(&h, url, AVIO_FLAG_WRITE, NULL);
    if (ret < 0)
        return ret;

    if (h->prot->url_delete)
        ret = h->prot->url_delete(h);
    else
        ret = AVERROR(ENOSYS);

    ffurl_close(h);
    return ret;
}
//读写封装
static inline int retry_transfer_wrapper(URLContext *h, uint8_t *buf,
                                         int size, int size_min,
                                         int (*transfer_func)(URLContext *h,
                                                              uint8_t *buf,
                                                              int size))
{
    int ret, len;
    int fast_retries = 5;
    int64_t wait_since = 0;

    len = 0;
    while (len < size_min) {
        if (ff_check_interrupt(&h->interrupt_callback))
            return AVERROR_EXIT;
        ret = transfer_func(h, buf + len, size - len);
        if (ret == AVERROR(EINTR))
            continue;
        if (h->flags & AVIO_FLAG_NONBLOCK)
            return ret;
        if (ret == AVERROR(EAGAIN)) {
            ret = 0;
            if (fast_retries) {
                fast_retries--;
            } else {
                if (h->rw_timeout) {
                    if (!wait_since)
                        wait_since = av_gettime_relative();
                    else if (av_gettime_relative() > wait_since + h->rw_timeout)
                        return AVERROR(EIO);
                }
                av_usleep(1000);
            }
        } else if (ret == AVERROR_EOF)
            return (len > 0) ? len : AVERROR_EOF;
        else if (ret < 0)
            return ret;
        if (ret) {
            fast_retries = FFMAX(fast_retries, 2);
            wait_since = 0;
        }
        len += ret;
    }
    return len;
}

int ffurl_read(URLContext *h, unsigned char *buf, int size)
{
    if (!(h->flags & AVIO_FLAG_READ))
        return AVERROR(EIO);
    return retry_transfer_wrapper(h, buf, size, 1, h->prot->url_read);
}

int ffurl_read_complete(URLContext *h, unsigned char *buf, int size)
{
    if (!(h->flags & AVIO_FLAG_READ))
        return AVERROR(EIO);
    return retry_transfer_wrapper(h, buf, size, size, h->prot->url_read);
}

int ffurl_write(URLContext *h, const unsigned char *buf, int size)
{
    if (!(h->flags & AVIO_FLAG_WRITE))
        return AVERROR(EIO);
    /* avoid sending too big packets */
    if (h->max_packet_size && size > h->max_packet_size)
        return AVERROR(EIO);

    return retry_transfer_wrapper(h, (unsigned char *)buf, size, size,
                                  (int (*)(struct URLContext *, uint8_t *, int))
                                  h->prot->url_write);
}

int64_t ffurl_seek(URLContext *h, int64_t pos, int whence)
{
    int64_t ret;

    if (!h->prot->url_seek)
        return AVERROR(ENOSYS);
    ret = h->prot->url_seek(h, pos, whence & ~AVSEEK_FORCE);
    return ret;
}

int ffurl_closep(URLContext **hh)
{
    URLContext *h= *hh;
    int ret = 0;
    if (!h)
        return 0;     /* can happen when ffurl_open fails */

    if (h->is_connected && h->prot->url_close)
        ret = h->prot->url_close(h);
#if CONFIG_NETWORK
    if (h->prot->flags & URL_PROTOCOL_FLAG_NETWORK)
        ff_network_close();
#endif
    if (h->prot->priv_data_size) {
        if (h->prot->priv_data_class)
            av_opt_free(h->priv_data);
        av_freep(&h->priv_data);
    }
    av_opt_free(h);
    av_freep(hh);
    return ret;
}

int ffurl_close(URLContext *h)
{
    return ffurl_closep(&h);
}

const char *avio_find_protocol_name(const char *url)
{
    const URLProtocol *p = url_find_protocol(url);

    return p ? p->name : NULL;
}
