#define _GNU_SOURCE
#include "engine.h"
#include "hash.h"
#include "ws.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <curl/curl.h>
#include "quickjs.h"

#define SJ_UA "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0"

struct sj_plugin {
    JSRuntime *rt;
    JSContext *ctx;
    CURL *curl;
    char **hosts;
    int nhosts;
    char *cookies;
    char **auth_headers;
    int nauth;
    char *clearance; // cookies of a verification passed in the browser: sent with every request
    char *ua;        // ...and the browser's User-Agent they are tied to
    sj_log_fn log;
    void *log_user;
};

typedef struct {
    char *data;
    size_t len, cap;
} sj_buf;

static void buf_add(sj_buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 16384;
        while (cap < b->len + n + 1)
            cap *= 2;
        b->data = realloc(b->data, cap);
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void sj_logf(sj_plugin *p, const char *fmt, ...)
{
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (p->log)
        p->log(msg, p->log_user);
}

char *sj_read_file(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc(n + 1);
    if (fread(s, 1, n, f) != (size_t)n) {
        fclose(f);
        free(s);
        return NULL;
    }
    fclose(f);
    s[n] = 0;
    if (len)
        *len = n;
    return s;
}

static char *host_of(const char *url)
{
    const char *s = strstr(url, "://");
    if (!s)
        return NULL;
    s += 3;
    const char *e = s + strcspn(s, "/?#");
    const char *at = memchr(s, '@', e - s);
    if (at)
        s = at + 1;
    const char *colon = memchr(s, ':', e - s);
    if (colon)
        e = colon;
    char *h = strndup(s, e - s);
    for (char *c = h; *c; c++)
        *c = tolower((unsigned char)*c);
    return h;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !strcmp(s + a - b, suffix);
}

static int host_allowed(sj_plugin *p, const char *url)
{
    char *h = host_of(url);
    if (!h)
        return 0;
    int ok = 0;
    for (int i = 0; i < p->nhosts && !ok; i++) {
        const char *a = p->hosts[i];
        if (!strcmp(a, "everywhere"))
            ok = 1;
        else if (a[0] == '.')
            ok = ends_with(h, a) || !strcmp(h, a + 1);
        else if (!strcmp(h, a))
            ok = 1;
        else {
            size_t hl = strlen(h), al = strlen(a);
            ok = hl > al && h[hl - al - 1] == '.' && !strcmp(h + hl - al, a);
        }
    }
    free(h);
    return ok;
}

typedef struct {
    JSContext *ctx;
    JSValue headers;
} hdr_state;

static size_t on_body(char *ptr, size_t size, size_t n, void *ud)
{
    buf_add(ud, ptr, size * n);
    return size * n;
}

static size_t on_header(char *ptr, size_t size, size_t n, void *ud)
{
    hdr_state *hs = ud;
    size_t len = size * n;
    if (len >= 5 && !strncmp(ptr, "HTTP/", 5)) {
        JS_FreeValue(hs->ctx, hs->headers);
        hs->headers = JS_NewObject(hs->ctx);
        return len;
    }
    char *colon = memchr(ptr, ':', len);
    if (!colon)
        return len;
    char *name = strndup(ptr, colon - ptr);
    for (char *c = name; *c; c++)
        *c = tolower((unsigned char)*c);
    const char *v = colon + 1;
    const char *end = ptr + len;
    while (v < end && (*v == ' ' || *v == '\t'))
        v++;
    while (end > v && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' '))
        end--;
    JSValue arr = JS_GetPropertyStr(hs->ctx, hs->headers, name);
    if (!JS_IsArray(arr)) {
        JS_FreeValue(hs->ctx, arr);
        arr = JS_NewArray(hs->ctx);
        JS_SetPropertyStr(hs->ctx, hs->headers, name, JS_DupValue(hs->ctx, arr));
    }
    int64_t idx = 0;
    JS_GetLength(hs->ctx, arr, &idx);
    JS_SetPropertyUint32(hs->ctx, arr, (uint32_t)idx, JS_NewStringLen(hs->ctx, v, end - v));
    JS_FreeValue(hs->ctx, arr);
    free(name);
    return len;
}

