#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define RPC_ENDPOINT "https://api.na-backend.odysee.com/api/v1/proxy"
#define LIVE_API     "https://api.odysee.live/livestream/is_live"
#define UA           "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0"

typedef struct { char *data; size_t len, cap; } Buf;

static void buf_init(Buf *b) {
    b->cap = 4096; b->len = 0; b->data = malloc(b->cap);
    if (b->data) b->data[0] = '\0';
}
static void buf_free(Buf *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }
static int buf_append_mem(Buf *b, const char *p, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + n + 1) nc *= 2;
        char *t = realloc(b->data, nc);
        if (!t) return -1;
        b->data = t; b->cap = nc;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n; b->data[b->len] = '\0';
    return 0;
}
static int buf_append_str(Buf *b, const char *s) { return buf_append_mem(b, s, strlen(s)); }
static size_t curl_write_to_buf(void *p, size_t sz, size_t nm, void *ud) {
    return (buf_append_mem(ud, p, sz * nm) == 0) ? sz * nm : 0;
}

static char g_user_id[32];
static pthread_mutex_t g_rate_mtx = PTHREAD_MUTEX_INITIALIZER;
static double g_last_req_ts = 0.0;
static const double g_min_interval = 0.05;

/* --- Rotación de sesión Odysee (X-Odysee-User-Id) --- */
static pthread_mutex_t g_sess_mtx = PTHREAD_MUTEX_INITIALIZER;

static void rotate_session_user_id(void) {
    unsigned int r = (unsigned int)time(NULL) ^ (unsigned int)getpid()
                   ^ (unsigned int)(uintptr_t)pthread_self();
    FILE *ur = fopen("/dev/urandom", "rb");
    if (ur) { if (fread(&r, sizeof(r), 1, ur) != 1) {} fclose(ur); }
    uint64_t v = 1000000000ULL + ((uint64_t)r % 2000000000ULL);
    pthread_mutex_lock(&g_sess_mtx);
    snprintf(g_user_id, sizeof(g_user_id), "%llu", (unsigned long long)v);
    pthread_mutex_unlock(&g_sess_mtx);
    fprintf(stderr, "[session] Nuevo X-Odysee-User-Id: %s\n", g_user_id);
}

static void copy_session_user_id(char *out, size_t out_sz) {
    pthread_mutex_lock(&g_sess_mtx);
    snprintf(out, out_sz, "%s", g_user_id);
    pthread_mutex_unlock(&g_sess_mtx);
}
/* --- fin rotación --- */

typedef struct HeadCacheEntry {
    char *url, content_type[128], content_length[64], accept_ranges[64], location[1024];
    long status;
    struct HeadCacheEntry *next;
} HeadCacheEntry;

static pthread_mutex_t g_head_mtx = PTHREAD_MUTEX_INITIALIZER;
static HeadCacheEntry *g_head_cache = NULL;

static double now_sec(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}
static void sleep_sec(double sec) {
    if (sec <= 0.0) return;
    struct timespec ts = { (time_t)sec, (long)((sec - (time_t)sec) * 1e9) };
    nanosleep(&ts, NULL);
}
static void init_session_user_id(void) {
    rotate_session_user_id();
}
static void throttle_cdn(void) {
    pthread_mutex_lock(&g_rate_mtx);
    double wait = g_min_interval - (now_sec() - g_last_req_ts);
    if (wait > 0.0) sleep_sec(wait);
    g_last_req_ts = now_sec();
    pthread_mutex_unlock(&g_rate_mtx);
}

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1; char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
static char *url_encode_strict(const char *s) {
    CURL *c = curl_easy_init(); if (!c) return NULL;
    char *e = curl_easy_escape(c, s, (int)strlen(s));
    char *r = xstrdup(e); curl_free(e); curl_easy_cleanup(c);
    return r;
}
static char *url_decode_copy(const char *s) {
    CURL *c = curl_easy_init(); if (!c) return xstrdup(s);
    int ol = 0; char *d = curl_easy_unescape(c, s, (int)strlen(s), &ol);
    char *r = xstrdup(d); curl_free(d); curl_easy_cleanup(c);
    return r;
}
static char *str_replace_all(const char *src, const char *old_s, const char *new_s) {
    Buf b; buf_init(&b); size_t olen = strlen(old_s); const char *p = src;
    while (*p && olen > 0) {
        const char *h = strstr(p, old_s);
        if (!h) { buf_append_str(&b, p); break; }
        buf_append_mem(&b, p, (size_t)(h - p)); buf_append_str(&b, new_s); p = h + olen;
    }
    return b.data;
}
static void unescape_unicode_slash_etc(char **hp) {
    struct { const char *a, *b; } m[] = {
        {"\\u002F","/"},{"\\u002f","/"},{"\\u003A",":"},{"\\u003a",":"},
        {"\\u003F","?"},{"\\u003f","?"},{"\\u0026","&"},{"\\u003D","="},{"\\u003d","="},{NULL,NULL}
    };
    char *c = *hp;
    for (int i = 0; m[i].a; i++) { char *n = str_replace_all(c, m[i].a, m[i].b); free(c); c = n; }
    *hp = c;
}
static char *extract_query_param(const char *url, const char *key) {
    const char *q = strchr(url, '?'); if (!q) return NULL;
    q++; size_t kl = strlen(key);
    while (*q) {
        const char *a = strchr(q, '&'); size_t pl = a ? (size_t)(a - q) : strlen(q);
        if (pl > kl + 1 && strncmp(q, key, kl) == 0 && q[kl] == '=') {
            char *raw = malloc(pl - kl); memcpy(raw, q + kl + 1, pl - kl - 1); raw[pl - kl - 1] = '\0';
            char *dec = url_decode_copy(raw); free(raw); return dec;
        }
        if (!a) break;
        q = a + 1;
    }
    return NULL;
}
static char *json_get_string(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char pat[128]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        const char *af = p + strlen(pat);
        while (*af && isspace((unsigned char)*af)) af++;
        if (*af == ':') {
            af++; while (*af && isspace((unsigned char)*af)) af++;
            if (*af == '"') {
                af++; Buf b; buf_init(&b);
                while (*af && *af != '"') {
                    if (*af == '\\' && af[1]) {
                        af++;
                        if (*af == 'u' && isxdigit((unsigned char)af[1]) && isxdigit((unsigned char)af[2]) &&
                            isxdigit((unsigned char)af[3]) && isxdigit((unsigned char)af[4])) {
                            char hx[5] = {af[1],af[2],af[3],af[4],0};
                            unsigned int cp = (unsigned int)strtoul(hx, NULL, 16);
                            if (cp < 0x80) { char ch = (char)cp; buf_append_mem(&b, &ch, 1); }
                            af += 4;
                        } else {
                            char ch = *af;
                            if (ch == 'n') ch = '\n'; else if (ch == 'r') ch = '\r'; else if (ch == 't') ch = '\t';
                            buf_append_mem(&b, &ch, 1);
                        }
                    } else buf_append_mem(&b, af, 1);
                    af++;
                }
                return b.data;
            }
        }
        p = af;
    }
    return NULL;
}
static int json_get_bool(const char *json, const char *key, int *out) {
    if (!json || !key) return 0;
    char pat[128]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat); if (!p) return 0;
    const char *af = p + strlen(pat);
    while (*af && isspace((unsigned char)*af)) af++;
    if (*af != ':') return 0;
    af++; while (*af && isspace((unsigned char)*af)) af++;
    if (strncmp(af, "true", 4) == 0) { *out = 1; return 1; }
    if (strncmp(af, "false", 5) == 0) { *out = 0; return 1; }
    return 0;
}
static int http_get_text(const char *url, struct curl_slist *hdrs, long *code, Buf *body) {
    CURL *c = curl_easy_init(); if (!c) return -1;
    buf_init(body);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_to_buf);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) { fprintf(stderr, "curl error (%s): %s\n", url, curl_easy_strerror(rc)); curl_easy_cleanup(c); return -1; }
    if (code) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, code);
    curl_easy_cleanup(c);
    return 0;
}
static char *rpc_call(const char *method, const char *params_json) {
    char payload[2048];
    snprintf(payload, sizeof(payload),
             "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s,\"id\":1795521319058}",
             method, params_json);
    char url[256]; snprintf(url, sizeof(url), "%s?m=%s", RPC_ENDPOINT, method);
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json-rpc");
    hdrs = curl_slist_append(hdrs, "Accept: */*");
    hdrs = curl_slist_append(hdrs, "Origin: https://odysee.com");
    hdrs = curl_slist_append(hdrs, "Referer: https://odysee.com/");
    hdrs = curl_slist_append(hdrs, "User-Agent: " UA);
    const char *tok = getenv("LBRY_TOKEN"); char th[512];
    if (tok && *tok) { snprintf(th, sizeof(th), "x-lbry-auth-token: %s", tok); hdrs = curl_slist_append(hdrs, th); }
    Buf resp; buf_init(&resp); CURL *c = curl_easy_init(); long code = 0; CURLcode rc = CURLE_FAILED_INIT;
    if (c) {
        curl_easy_setopt(c, CURLOPT_URL, url); curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload); curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_to_buf); curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
        rc = curl_easy_perform(c); curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code); curl_easy_cleanup(c);
    }
    curl_slist_free_all(hdrs);
    if (rc != CURLE_OK || !resp.data || resp.data[0] != '{') {
        if (resp.data && resp.data[0] != '{') fprintf(stderr, "RPC %s: respuesta no JSON (HTTP %ld)\n", method, code);
        buf_free(&resp); return NULL;
    }
    return resp.data;
}
static char *extract_odysee_from_hackstream(const char *raw_url) {
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "User-Agent: " UA);
    hdrs = curl_slist_append(hdrs, "Referer: https://hackstream.io/");
    long code = 0; Buf body;
    if (http_get_text(raw_url, hdrs, &code, &body) != 0) { curl_slist_free_all(hdrs); return NULL; }
    curl_slist_free_all(hdrs);
    unescape_unicode_slash_etc(&body.data);
    char *emb = json_get_string(body.data, "embedUrl");
    if (emb && *emb) { buf_free(&body); return emb; }
    free(emb);
    const char *od = strstr(body.data, "https://odysee.com/");
    if (!od) od = strstr(body.data, "https://www.odysee.com/");
    if (od) {
        const char *e = od;
        while (*e && !isspace((unsigned char)*e) && *e != '"' && *e != '\'' && *e != '<' && *e != '>' && *e != '\\') e++;
        size_t n = (size_t)(e - od); char *f = malloc(n + 1); memcpy(f, od, n); f[n] = '\0';
        buf_free(&body); return f;
    }
    buf_free(&body); return NULL;
}

/* -------------------------------------------------------------------------
 * url_to_lbry_uri
 *   - lbry://  : respeta '#' (claim_id es parte de la URI). Además quita
 *                los #claimid de cada segmento para que resolve devuelva
 *                el stream (no el canal).
 *   - https:// : '#' sí es fragmento → se corta.
 * ------------------------------------------------------------------------- */