static char *cookie_value(const char *cookies, const char *name)
{
    size_t nl = strlen(name);
    const char *p = cookies;
    while (p && *p) {
        while (*p == ' ' || *p == ';')
            p++;
        if (!strncmp(p, name, nl) && p[nl] == '=') {
            const char *v = p + nl + 1;
            return strndup(v, strcspn(v, ";"));
        }
        p = strchr(p, ';');
    }
    return NULL;
}

static char *sapisid_header(const char *cookies, const char *origin)
{
    static const char *names[3][2] = {{"SAPISID", "SAPISIDHASH"},
                                      {"__Secure-1PAPISID", "SAPISID1PHASH"},
                                      {"__Secure-3PAPISID", "SAPISID3PHASH"}};
    char *out = NULL;
    long ts = (long)time(NULL);
    for (int i = 0; i < 3; i++) {
        char *v = cookie_value(cookies, names[i][0]);
        if (!v && i == 0)
            v = cookie_value(cookies, "__Secure-3PAPISID");
        if (!v)
            continue;
        char *msg, hex[41];
        if (asprintf(&msg, "%ld %s %s", ts, v, origin) >= 0) {
            sha1_hex(msg, strlen(msg), hex);
            free(msg);
            char *prev = out;
            if (asprintf(&out, "%s%s%s %ld_%s", prev ? prev : "", prev ? " " : "", names[i][1], ts, hex) < 0)
                out = prev;
            else
                free(prev);
        }
        free(v);
    }
    return out;
}

char *sj_sapisid_header(const char *cookies, const char *origin)
{
    return sapisid_header(cookies, origin);
}

static JSValue make_response(JSContext *ctx, int code, const char *body, size_t blen, const char *url,
                             JSValue headers)
{
    JSValue r = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, r, "code", JS_NewInt32(ctx, code));
    JS_SetPropertyStr(ctx, r, "body", JS_NewStringLen(ctx, body ? body : "", body ? blen : 0));
    JS_SetPropertyStr(ctx, r, "url", JS_NewString(ctx, url));
    JS_SetPropertyStr(ctx, r, "headers", headers);
    return r;
}

static JSValue js_http(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    sj_plugin *p = JS_GetContextOpaque(ctx);
    const char *method = JS_ToCString(ctx, argv[0]);
    const char *url = JS_ToCString(ctx, argv[1]);
    int use_auth = argc > 4 && JS_ToBool(ctx, argv[4]);
    JSValue result;

    if (!host_allowed(p, url)) {
        sj_logf(p, "blocked %s", url);
        result = make_response(ctx, 0, "Blocked: host not allowed by plugin config", 42, url,
                               JS_NewObject(ctx));
        goto done;
    }

    const char *body = NULL;
    size_t body_len = 0;
    JSValue bin = JS_UNDEFINED;
    if (JS_IsString(argv[2])) {
        body = JS_ToCStringLen(ctx, &body_len, argv[2]);
    } else if (JS_IsObject(argv[2])) {
        bin = JS_GetPropertyStr(ctx, argv[2], "binary");
        const char *s = JS_ToCStringLen(ctx, &body_len, bin);
        char *raw = malloc(body_len + 1);
        size_t o = 0;
        for (size_t i = 0; i < body_len; o++) {
            unsigned char c = s[i];
            if (c < 0x80) {
                raw[o] = c;
                i++;
            } else {
                raw[o] = ((c & 0x1f) << 6) | (s[i + 1] & 0x3f);
                i += 2;
            }
        }
        JS_FreeCString(ctx, s);
        body = raw;
        body_len = o;
    }

    struct curl_slist *hl = NULL;
    int has_ua = 0, has_ct = 0, has_auth = 0;
    char origin[128] = "https://www.youtube.com";
    char *cookie_extra = NULL;
    JSPropertyEnum *tab = NULL;
    uint32_t n = 0;
    if (JS_IsObject(argv[3]) &&
        !JS_GetOwnPropertyNames(ctx, &tab, &n, argv[3], JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
        for (uint32_t i = 0; i < n; i++) {
            const char *k = JS_AtomToCString(ctx, tab[i].atom);
            JSValue v = JS_GetProperty(ctx, argv[3], tab[i].atom);
            if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
                const char *vs = JS_ToCString(ctx, v);
                if (!strcasecmp(k, "accept-encoding") || (!strcasecmp(k, "user-agent") && p->ua)) {
                } else if (!strcasecmp(k, "cookie")) {
                    if (asprintf(&cookie_extra, "%s", vs) < 0)
                        cookie_extra = NULL;
                } else {
                    char *line;
                    if (asprintf(&line, "%s: %s", k, vs) >= 0) {
                        hl = curl_slist_append(hl, line);
                        free(line);
                    }
                    if (!strcasecmp(k, "user-agent"))
                        has_ua = 1;
                    if (!strcasecmp(k, "content-type"))
                        has_ct = 1;
                    if (!strcasecmp(k, "authorization"))
                        has_auth = 1;
                    if (!strcasecmp(k, "x-origin") || !strcasecmp(k, "origin"))
                        snprintf(origin, sizeof origin, "%s", vs);
                }
                JS_FreeCString(ctx, vs);
            }
            JS_FreeValue(ctx, v);
            JS_FreeCString(ctx, k);
        }
        JS_FreePropertyEnum(ctx, tab, n);
    }
    if (use_auth) {
        for (int i = 0; i < p->nauth; i++) {
            hl = curl_slist_append(hl, p->auth_headers[i]);
            if (!strncasecmp(p->auth_headers[i], "authorization:", 14))
                has_auth = 1;
        }
        char *host = host_of(url);
        if (!has_auth && p->cookies && host && ends_with(host, "youtube.com")) {
            char *sap = sapisid_header(p->cookies, origin);
            if (sap) {
                char *line;
                if (asprintf(&line, "Authorization: %s", sap) >= 0) {
                    hl = curl_slist_append(hl, line);
                    free(line);
                }
                free(sap);
            }
        }
        free(host);
    }
    {
        const char *parts[3] = {use_auth ? p->cookies : NULL, cookie_extra, p->clearance};
        sj_buf ck = {0};
        for (int i = 0; i < 3; i++)
            if (parts[i] && *parts[i]) {
                buf_add(&ck, ck.len ? "; " : "Cookie: ", ck.len ? 2 : 8);
                buf_add(&ck, parts[i], strlen(parts[i]));
            }
        if (ck.len)
            hl = curl_slist_append(hl, ck.data);
        free(ck.data);
    }
    free(cookie_extra);
    if (p->ua) {
        char *line;
        if (asprintf(&line, "User-Agent: %s", p->ua) >= 0) {
            hl = curl_slist_append(hl, line);
            free(line);
        }
        has_ua = 1;
    }
    if (!has_ua)
        hl = curl_slist_append(hl, "User-Agent: " SJ_UA);
    hl = curl_slist_append(hl, "Expect:");
    if (!has_ct && body) {
        size_t i = 0;
        while (i < body_len && isspace((unsigned char)body[i]))
            i++;
        if (i < body_len && (body[i] == '{' || body[i] == '['))
            hl = curl_slist_append(hl, "Content-Type: application/json");
        else
            hl = curl_slist_append(hl, "Content-Type:");
    }

    CURL *c = p->curl;
    curl_easy_reset(c);
    sj_buf out = {0};
    hdr_state hs = {ctx, JS_NewObject(ctx)};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &hs);
    if (!strcmp(method, "GET")) {
        curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    } else if (!strcmp(method, "HEAD")) {
        curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    } else {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
        if (body || !strcmp(method, "POST")) {
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, body ? body : "");
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body_len);
        }
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CURLcode rc = curl_easy_perform(c);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long code = 0;
    char *eff = NULL;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &eff);
    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    sj_logf(p, "http %s %ld %.0fms %zuB %.120s", method, code, ms, out.len, url);
    if (code >= 400 && out.data)
        sj_logf(p, "http body: %.400s", out.data);
    if (getenv("SJ_TRACE"))
        sj_logf(p, "http req body: %.600s", body ? body : "");
    if (rc != CURLE_OK) {
        JS_FreeValue(ctx, hs.headers);
        const char *msg = curl_easy_strerror(rc);
        result = make_response(ctx, 0, msg, strlen(msg), url, JS_NewObject(ctx));
    } else {
        result = make_response(ctx, (int)code, out.data, out.len, eff ? eff : url, hs.headers);
    }
    free(out.data);
    curl_slist_free_all(hl);
    if (JS_IsObject(argv[2]))
        free((char *)body);
    else if (body)
        JS_FreeCString(ctx, body);
    JS_FreeValue(ctx, bin);