static char *url_to_lbry_uri(const char *url) {
    if (!url || !*url) return NULL;

    /* Caso lbry:// nativo */
    if (strncmp(url, "lbry://", 7) == 0) {
        char *clean = xstrdup(url);
        /* Sólo quitamos query '?' (no '#' porque es claim_id) */
        char *q = strchr(clean, '?'); if (q) *q = '\0';

        /* Quitar '/' finales sobrantes */
        size_t dl = strlen(clean);
        while (dl > 7 && clean[dl - 1] == '/') clean[--dl] = '\0';

        /* Respetar los #claimid: son parte de la URI y necesarios para
           resolver claims cuyo 'name' no coincide con el segmento del
           path (p.ej. @p4im0n#4/VULN-Dirtyclone-ROOTVECTOR#7). */
        char *dec = url_decode_copy(clean + 7);
        free(clean);
        if (!dec) return NULL;

        Buf out; buf_init(&out);
        buf_append_str(&out, "lbry://");
        buf_append_str(&out, dec);
        free(dec);
        return out.data;
    }

    /* Caso https:// odysee/lbry */
    char *clean = xstrdup(url);
    char *q = strchr(clean, '?'); if (q) *q = '\0';
    char *h = strchr(clean, '#'); if (h) *h = '\0';

    const char *pfx[] = {
        "https://www.odysee.com/$/download/","https://odysee.com/$/download/",
        "https://www.odysee.com/$/embed/","https://odysee.com/$/embed/",
        "https://www.odysee.com/%24/download/","https://odysee.com/%24/download/",
        "https://www.odysee.com/%24/embed/","https://odysee.com/%24/embed/",
        "https://www.odysee.com/","https://odysee.com/",
        "https://www.odysee.tv/","https://odysee.tv/",
        "https://www.lbry.tv/","https://lbry.tv/",NULL
    };
    const char *rest = NULL;
    for (int i = 0; pfx[i]; i++) {
        size_t pl = strlen(pfx[i]);
        if (strncmp(clean, pfx[i], pl) == 0) { rest = clean + pl; break; }
    }
    if (!rest || !*rest) { free(clean); return NULL; }

    char *disp = xstrdup(rest);
    size_t dl = strlen(disp);
    while (dl > 0 && disp[dl - 1] == '/') disp[--dl] = '\0';

    /* @canal/reclamo  →  @canal:reclamo  (forma clásica LBRY) */
    if (disp[0] == '@') { char *c = strchr(disp, ':'); if (c) *c = '#'; }
    else { char *s = strchr(disp, '/'); if (s) *s = ':'; }

    char *dec = url_decode_copy(disp); free(disp); free(clean);
    if (!dec) return NULL;

    Buf out; buf_init(&out);
    buf_append_str(&out, "lbry://");
    buf_append_str(&out, dec);
    free(dec);
    return out.data;
}

static void json_escape_into(Buf *b, const char *s) {
    buf_append_str(b, "\"");
    for (; *s; s++) { if (*s == '"' || *s == '\\') buf_append_mem(b, "\\", 1); buf_append_mem(b, s, 1); }
    buf_append_str(b, "\"");
}
static char *resolve_play_url(const char *raw_url) {
    char *cur = xstrdup(raw_url);
    if (strncmp(cur, "https://hackstream.io/watch/", 28) == 0 ||
        strncmp(cur, "https://www.hackstream.io/watch/", 32) == 0) {
        fprintf(stderr, "[*] Extrayendo enlace de Odysee desde HackStream...\n");
        char *ex = extract_odysee_from_hackstream(cur); free(cur);
        if (!ex) { fprintf(stderr, "No se encontro enlace de Odysee en HackStream\n"); return NULL; }
        cur = ex; fprintf(stderr, "[*] Enlace Odysee: %s\n", ex);
    }
    char *uri = url_to_lbry_uri(cur);
    if (!uri) { fprintf(stderr, "URL no soportada: %s\n", cur); free(cur); return NULL; }
    fprintf(stderr, "[*] URI: %s\n", uri);

    Buf rp; buf_init(&rp); buf_append_str(&rp, "{\"urls\":"); json_escape_into(&rp, uri); buf_append_str(&rp, "}");
    char *res_str = rpc_call("resolve", rp.data); buf_free(&rp);
    if (!res_str) { free(uri); free(cur); return NULL; }

    char *title = json_get_string(res_str, "title");
    char *stype = json_get_string(res_str, "stream_type");
    char *vtype = json_get_string(res_str, "value_type");
    fprintf(stderr, "[*] Titulo: %s\n", title ? title : "LBRY Stream");

    char *final_url = NULL;

    if ((stype && strcmp(stype, "video") == 0) || (stype && strcmp(stype, "audio") == 0)) {
        Buf gp; buf_init(&gp); buf_append_str(&gp, "{\"uri\":"); json_escape_into(&gp, uri);
        buf_append_str(&gp, ",\"environment\":null");
        char *sig = extract_query_param(cur, "signature");
        char *sig_ts = extract_query_param(cur, "signature_ts");
        if (sig) { buf_append_str(&gp, ",\"signature\":"); json_escape_into(&gp, sig); free(sig); }
        if (sig_ts) { buf_append_str(&gp, ",\"signature_ts\":"); json_escape_into(&gp, sig_ts); free(sig_ts); }
        buf_append_str(&gp, "}");
        char *get_str = rpc_call("get", gp.data); buf_free(&gp);
        if (get_str) {
            char *surl = json_get_string(get_str, "streaming_url");
            if (surl && *surl) {
                fprintf(stderr, "[*] streaming_url: %s\n", surl);
                if (strstr(surl, ".mp4")) final_url = xstrdup(surl);
                else {
                    struct curl_slist *hdrs = NULL; char hua[256], huid[64];
                    snprintf(hua, sizeof(hua), "User-Agent: %s", UA);
                    snprintf(huid, sizeof(huid), "X-Odysee-User-Id: %s", g_user_id);
                    hdrs = curl_slist_append(hdrs, hua); hdrs = curl_slist_append(hdrs, "Accept: */*");
                    hdrs = curl_slist_append(hdrs, "Accept-Encoding: identity");
                    hdrs = curl_slist_append(hdrs, "Origin: https://odysee.com");
                    hdrs = curl_slist_append(hdrs, "Referer: https://odysee.com/");
                    hdrs = curl_slist_append(hdrs, huid);
                    CURL *hc = curl_easy_init();
                    if (hc) {
                        curl_easy_setopt(hc, CURLOPT_URL, surl); curl_easy_setopt(hc, CURLOPT_NOBODY, 1L);
                        curl_easy_setopt(hc, CURLOPT_FOLLOWLOCATION, 0L); curl_easy_setopt(hc, CURLOPT_HTTPHEADER, hdrs);
                        curl_easy_setopt(hc, CURLOPT_TIMEOUT, 20L); curl_easy_perform(hc);
                        char *loc = NULL; curl_easy_getinfo(hc, CURLINFO_REDIRECT_URL, &loc);
                        if (loc && *loc) { fprintf(stderr, "[*] Redirige a: %s\n", loc); final_url = xstrdup(loc); }
                        else final_url = xstrdup(surl);
                        curl_easy_cleanup(hc);
                    }
                    curl_slist_free_all(hdrs);
                }
            }
            free(surl); free(get_str);
        }
    } else if (vtype && strcmp(vtype, "stream") == 0) {
        char *cid = json_get_string(res_str, "claim_id");
        if (cid && *cid) {
            char *ec = url_encode_strict(cid); char lurl[512];
            snprintf(lurl, sizeof(lurl), "%s?channel_claim_id=%s", LIVE_API, ec); free(ec);
            struct curl_slist *hdrs = curl_slist_append(NULL, "User-Agent: " UA);
            long code = 0; Buf body;
            if (http_get_text(lurl, hdrs, &code, &body) == 0) {
                int is_live = 0; char *vurl = json_get_string(body.data, "VideoURL");
                if (json_get_bool(body.data, "Live", &is_live) && is_live && vurl && *vurl) final_url = xstrdup(vurl);
                else fprintf(stderr, "El directo no esta activo\n");
                free(vurl);
            }
            curl_slist_free_all(hdrs); buf_free(&body);
        }
        free(cid);
    } else {
        fprintf(stderr, "Tipo no soportado: stream_type=%s value_type=%s\n",
                stype ? stype : "", vtype ? vtype : "");
    }

    free(title); free(stype); free(vtype); free(res_str); free(uri); free(cur);
    return final_url;
}

typedef struct { int client_fd; uint16_t port; } ClientArg;
typedef struct {
    int client_fd, is_head, headers_sent;
    CURL *curl;
    char content_type[128], content_length[64], accept_ranges[64], location[1024];
    double retry_after;
    double t0_ms;
    int first_byte_seen;
    long long total_sent;
} StreamCtx;

static double log_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

#define IDLE_TIMEOUT_SEC 120.0
#define LOCK_FILE_PATH   "/tmp/hackstream_proxy.lock"
#define PORT_FILE_PATH   "/tmp/hackstream_proxy.port"

static pthread_mutex_t g_act_mtx = PTHREAD_MUTEX_INITIALIZER;
static double g_last_active_ts = 0.0;
static int g_active_streams = 0;
static int g_lock_fd = -1;

static void touch_activity(int delta_streams) {
    pthread_mutex_lock(&g_act_mtx);
    g_last_active_ts = now_sec();
    g_active_streams += delta_streams;
    if (g_active_streams < 0) g_active_streams = 0;
    pthread_mutex_unlock(&g_act_mtx);
}

static int acquire_single_instance_lock(void) {
    int fd = open(LOCK_FILE_PATH, O_CREAT | O_RDWR, 0666);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return 0; /* ya hay otra instancia activa */
    }
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%ld\n", (long)getpid());
    if (ftruncate(fd, 0) == 0 && n > 0) {
        ssize_t w = write(fd, buf, (size_t)n);
        (void)w;
    }
    g_lock_fd = fd;
    return 1; /* lock adquirido */
}

static void release_single_instance_lock(void) {
    if (g_lock_fd >= 0) {
        flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        g_lock_fd = -1;
    }
    unlink(PORT_FILE_PATH);
}

static void write_port_file(uint16_t port) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s.%ld", PORT_FILE_PATH, (long)getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "%u\n", (unsigned)port);
        if (n > 0) {
            ssize_t w = write(fd, buf, (size_t)n);
            (void)w;
        }
        close(fd);
        rename(tmp, PORT_FILE_PATH);
    }
}

static uint16_t read_port_file(void) {
    FILE *f = fopen(PORT_FILE_PATH, "r");
    if (!f) return 0;
    unsigned p = 0;
    if (fscanf(f, "%u", &p) != 1) p = 0;
    fclose(f);
    return (p > 0 && p <= 65535) ? (uint16_t)p : 0;
}

static int query_existing_daemon(uint16_t port, const char *raw_url, char *out_proxy_url, size_t out_sz) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct timeval tv = { 10, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    char *enc = url_encode_strict(raw_url);
    if (!enc) { close(fd); return -1; }
    char req[5200];
    int n = snprintf(req, sizeof(req),
                     "GET /resolve?url=%s HTTP/1.1\r\nHost: 127.0.0.1:%u\r\nConnection: close\r\n\r\n",
                     enc, (unsigned)port);
    free(enc);
    if (n <= 0 || send(fd, req, (size_t)n, MSG_NOSIGNAL) < 0) {
        close(fd);
        return -1;
    }
    char resp[8192];
    size_t used = 0;
    while (used + 1 < sizeof(resp)) {
        ssize_t r = recv(fd, resp + used, sizeof(resp) - 1 - used, 0);
        if (r <= 0) break;
        used += (size_t)r;
    }
    resp[used] = '\0';
    close(fd);
    char *body = strstr(resp, "\r\n\r\n");
    if (!body) return -1;
    body += 4;
    char *surl = json_get_string(body, "stream_url");
    if (!surl || !*surl) { free(surl); return -1; }
    snprintf(out_proxy_url, out_sz, "%s", surl);
    free(surl);
    return 0;
}