done:
    JS_FreeCString(ctx, method);
    JS_FreeCString(ctx, url);
    return result;
}

static JSValue js_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    sj_plugin *p = JS_GetContextOpaque(ctx);
    const char *s = JS_ToCString(ctx, argv[0]);
    if (s && p->log)
        p->log(s, p->log_user);
    JS_FreeCString(ctx, s);
    return JS_UNDEFINED;
}

static JSValue js_logged_in(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    sj_plugin *p = JS_GetContextOpaque(ctx);
    return JS_NewBool(ctx, p->cookies != NULL || p->nauth > 0);
}

static JSValue js_sleep(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    double ms = 0;
    JS_ToFloat64(ctx, &ms, argv[0]);
    if (ms > 10000)
        ms = 10000;
    if (ms > 0) {
        struct timespec ts = {(time_t)(ms / 1000), (long)((ms - (long)(ms / 1000) * 1000) * 1e6)};
        nanosleep(&ts, NULL);
    }
    return JS_UNDEFINED;
}

static JSValue js_md5(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    size_t len;
    const char *s = JS_ToCStringLen(ctx, &len, argv[0]);
    char hex[33];
    md5_hex(s, len, hex);
    JS_FreeCString(ctx, s);
    return JS_NewString(ctx, hex);
}

static JSValue js_socket(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    const char *op = JS_ToCString(ctx, argv[0]);
    const char *a = argc > 1 ? JS_ToCString(ctx, argv[1]) : NULL;
    const char *b = argc > 2 ? JS_ToCString(ctx, argv[2]) : NULL;
    JSValue r;
    if (!strcmp(op, "open")) {
        char id[16];
        snprintf(id, sizeof id, "%d", a ? ws_open(a, b) : 0);
        r = JS_NewString(ctx, id);
    } else if (!strcmp(op, "poll")) {
        char *s = ws_poll(a ? atoi(a) : 0);
        r = JS_NewString(ctx, s);
        free(s);
    } else {
        if (!strcmp(op, "send") && a && b)
            ws_send(atoi(a), b);
        else if (!strcmp(op, "close") && a)
            ws_close(atoi(a));
        r = JS_NewString(ctx, "");
    }
    JS_FreeCString(ctx, op);
    if (a)
        JS_FreeCString(ctx, a);
    if (b)
        JS_FreeCString(ctx, b);
    return r;
}

static char *dup_exception(JSContext *ctx)
{
    JSValue e = JS_GetException(ctx);
    const char *msg = JS_ToCString(ctx, e);
    JSValue stack = JS_GetPropertyStr(ctx, e, "stack");
    const char *st = JS_IsString(stack) ? JS_ToCString(ctx, stack) : NULL;
    char *out;
    if (asprintf(&out, "%s%s%s", msg ? msg : "error", st ? "\n" : "", st ? st : "") < 0)
        out = strdup("error");
    JS_FreeCString(ctx, msg);
    if (st)
        JS_FreeCString(ctx, st);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, e);
    return out;
}

static char *error_json(const char *type, const char *msg)
{
    sj_buf b = {0};
    buf_add(&b, "{\"__error\":true,\"type\":\"", 24);
    buf_add(&b, type, strlen(type));
    buf_add(&b, "\",\"message\":\"", 13);
    for (const char *c = msg; *c; c++) {
        char esc[8];
        if (*c == '"' || *c == '\\') {
            esc[0] = '\\';
            esc[1] = *c;
            buf_add(&b, esc, 2);
        } else if ((unsigned char)*c < 0x20) {
            snprintf(esc, sizeof esc, "\\u%04x", *c);
            buf_add(&b, esc, 6);
        } else {
            buf_add(&b, c, 1);
        }
    }
    buf_add(&b, "\"}", 2);
    return b.data;
}