static long ctx_http_code(StreamCtx *ctx) {
    long code = 0;
    if (ctx && ctx->curl) curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &code);
    return code;
}

static int is_retryable_status(long code) {
    return (code == 429 || code == 502 || code == 503 || code == 504);
}

static int flush_ctx_headers(StreamCtx *ctx, long code) {
    if (ctx->headers_sent || ctx->client_fd < 0 || code < 200) return 0;
    Buf rh; buf_init(&rh); char sl[128];
    const char *rs = (code == 206) ? "Partial Content" : (code == 200) ? "OK" : "Status";
    snprintf(sl, sizeof(sl), "HTTP/1.1 %ld %s\r\n", code, rs); buf_append_str(&rh, sl);
    if (ctx->content_type[0]) { char t[200]; snprintf(t, sizeof(t), "Content-Type: %s\r\n", ctx->content_type); buf_append_str(&rh, t); }
    if (ctx->content_length[0]) { char t[128]; snprintf(t, sizeof(t), "Content-Length: %s\r\n", ctx->content_length); buf_append_str(&rh, t); }
    if (ctx->accept_ranges[0]) { char t[128]; snprintf(t, sizeof(t), "Accept-Ranges: %s\r\n", ctx->accept_ranges); buf_append_str(&rh, t); }
    if (ctx->location[0] && code >= 300 && code < 400) { char t[1200]; snprintf(t, sizeof(t), "Location: %s\r\n", ctx->location); buf_append_str(&rh, t); }
    buf_append_str(&rh, "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
    ssize_t w = send(ctx->client_fd, rh.data, rh.len, MSG_NOSIGNAL);
    buf_free(&rh); ctx->headers_sent = 1;
    return (w < 0) ? -1 : 0;
}

static size_t on_cdn_header(char *buf, size_t sz, size_t nm, void *ud) {
    size_t tot = sz * nm; StreamCtx *ctx = ud;
    char *line = malloc(tot + 1); if (!line) return tot;
    memcpy(line, buf, tot); line[tot] = '\0';
    for (size_t i = tot; i > 0 && (line[i - 1] == '\r' || line[i - 1] == '\n'); i--) line[i - 1] = '\0';
    if (strncasecmp(line, "Content-Type:", 13) == 0) {
        const char *v = line + 13; while (*v == ' ') v++;
        snprintf(ctx->content_type, sizeof(ctx->content_type), "%s", v);
    } else if (strncasecmp(line, "Content-Length:", 15) == 0) {
        const char *v = line + 15; while (*v == ' ') v++;
        snprintf(ctx->content_length, sizeof(ctx->content_length), "%s", v);
    } else if (strncasecmp(line, "Accept-Ranges:", 14) == 0) {
        const char *v = line + 14; while (*v == ' ') v++;
        snprintf(ctx->accept_ranges, sizeof(ctx->accept_ranges), "%s", v);
    } else if (strncasecmp(line, "Retry-After:", 12) == 0) {
        const char *v = line + 12; while (*v == ' ') v++;
        double ra = atof(v);
        if (ra > 0.0) ctx->retry_after = ra;
    } else if (strncasecmp(line, "Location:", 9) == 0) {
        const char *v = line + 9; while (*v == ' ') v++;
        snprintf(ctx->location, sizeof(ctx->location), "%s", v);
    } else if (line[0] == '\0') {
        long code = ctx_http_code(ctx);
        /* No enviar 429/502/503/504 al cliente mientras haya reintentos */
        if (!is_retryable_status(code) && code >= 200) {
            if (flush_ctx_headers(ctx, code) < 0) { free(line); return 0; }
        }
    }
    free(line); return tot;
}

static size_t on_cdn_body(char *ptr, size_t sz, size_t nm, void *ud) {
    size_t tot = sz * nm; StreamCtx *ctx = ud;
    long code = ctx_http_code(ctx);
    if (is_retryable_status(code)) return tot;
    if (ctx->is_head) return tot;
    if (!ctx->headers_sent && code >= 200) {
        if (flush_ctx_headers(ctx, code) < 0) return 0;
    }
    if (!ctx->first_byte_seen && tot > 0) {
        ctx->first_byte_seen = 1;
        fprintf(stderr, "[proxy-first-byte] fd=%d first_byte_ms=%.1f bytes=%zu\n",
                ctx->client_fd, log_now_ms() - ctx->t0_ms, tot);
    }
    size_t sent = 0;
    while (sent < tot) {
        ssize_t n = send(ctx->client_fd, ptr + sent, tot - sent, MSG_NOSIGNAL);
        if (n <= 0) return 0;
        sent += (size_t)n;
        ctx->total_sent += n;
    }
    return tot;
}

static void *handle_client(void *arg) {
    ClientArg *carg = arg; int cfd = carg->client_fd; uint16_t port = carg->port; free(carg);
    touch_activity(0);
    char req_buf[8192]; ssize_t rlen = recv(cfd, req_buf, sizeof(req_buf) - 1, 0);
    if (rlen <= 0) { close(cfd); return NULL; }
    req_buf[rlen] = '\0';
    char method[16] = {0}, target_path[4096] = {0};
    if (sscanf(req_buf, "%15s %4095s", method, target_path) != 2) { close(cfd); return NULL; }
    int is_head = (strcasecmp(method, "HEAD") == 0), is_get = (strcasecmp(method, "GET") == 0);
    if (!is_head && !is_get) {
        const char *m405 = "HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\n\r\n";
        send(cfd, m405, strlen(m405), MSG_NOSIGNAL); close(cfd); return NULL;
    }
    if (strncmp(target_path, "/resolve?", 9) == 0) {
        char *raw_u = extract_query_param(target_path, "url");
        if (!raw_u || !*raw_u) {
            free(raw_u);
            const char *m400 = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"missing url\"}";
            send(cfd, m400, strlen(m400), MSG_NOSIGNAL); close(cfd); return NULL;
        }
        touch_activity(1);
        char *resolved = resolve_play_url(raw_u); free(raw_u);
        touch_activity(-1);
        if (!resolved) {
            const char *m502 = "HTTP/1.1 502 Bad Gateway\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"resolve failed\"}";
            send(cfd, m502, strlen(m502), MSG_NOSIGNAL); close(cfd); return NULL;
        }
        char *enc = url_encode_strict(resolved); free(resolved);
        char jout[4600]; snprintf(jout, sizeof(jout), "{\"stream_url\":\"http://127.0.0.1:%u/proxy?u=%s\"}", (unsigned)port, enc ? enc : "");
        free(enc);
        char hdr[512]; snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n", strlen(jout));
        send(cfd, hdr, strlen(hdr), MSG_NOSIGNAL);
        if (is_get) send(cfd, jout, strlen(jout), MSG_NOSIGNAL);
        close(cfd); return NULL;
    }
    if (strncmp(target_path, "/proxy?", 7) != 0) {
        const char *m404 = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
        send(cfd, m404, strlen(m404), MSG_NOSIGNAL); close(cfd); return NULL;
    }
    char *cdn_url = extract_query_param(target_path, "u");
    if (!cdn_url || !*cdn_url) {
        free(cdn_url);
        const char *m400 = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nMissing u parameter\n";
        send(cfd, m400, strlen(m400), MSG_NOSIGNAL); close(cfd); return NULL;
    }
    char range_val[256] = {0};
    const char *rline = strcasestr(req_buf, "\r\nRange:");
    if (rline) {
        rline += 8; while (*rline == ' ') rline++;
        size_t i = 0; while (*rline && *rline != '\r' && *rline != '\n' && i + 1 < sizeof(range_val)) range_val[i++] = *rline++;
        range_val[i] = '\0';
    }
    double t0_ms = log_now_ms();
    fprintf(stderr, "[proxy-in] fd=%d %s range=%s u=%.120s\n", cfd, method, range_val[0] ? range_val : "NONE", cdn_url);
    if (is_head && range_val[0] == '\0') {
        pthread_mutex_lock(&g_head_mtx);
        for (HeadCacheEntry *e = g_head_cache; e; e = e->next) {
            if (strcmp(e->url, cdn_url) == 0) {
                Buf b; buf_init(&b); char sl[128];
                snprintf(sl, sizeof(sl), "HTTP/1.1 %ld %s\r\n", e->status, (e->status == 206) ? "Partial Content" : "OK");
                buf_append_str(&b, sl);
                if (e->content_type[0]) { char t[200]; snprintf(t, sizeof(t), "Content-Type: %s\r\n", e->content_type); buf_append_str(&b, t); }
                if (e->content_length[0]) { char t[128]; snprintf(t, sizeof(t), "Content-Length: %s\r\n", e->content_length); buf_append_str(&b, t); }
                if (e->accept_ranges[0]) { char t[128]; snprintf(t, sizeof(t), "Accept-Ranges: %s\r\n", e->accept_ranges); buf_append_str(&b, t); }
                if (e->location[0]) { char t[1200]; snprintf(t, sizeof(t), "Location: %s\r\n", e->location); buf_append_str(&b, t); }
                buf_append_str(&b, "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
                send(cfd, b.data, b.len, MSG_NOSIGNAL); buf_free(&b);
                pthread_mutex_unlock(&g_head_mtx); free(cdn_url); close(cfd); return NULL;
            }
        }
        pthread_mutex_unlock(&g_head_mtx);
    }
    /* Reintentos: 3 intentos por sesión, hasta 3 sesiones distintas.
       Si tras cambiar de sesión (2ª sesión) vuelve a fallar en el
       mismo intento, se aborta y se devuelve 502. */
    enum { ATTEMPTS_PER_SESSION = 3, MAX_SESSIONS = 3, ABORT_ON_SESSION = 2 };
    int done = 0;
    long final_code = 0;
    double delay = 1.0;
    int headers_ever_sent = 0;

    for (int sess = 1; sess <= MAX_SESSIONS && !done; sess++) {
        if (sess > 1) {
            rotate_session_user_id();
            delay = 1.0;
        }
        for (int attempt = 1; attempt <= ATTEMPTS_PER_SESSION && !done; attempt++) {
            throttle_cdn();

            char huid[64];
            copy_session_user_id(huid, sizeof(huid));

            struct curl_slist *hdrs = NULL;
            char hua[256], hrng[300];
            snprintf(hua, sizeof(hua), "User-Agent: %s", UA);
            hdrs = curl_slist_append(hdrs, hua);
            hdrs = curl_slist_append(hdrs, "Accept: */*");
            hdrs = curl_slist_append(hdrs, "Accept-Language: es-ES,es;q=0.9,en-US;q=0.8,en;q=0.7");
            hdrs = curl_slist_append(hdrs, "Accept-Encoding: identity");
            hdrs = curl_slist_append(hdrs, "Origin: https://odysee.com");
            hdrs = curl_slist_append(hdrs, "Referer: https://odysee.com/");
            {
                char h[128];
                snprintf(h, sizeof(h), "X-Odysee-User-Id: %s", huid);
                hdrs = curl_slist_append(hdrs, h);
            }
            hdrs = curl_slist_append(hdrs, "Sec-GPC: 1");
            hdrs = curl_slist_append(hdrs, "Sec-Fetch-Dest: empty");
            hdrs = curl_slist_append(hdrs, "Sec-Fetch-Mode: cors");
            hdrs = curl_slist_append(hdrs, "Sec-Fetch-Site: cross-site");
            hdrs = curl_slist_append(hdrs, "Connection: close");
            if (range_val[0] != '\0') {
                snprintf(hrng, sizeof(hrng), "Range: %s", range_val);
                hdrs = curl_slist_append(hdrs, hrng);
            }

            StreamCtx sctx;
            memset(&sctx, 0, sizeof(sctx));
            sctx.client_fd = cfd;
            sctx.is_head = is_head;
            sctx.t0_ms = t0_ms;

            CURL *curl = curl_easy_init();
            CURLcode crc = CURLE_FAILED_INIT;
            sctx.curl = curl;
            if (curl) {
                curl_easy_setopt(curl, CURLOPT_URL, cdn_url);
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
                curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
                curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
                curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L);
                curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
                curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
                if (is_head) curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
                curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, on_cdn_header);
                curl_easy_setopt(curl, CURLOPT_HEADERDATA, &sctx);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_cdn_body);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sctx);
                touch_activity(1);
                crc = curl_easy_perform(curl);
                touch_activity(-1);
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &final_code);
                curl_easy_cleanup(curl);
            }
            curl_slist_free_all(hdrs);

            if (sctx.headers_sent) headers_ever_sent = 1;

            /* Éxito o error no retryable -> salir */
            if (!(crc == CURLE_OK && is_retryable_status(final_code)) ) {
                if (!sctx.headers_sent && final_code >= 200) {
                    flush_ctx_headers(&sctx, final_code);
                    if (sctx.headers_sent) headers_ever_sent = 1;
                }
                if (is_head && range_val[0] == '\0' &&
                    (final_code == 200 || final_code == 206)) {
                    pthread_mutex_lock(&g_head_mtx);
                    HeadCacheEntry *entry = calloc(1, sizeof(HeadCacheEntry));
                    if (entry) {
                        entry->url = xstrdup(cdn_url);
                        entry->status = final_code;
                        snprintf(entry->content_type, sizeof(entry->content_type), "%s", sctx.content_type);
                        snprintf(entry->content_length, sizeof(entry->content_length), "%s", sctx.content_length);
                        snprintf(entry->accept_ranges, sizeof(entry->accept_ranges), "%s", sctx.accept_ranges);
                        snprintf(entry->location, sizeof(entry->location), "%s", sctx.location);
                        entry->next = g_head_cache;
                        g_head_cache = entry;
                    }
                    pthread_mutex_unlock(&g_head_mtx);
                }
                fprintf(stderr,
                        "[proxy-out] fd=%d %s range=%s code=%ld headers_sent=%d bytes=%lld crc=%d elapsed=%.1fms\n",
                        cfd, method, range_val[0] ? range_val : "NONE", final_code,
                        sctx.headers_sent, sctx.total_sent, (int)crc,
                        log_now_ms() - t0_ms);
                done = 1;
                break;
            }

            /* Error retryable */
            double slp = sctx.retry_after > 0.0 ? sctx.retry_after : delay;
            if (slp > 30.0) slp = 30.0;
            fprintf(stderr,
                    "[proxy] CDN HTTP %ld (sesión %d/%d, intento %d/%d) -> espera %.1fs\n",
                    final_code, sess, MAX_SESSIONS, attempt, ATTEMPTS_PER_SESSION, slp);

            /* Regla pedida: si ya cambiamos UNA vez de sesión (sess==2)
               y vuelve a fallar el mismo intento, abortar. */
            if (sess >= ABORT_ON_SESSION) {
                fprintf(stderr,
                        "[proxy] Fallo persiste tras cambio de sesión (sesión %d, intento %d). Abortando.\n",
                        sess, attempt);
                if (!headers_ever_sent) {
                    const char *m502 =
                        "HTTP/1.1 502 Bad Gateway\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nCDN rate limited (persiste tras cambio de sesión)\n";
                    send(cfd, m502, strlen(m502), MSG_NOSIGNAL);
                    headers_ever_sent = 1;
                }
                done = 1;
                break;
            }

            if (attempt < ATTEMPTS_PER_SESSION) {
                sleep_sec(slp);
                delay = (delay * 2.0 > 15.0) ? 15.0 : delay * 2.0;
            }
            /* si agotamos intentos de esta sesión, cae al for externo y rota */
        }
    }

    if (!done && !headers_ever_sent) {
        const char *m502 =
            "HTTP/1.1 502 Bad Gateway\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nCDN rate limited\n";
        send(cfd, m502, strlen(m502), MSG_NOSIGNAL);
    }

    free(cdn_url); close(cfd); return NULL;
}

static int warmup_head_via_proxy(uint16_t port, const char *enc_url) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    char req[4800];
    int n = snprintf(req, sizeof(req),
                     "HEAD /proxy?u=%s HTTP/1.1\r\nHost: 127.0.0.1:%u\r\nUser-Agent: %s\r\nConnection: close\r\n\r\n",
                     enc_url, (unsigned)port, UA);
    if (n > 0) send(fd, req, (size_t)n, MSG_NOSIGNAL);
    char buf[1024];
    int status = 0;
    ssize_t r = recv(fd, buf, sizeof(buf) - 1, 0);
    if (r > 0) {
        buf[r] = '\0';
        int tmp = 0;
        if (sscanf(buf, "HTTP/%*d.%*d %d", &tmp) == 1) status = tmp;
    }
    close(fd);
    return (status == 200 || status == 206) ? 0 : -1;
}

static volatile sig_atomic_t g_stop = 0;
static int g_listen_fd = -1;

static void on_term_sig(int sig) {
    (void)sig;
    g_stop = 1;
    if (g_listen_fd >= 0) {
        shutdown(g_listen_fd, SHUT_RDWR);
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}

static uint16_t start_proxy_server(int *out_fd) {
    int fd = socket(AF_INET, SOCK_STREAM, 0); if (fd < 0) return 0;
    int opt = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = htons(0);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 64) < 0) { close(fd); return 0; }
    socklen_t len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &len) < 0) { close(fd); return 0; }
    *out_fd = fd; g_listen_fd = fd; return ntohs(addr.sin_port);
}