static void run_jobs(sj_plugin *p)
{
    JSContext *jc;
    while (JS_IsJobPending(p->rt)) {
        if (JS_ExecutePendingJob(p->rt, &jc) < 0) {
            char *e = dup_exception(jc);
            sj_logf(p, "job failed: %s", e);
            free(e);
        }
    }
}

static char *value_to_string(sj_plugin *p, JSValue v)
{
    if (JS_IsException(v)) {
        char *e = dup_exception(p->ctx);
        char *r = error_json("Exception", e);
        free(e);
        return r;
    }
    if (!JS_IsString(v)) {
        JS_FreeValue(p->ctx, v);
        return NULL;
    }
    const char *s = JS_ToCString(p->ctx, v);
    char *r = strdup(s);
    JS_FreeCString(p->ctx, s);
    JS_FreeValue(p->ctx, v);
    return r;
}

static JSValue call_global(sj_plugin *p, const char *fn, int argc, JSValueConst *argv)
{
    JSValue g = JS_GetGlobalObject(p->ctx);
    JSValue f = JS_GetPropertyStr(p->ctx, g, fn);
    JSValue r = JS_Call(p->ctx, f, g, argc, argv);
    JS_FreeValue(p->ctx, f);
    JS_FreeValue(p->ctx, g);
    return r;
}

static char *settle(sj_plugin *p, char *r)
{
    run_jobs(p);
    if (!r || strncmp(r, "{\"__async\":", 11))
        return r;
    int id = atoi(r + 11);
    free(r);
    for (int guard = 0; guard < 100000; guard++) {
        run_jobs(p);
        JSValue a = JS_NewInt32(p->ctx, id);
        JSValue v = call_global(p, "__sansonAsyncResult", 1, &a);
        if (JS_IsString(v) || JS_IsException(v))
            return value_to_string(p, v);
        JS_FreeValue(p->ctx, v);
        JSValue t = call_global(p, "__sansonTick", 0, NULL);
        int more = JS_ToBool(p->ctx, t);
        JS_FreeValue(p->ctx, t);
        if (!more && !JS_IsJobPending(p->rt))
            break;
    }
    return error_json("AsyncNeverSettled", "promise did not settle");
}

static char *call_str(sj_plugin *p, const char *fn, int argc, JSValueConst *argv)
{
    JSValue r = call_global(p, fn, argc, argv);
    for (int i = 0; i < argc; i++)
        JS_FreeValue(p->ctx, (JSValue)argv[i]);
    return settle(p, value_to_string(p, r));
}

static int eval_file(sj_plugin *p, const char *path, char **error)
{
    long len;
    char *src = sj_read_file(path, &len);
    if (!src)
        return 1;
    JSValue r = JS_Eval(p->ctx, src, len, path, JS_EVAL_TYPE_GLOBAL);
    free(src);
    if (JS_IsException(r)) {
        char *e = dup_exception(p->ctx);
        if (error)
            *error = e;
        else
            free(e);
        return -1;
    }
    JS_FreeValue(p->ctx, r);
    run_jobs(p);
    return 0;
}

static void free_auth(sj_plugin *p)
{
    free(p->cookies);
    p->cookies = NULL;
    for (int i = 0; i < p->nauth; i++)
        free(p->auth_headers[i]);
    free(p->auth_headers);
    p->auth_headers = NULL;
    p->nauth = 0;
}

void sj_set_clearance(sj_plugin *p, const char *cookies, const char *ua)
{
    free(p->clearance);
    free(p->ua);
    p->clearance = cookies && *cookies ? strdup(cookies) : NULL;
    p->ua = ua && *ua ? strdup(ua) : NULL;
}

void sj_set_auth(sj_plugin *p, const char *auth_json)
{
    free_auth(p);
    if (!auth_json || !*auth_json)
        return;
    JSValue a = JS_ParseJSON(p->ctx, auth_json, strlen(auth_json), "auth");
    if (!JS_IsObject(a)) {
        JS_FreeValue(p->ctx, a);
        return;
    }
    JSValue c = JS_GetPropertyStr(p->ctx, a, "cookies");
    if (JS_IsString(c)) {
        const char *s = JS_ToCString(p->ctx, c);
        if (*s)
            p->cookies = strdup(s);
        JS_FreeCString(p->ctx, s);
    }
    JS_FreeValue(p->ctx, c);
    JSValue h = JS_GetPropertyStr(p->ctx, a, "headers");
    JSPropertyEnum *tab;
    uint32_t n;
    if (JS_IsObject(h) &&
        !JS_GetOwnPropertyNames(p->ctx, &tab, &n, h, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
        p->auth_headers = calloc(n ? n : 1, sizeof(char *));
        for (uint32_t i = 0; i < n; i++) {
            const char *k = JS_AtomToCString(p->ctx, tab[i].atom);
            JSValue v = JS_GetProperty(p->ctx, h, tab[i].atom);
            const char *vs = JS_ToCString(p->ctx, v);
            if (asprintf(&p->auth_headers[p->nauth], "%s: %s", k, vs) >= 0)
                p->nauth++;
            JS_FreeCString(p->ctx, vs);
            JS_FreeValue(p->ctx, v);
            JS_FreeCString(p->ctx, k);
        }
        JS_FreePropertyEnum(p->ctx, tab, n);
    }
    JS_FreeValue(p->ctx, h);
    JS_FreeValue(p->ctx, a);
}

static char *default_settings(sj_plugin *p, JSValue cfg)
{
    JSValue out = JS_NewObject(p->ctx);
    JSValue arr = JS_GetPropertyStr(p->ctx, cfg, "settings");
    int64_t n = 0;
    if (JS_IsArray(arr))
        JS_GetLength(p->ctx, arr, &n);
    for (int64_t i = 0; i < n; i++) {
        JSValue s = JS_GetPropertyUint32(p->ctx, arr, (uint32_t)i);
        JSValue var = JS_GetPropertyStr(p->ctx, s, "variable");
        JSValue type = JS_GetPropertyStr(p->ctx, s, "type");
        JSValue def = JS_GetPropertyStr(p->ctx, s, "default");
        const char *vn = JS_ToCString(p->ctx, var);
        const char *tn = JS_ToCString(p->ctx, type);
        const char *dn = JS_IsUndefined(def) ? NULL : JS_ToCString(p->ctx, def);
        if (vn) {
            if (tn && !strcmp(tn, "Boolean"))
                JS_SetPropertyStr(p->ctx, out, vn, JS_NewBool(p->ctx, dn && !strcmp(dn, "true")));
            else
                JS_SetPropertyStr(p->ctx, out, vn, JS_NewString(p->ctx, dn ? dn : ""));
        }
        JS_FreeCString(p->ctx, vn);
        JS_FreeCString(p->ctx, tn);
        if (dn)
            JS_FreeCString(p->ctx, dn);
        JS_FreeValue(p->ctx, var);
        JS_FreeValue(p->ctx, type);
        JS_FreeValue(p->ctx, def);
        JS_FreeValue(p->ctx, s);
    }
    JS_FreeValue(p->ctx, arr);
    JSValue str = JS_JSONStringify(p->ctx, out, JS_UNDEFINED, JS_UNDEFINED);
    JS_FreeValue(p->ctx, out);
    return value_to_string(p, str);
}

static char *merge_settings(sj_plugin *p, const char *defaults, const char *over)
{
    if (!over || !*over)
        return strdup(defaults);
    JSValue d = JS_ParseJSON(p->ctx, defaults, strlen(defaults), "settings");
    JSValue o = JS_ParseJSON(p->ctx, over, strlen(over), "settings");
    JSPropertyEnum *tab;
    uint32_t n;
    if (JS_IsObject(o) &&
        !JS_GetOwnPropertyNames(p->ctx, &tab, &n, o, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
        for (uint32_t i = 0; i < n; i++)
            JS_SetProperty(p->ctx, d, tab[i].atom, JS_GetProperty(p->ctx, o, tab[i].atom));
        JS_FreePropertyEnum(p->ctx, tab, n);
    }
    JS_FreeValue(p->ctx, o);
    JSValue str = JS_JSONStringify(p->ctx, d, JS_UNDEFINED, JS_UNDEFINED);
    JS_FreeValue(p->ctx, d);
    return value_to_string(p, str);
}

sj_plugin *sj_open(const char *dir, const char *host_js, const char *settings_json,
                   const char *auth_json, const char *save_state, sj_log_fn log, void *log_user,
                   char **error)
{
    static int curl_ready;
    if (!curl_ready) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_ready = 1;
    }
    sj_plugin *p = calloc(1, sizeof *p);
    p->log = log;
    p->log_user = log_user;
    p->rt = JS_NewRuntime();
    JS_SetMaxStackSize(p->rt, 6 * 1024 * 1024);
    p->ctx = JS_NewContext(p->rt);
    JS_SetContextOpaque(p->ctx, p);
    p->curl = curl_easy_init();

    JSValue g = JS_GetGlobalObject(p->ctx);
    JSValue ns = JS_NewObject(p->ctx);
    JS_SetPropertyStr(p->ctx, ns, "http", JS_NewCFunction(p->ctx, js_http, "http", 5));
    JS_SetPropertyStr(p->ctx, ns, "log", JS_NewCFunction(p->ctx, js_log, "log", 1));
    JS_SetPropertyStr(p->ctx, ns, "loggedIn", JS_NewCFunction(p->ctx, js_logged_in, "loggedIn", 0));
    JS_SetPropertyStr(p->ctx, ns, "sleep", JS_NewCFunction(p->ctx, js_sleep, "sleep", 1));
    JS_SetPropertyStr(p->ctx, ns, "md5", JS_NewCFunction(p->ctx, js_md5, "md5", 1));
    JS_SetPropertyStr(p->ctx, ns, "socket", JS_NewCFunction(p->ctx, js_socket, "socket", 3));
    JS_SetPropertyStr(p->ctx, g, "__sanson", ns);
    JS_FreeValue(p->ctx, g);

    char path[4096];
    snprintf(path, sizeof path, "%s/config.json", dir);
    long clen;
    char *cfg_text = sj_read_file(path, &clen);
    if (!cfg_text) {
        if (error)
            *error = strdup("config.json not found");
        sj_close(p);
        return NULL;
    }
    JSValue cfg = JS_ParseJSON(p->ctx, cfg_text, clen, "config.json");
    JSValue urls = JS_GetPropertyStr(p->ctx, cfg, "allowUrls");
    int64_t n = 0;
    if (JS_IsArray(urls))
        JS_GetLength(p->ctx, urls, &n);
    p->hosts = calloc(n ? n : 1, sizeof(char *));
    for (int64_t i = 0; i < n; i++) {
        JSValue u = JS_GetPropertyUint32(p->ctx, urls, (uint32_t)i);
        const char *s = JS_ToCString(p->ctx, u);
        p->hosts[p->nhosts] = strdup(s);
        for (char *c = p->hosts[p->nhosts]; *c; c++)
            *c = tolower((unsigned char)*c);
        p->nhosts++;
        JS_FreeCString(p->ctx, s);
        JS_FreeValue(p->ctx, u);
    }
    JS_FreeValue(p->ctx, urls);
    sj_set_auth(p, auth_json);

    char *err = NULL;
    if (eval_file(p, host_js, &err)) {
        if (error)
            *error = err ? err : strdup("host script not found");
        JS_FreeValue(p->ctx, cfg);
        free(cfg_text);
        sj_close(p);
        return NULL;
    }
    // Files the plugin shares with others (plugins/comun.js),
    // evaluated before its script.
    JSValue inc = JS_GetPropertyStr(p->ctx, cfg, "sansonIncludes");
    int64_t ninc = 0;
    if (JS_IsArray(inc))
        JS_GetLength(p->ctx, inc, &ninc);
    for (int64_t i = 0; i < ninc; i++) {
        JSValue f = JS_GetPropertyUint32(p->ctx, inc, (uint32_t)i);
        const char *name = JS_ToCString(p->ctx, f);
        int bad = 0;
        if (name) {
            snprintf(path, sizeof path, "%s/%s", dir, name);
            bad = eval_file(p, path, &err) != 0;
            JS_FreeCString(p->ctx, name);
        }
        JS_FreeValue(p->ctx, f);
        if (bad) {
            if (error)
                *error = err ? err : strdup("include not found");
            else
                free(err);
            JS_FreeValue(p->ctx, inc);
            JS_FreeValue(p->ctx, cfg);
            free(cfg_text);
            sj_close(p);
            return NULL;
        }
    }
    JS_FreeValue(p->ctx, inc);
    snprintf(path, sizeof path, "%s/script.js", dir);
    if (eval_file(p, path, &err)) {
        if (error)
            *error = err ? err : strdup("script.js not found");
        JS_FreeValue(p->ctx, cfg);
        free(cfg_text);
        sj_close(p);
        return NULL;
    }
    snprintf(path, sizeof path, "%s/sanson_extra.js", dir);
    if (eval_file(p, path, &err) < 0) {
        sj_logf(p, "sanson_extra.js failed: %s", err);
        free(err);
    }

    char *defs = default_settings(p, cfg);
    char *settings = merge_settings(p, defs ? defs : "{}", settings_json);
    JSValue args[3] = {JS_NewString(p->ctx, cfg_text), JS_NewString(p->ctx, settings),
                       save_state ? JS_NewString(p->ctx, save_state) : JS_NULL};
    char *r = call_str(p, "__sansonEnable", 3, args);
    free(defs);
    free(settings);
    JS_FreeValue(p->ctx, cfg);
    free(cfg_text);
    if (!r || strstr(r, "\"__error\"")) {
        if (error)
            *error = r ? r : strdup("enable failed");
        else
            free(r);
        sj_close(p);
        return NULL;
    }
    free(r);
    return p;
}

char *sj_call(sj_plugin *p, const char *name, const char *args_json)
{
    JSValue args[2] = {JS_NewString(p->ctx, name), JS_NewString(p->ctx, args_json ? args_json : "[]")};
    return call_str(p, "__sansonCall", 2, args);
}

char *sj_next(sj_plugin *p, int pager, const char *arg_json)
{
    JSValue arg = JS_UNDEFINED;
    if (arg_json && *arg_json)
        arg = JS_ParseJSON(p->ctx, arg_json, strlen(arg_json), "arg");
    JSValue args[2] = {JS_NewInt32(p->ctx, pager), arg};
    return call_str(p, "__sansonNext", 2, args);
}

char *sj_pager_call(sj_plugin *p, int pager, const char *name, const char *args_json)
{
    JSValue args[3] = {JS_NewInt32(p->ctx, pager), JS_NewString(p->ctx, name),
                       JS_NewString(p->ctx, args_json ? args_json : "[]")};
    return call_str(p, "__sansonPagerCall", 3, args);
}

char *sj_drop(sj_plugin *p, int pager)
{
    JSValue a = JS_NewInt32(p->ctx, pager);
    return call_str(p, "__sansonDrop", 1, &a);
}

char *sj_eval(sj_plugin *p, const char *code)
{
    JSValue r = JS_Eval(p->ctx, code, strlen(code), "<eval>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r))
        return value_to_string(p, r);
    JSValue s = JS_JSONStringify(p->ctx, r, JS_UNDEFINED, JS_UNDEFINED);
    JS_FreeValue(p->ctx, r);
    return settle(p, value_to_string(p, s));
}

void sj_close(sj_plugin *p)
{
    if (!p)
        return;
    free_auth(p);
    sj_set_clearance(p, NULL, NULL);
    for (int i = 0; i < p->nhosts; i++)
        free(p->hosts[i]);
    free(p->hosts);
    if (p->curl)
        curl_easy_cleanup(p->curl);
    if (p->ctx)
        JS_FreeContext(p->ctx);
    if (p->rt)
        JS_FreeRuntime(p->rt);
    free(p);
}