static void *accept_loop(void *arg) {
    int lfd = *(int *)arg; free(arg);
    struct sockaddr_in addr; socklen_t len = sizeof(addr); uint16_t port = 0;
    if (getsockname(lfd, (struct sockaddr *)&addr, &len) == 0) port = ntohs(addr.sin_port);
    while (!g_stop) {
        int cfd = accept(lfd, NULL, NULL);
        touch_activity(0);
        if (cfd < 0) { if (errno == EINTR) continue; break; }
        ClientArg *ca = malloc(sizeof(ClientArg)); ca->client_fd = cfd; ca->port = port;
        pthread_t th; pthread_attr_t attr; pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&th, &attr, handle_client, ca); pthread_attr_destroy(&attr);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso:\n  %s <url_odysee|lbry|hackstream> [--serve-only]\n  %s --daemon\n", argv[0], argv[0]);
        return 1;
    }
    signal(SIGPIPE, SIG_IGN); init_session_user_id(); curl_global_init(CURL_GLOBAL_ALL);
    int is_daemon = (strcmp(argv[1], "--daemon") == 0);
    const char *raw_url = is_daemon ? NULL : argv[1];
    int serve_only = (!is_daemon && argc >= 3 && strcmp(argv[2], "--serve-only") == 0);

    int lock_res = acquire_single_instance_lock();
    if (lock_res == 0) {
        /* Ya existe un daemon/instancia activa: reutilizarlo */
        for (int wait_i = 0; wait_i < 40; wait_i++) {
            uint16_t existing_port = read_port_file();
            if (existing_port > 0) {
                if (is_daemon) {
                    curl_global_cleanup();
                    return 0;
                }
                char reused_url[5200];
                if (query_existing_daemon(existing_port, raw_url, reused_url, sizeof(reused_url)) == 0) {
                    printf("%s\n", reused_url);
                    fflush(stdout);
                    fprintf(stderr, "[*] Reutilizando hackstream_proxy activo en puerto %u\n", (unsigned)existing_port);
                    curl_global_cleanup();
                    return 0;
                }
            }
            sleep_sec(0.05);
        }
        fprintf(stderr, "Instancia previa ocupada sin puerto disponible\n");
        curl_global_cleanup();
        return 1;
    }
    if (lock_res < 0) {
        fprintf(stderr, "No se pudo abrir lockfile %s\n", LOCK_FILE_PATH);
    }

    int lfd = -1; uint16_t port = start_proxy_server(&lfd);
    if (port == 0) {
        fprintf(stderr, "Error al iniciar socket local en 127.0.0.1\n");
        release_single_instance_lock();
        curl_global_cleanup();
        return 1;
    }
    write_port_file(port);
    touch_activity(0);

    int *fd_arg = malloc(sizeof(int)); *fd_arg = lfd;
    pthread_t acc_th; pthread_create(&acc_th, NULL, accept_loop, fd_arg); pthread_detach(acc_th);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term_sig;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    if (!is_daemon) {
        char *play_url = resolve_play_url(raw_url);
        if (!play_url) {
            if (lfd >= 0) { shutdown(lfd, SHUT_RDWR); close(lfd); }
            release_single_instance_lock();
            curl_global_cleanup();
            return 1;
        }
        char *enc = url_encode_strict(play_url);
        char proxy_url[4600];
        snprintf(proxy_url, sizeof(proxy_url), "http://127.0.0.1:%u/proxy?u=%s", (unsigned)port, enc ? enc : "");
        warmup_head_via_proxy(port, enc ? enc : "");
        free(enc); free(play_url);
        printf("%s\n", proxy_url); fflush(stdout);
        fprintf(stderr, "[*] Proxy listo en: %s\n", proxy_url);
        if (serve_only) fprintf(stderr, "[*] Modo --serve-only activo (idle timeout %.0fs).\n", IDLE_TIMEOUT_SEC);
        else fprintf(stderr, "[*] Sirviendo stream (idle timeout %.0fs)...\n", IDLE_TIMEOUT_SEC);
    } else {
        fprintf(stderr, "[*] Daemon hackstream_proxy escuchando en 127.0.0.1:%u (idle timeout %.0fs)\n",
                (unsigned)port, IDLE_TIMEOUT_SEC);
    }

    while (!g_stop) {
        sleep_sec(1.0);
        pthread_mutex_lock(&g_act_mtx);
        int active = g_active_streams;
        double last_t = g_last_active_ts;
        pthread_mutex_unlock(&g_act_mtx);
        if (active == 0 && last_t > 0.0 && (now_sec() - last_t) >= IDLE_TIMEOUT_SEC) {
            fprintf(stderr, "[*] Inactividad de %.0fs alcanzada; cerrando hackstream_proxy\n", IDLE_TIMEOUT_SEC);
            g_stop = 1;
            break;
        }
    }

    if (g_listen_fd >= 0) {
        shutdown(g_listen_fd, SHUT_RDWR);
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    release_single_instance_lock();
    curl_global_cleanup();
    return 0;
}
