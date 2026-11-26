#define _GNU_SOURCE
#include "stream.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>

#define SP_UA "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0"
#define SP_CHUNK (1024 * 1024)
#define SP_MAX 64

enum { K_RANGE, K_HLS, K_DIRECT };

typedef struct {
    int id;
    int kind;
    char *url;
    char *referer;
    long long length;
    int flags;
    double start;
} sp_entry;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#define DBG(...) do { if (getenv("SP_DEBUG")) { fprintf(stderr, "[sp] " __VA_ARGS__); fputc('\n', stderr); } } while (0)
static sp_entry entries[SP_MAX];
static int nentries, next_id = 1, listen_fd = -1, port;

typedef struct {
    char *data;
    size_t len, cap;
} sbuf;

static void sbuf_add(sbuf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 8192;
        while (cap < b->len + n + 1)
            cap *= 2;
        b->data = realloc(b->data, cap);
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static size_t to_buf(char *p, size_t s, size_t n, void *ud)
{
    sbuf_add(ud, p, s * n);
    return s * n;
}

typedef struct {
    int fd;
    int gone;
    long long sent;
} sink;

static int send_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += w;
        n -= w;
    }
    return 0;
}

static size_t to_sock(char *p, size_t s, size_t n, void *ud)
{
    sink *k = ud;
    if (send_all(k->fd, p, s * n)) {
        k->gone = 1;
        return 0;
    }
    k->sent += s * n;
    return s * n;
}

static CURL *new_curl(const char *url, struct curl_slist *hl)
{
    CURL *c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, SP_UA);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    if (hl)
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    return c;
}

static int fetch_text(const char *url, sbuf *out)
{
    CURL *c = new_curl(url, NULL);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_buf);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    DBG("text %ld rc=%d %zuB %.100s", code, rc, out->len, url);
    return rc == CURLE_OK && code >= 200 && code < 300 ? 0 : -1;
}

static char *resolve(const char *base, const char *uri)
{
    char *r;
    if (strstr(uri, "://"))
        return strdup(uri);
    if (uri[0] == '/') {
        const char *h = strstr(base, "://");
        const char *e = h ? strchr(h + 3, '/') : NULL;
        int n = e ? (int)(e - base) : (int)strlen(base);
        if (asprintf(&r, "%.*s%s", n, base, uri) < 0)
            return NULL;
        return r;
    }
    const char *q = strchr(base, '?');
    int n = q ? (int)(q - base) : (int)strlen(base);
    while (n > 0 && base[n - 1] != '/')
        n--;
    if (asprintf(&r, "%.*s%s", n, base, uri) < 0)
        return NULL;
    return r;
}

static const char *attr(const char *line, const char *key, char *out, size_t sz)
{
    size_t kl = strlen(key);
    const char *p = line;
    while ((p = strstr(p, key))) {
        if ((p == line || p[-1] == ',' || p[-1] == ':') && p[kl] == '=') {
            p += kl + 1;
            size_t i = 0;
            if (*p == '"') {
                p++;
                while (*p && *p != '"' && i + 1 < sz)
                    out[i++] = *p++;
            } else {
                while (*p && *p != ',' && *p != '\r' && *p != '\n' && i + 1 < sz)
                    out[i++] = *p++;
            }
            out[i] = 0;
            return out;
        }
        p += kl;
    }
    return NULL;
}

int sp_hls_variants(const char *master, sp_variant **out)
{
    sbuf b = {0};
    *out = NULL;
    if (fetch_text(master, &b) || !b.data)
        return -1;
    char *media_ids[32], *media_names[32], *media_audio[32];
    int nmedia = 0, n = 0, cap = 0;
    sp_variant *v = NULL;
    char *save, *line, *pending = NULL;
    for (line = strtok_r(b.data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        line[strcspn(line, "\r")] = 0;
        char a[4096];
        if (!strncmp(line, "#EXT-X-MEDIA:", 13) && nmedia < 32 && attr(line, "GROUP-ID", a, sizeof a)) {
            media_ids[nmedia] = strdup(a);
            media_names[nmedia] = strdup(attr(line, "NAME", a, sizeof a) ? a : "");
            media_audio[nmedia] = NULL;
            char t[16];
            if (attr(line, "TYPE", t, sizeof t) && !strcmp(t, "AUDIO") && attr(line, "URI", a, sizeof a))
                media_audio[nmedia] = resolve(master, a);
            nmedia++;
        } else if (!strncmp(line, "#EXT-X-STREAM-INF:", 18)) {
            free(pending);
            pending = strdup(line);
        } else if (pending && line[0] && line[0] != '#') {
            if (n == cap) {
                cap = cap ? cap * 2 : 8;
                v = realloc(v, cap * sizeof *v);
            }
            sp_variant *x = &v[n++];
            memset(x, 0, sizeof *x);
            x->url = resolve(master, line);
            if (attr(pending, "BANDWIDTH", a, sizeof a))
                x->bandwidth = atoi(a);
            if (attr(pending, "RESOLUTION", a, sizeof a))
                sscanf(a, "%dx%d", &x->width, &x->height);
            if (attr(pending, "FRAME-RATE", a, sizeof a))
                x->fps = atof(a);
            if (attr(pending, "AUDIO", a, sizeof a))
                for (int i = 0; i < nmedia && !x->audio; i++)
                    if (!strcmp(media_ids[i], a) && media_audio[i])
                        x->audio = strdup(media_audio[i]);
            if (attr(pending, "VIDEO", a, sizeof a)) {
                for (int i = 0; i < nmedia; i++)
                    if (!strcmp(media_ids[i], a) && media_names[i][0])
                        x->name = strdup(media_names[i]);
                if (!x->name && strcmp(a, "chunked"))
                    x->name = strdup(a);
            }
            if (!x->name) {
                char nm[32];
                if (x->height)
                    snprintf(nm, sizeof nm, "%dp%s", x->height, x->fps > 31 ? "60" : "");
                else
                    snprintf(nm, sizeof nm, "audio");
                x->name = strdup(nm);
            }
            free(pending);
            pending = NULL;
        }
    }
    free(pending);
    for (int i = 0; i < nmedia; i++) {
        free(media_ids[i]);
        free(media_names[i]);
        free(media_audio[i]);
    }
    free(b.data);
    if (!n) {
        v = calloc(1, sizeof *v);
        v->url = strdup(master);
        v->name = strdup("Auto");
        n = 1;
    }
    *out = v;
    return n;
}

void sp_free_variants(sp_variant *v, int n)
{
    for (int i = 0; i < n; i++) {
        free(v[i].url);
        free(v[i].name);
        free(v[i].audio);
    }
    free(v);
}

long long sp_url_clen(const char *url)
{
    const char *p = strstr(url, "clen=");
    while (p && p[-1] != '?' && p[-1] != '&')
        p = strstr(p + 5, "clen=");
    return p ? atoll(p + 5) : 0;
}

static char *make_url(int id)
{
    char *u;
    if (asprintf(&u, "http://127.0.0.1:%d/s/%d", port, id) < 0)
        return NULL;
    return u;
}

static char *add(int kind, const char *url, long long len, const char *referer, int flags, double start)
{
    pthread_mutex_lock(&lock);
    if (nentries == SP_MAX) {
        free(entries[0].url);
        free(entries[0].referer);
        memmove(entries, entries + 1, (SP_MAX - 1) * sizeof *entries);
        nentries--;
    }
    sp_entry *e = &entries[nentries++];
    e->id = next_id++;
    e->kind = kind;
    e->url = strdup(url);
    e->referer = referer ? strdup(referer) : NULL;
    e->length = len;
    e->flags = flags;
    e->start = start;
    char *u = make_url(e->id);
    pthread_mutex_unlock(&lock);
    return u;
}

char *sp_add_range(const char *url, long long length, const char *referer)
{
    return add(K_RANGE, url, length, referer, 0, 0);
}

char *sp_add_direct(const char *url)
{
    return add(K_DIRECT, url, 0, NULL, 0, 0);
}

char *sp_add_hls(const char *url, int flags, double start)
{
    return add(K_HLS, url, 0, NULL, flags, start);
}

static volatile time_t ad_seen;

/* 1 mientras el proxy está saltando un anuncio de Twitch */
int sp_in_ad(void)
{
    return ad_seen && time(NULL) - ad_seen < 8;
}

/* Twitch ya sirve algunos canales en fMP4 (#EXT-X-MAP) en vez de TS */
int sp_hls_fmp4(const char *media_url)
{
    sbuf b = {0};
    int r = !fetch_text(media_url, &b) && b.data && strstr(b.data, "#EXT-X-MAP:");
    free(b.data);
    return r;
}

double sp_hls_duration(const char *media_url)
{
    sbuf b = {0};
    if (fetch_text(media_url, &b) || !b.data) {
        free(b.data);
        return 0;
    }
    if (!strstr(b.data, "#EXT-X-ENDLIST")) {
        free(b.data);
        return 0;
    }
    double total = 0;
    char *save, *line;
    for (line = strtok_r(b.data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(line, "#EXTINF:", 8)) {
            double d = atof(line + 8);
            if (d > 0)
                total += d;
        }
    }
    free(b.data);
    return total;
}

static void stop_hs_locked(void);

void sp_clear(void)
{
    pthread_mutex_lock(&lock);
    for (int i = 0; i < nentries; i++) {
        free(entries[i].url);
        free(entries[i].referer);
    }
    nentries = 0;
    ad_seen = 0;
    pthread_mutex_unlock(&lock);
}

static int find(int id, sp_entry *copy)
{
    int ok = 0;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < nentries; i++)
        if (entries[i].id == id) {
            *copy = entries[i];
            copy->url = strdup(entries[i].url);
            copy->referer = entries[i].referer ? strdup(entries[i].referer) : NULL;
            ok = 1;
        }
    pthread_mutex_unlock(&lock);
    return ok;
}

typedef struct {
    sink k;
    sbuf head;
    long code;
    int started;
} direct_state;

static size_t direct_header(char *p, size_t s, size_t n, void *ud)
{
    direct_state *d = ud;
    size_t len = s * n;
    if (len >= 5 && !strncmp(p, "HTTP/", 5)) {
        d->head.len = 0;
        if (d->head.data)
            d->head.data[0] = 0;
        d->code = atol(p + 9);
    } else if (!strncasecmp(p, "Content-Length:", 15) || !strncasecmp(p, "Content-Range:", 14) ||
               !strncasecmp(p, "Content-Type:", 13) || !strncasecmp(p, "Accept-Ranges:", 14)) {
        sbuf_add(&d->head, p, len);
    }
    return len;
}

static void direct_start(direct_state *d)
{
    if (d->started)
        return;
    d->started = 1;
    char h[160];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %ld %s\r\nConnection: close\r\n", d->code ? d->code : 502,
                     d->code == 206 ? "Partial Content" : "OK");
    send_all(d->k.fd, h, n);
    if (d->head.data)
        send_all(d->k.fd, d->head.data, d->head.len);
    send_all(d->k.fd, "\r\n", 2);
}

static size_t direct_body(char *p, size_t s, size_t n, void *ud)
{
    direct_state *d = ud;
    if (d->code >= 300 && d->code < 400)
        return s * n;
    direct_start(d);
    return to_sock(p, s, n, &d->k);
}

static void serve_direct(int fd, sp_entry *e, long long a, long long b)
{
    direct_state d = {{fd, 0, 0}, {0}, 0, 0};
    CURL *c = new_curl(e->url, NULL);
    char range[64];
    if (a > 0 || b >= 0) {
        if (b >= 0)
            snprintf(range, sizeof range, "%lld-%lld", a, b);
        else
            snprintf(range, sizeof range, "%lld-", a);
        curl_easy_setopt(c, CURLOPT_RANGE, range);
    }
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, NULL);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, direct_header);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &d);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, direct_body);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
    curl_easy_perform(c);
    direct_start(&d);
    curl_easy_cleanup(c);
    free(d.head.data);
}

static void reply(int fd, const char *status, const char *extra)
{
    char h[512];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %s\r\nConnection: close\r\n%s\r\n", status, extra ? extra : "");
    send_all(fd, h, n);
}

static void serve_range(int fd, sp_entry *e, long long a, long long b)
{
    long long total = e->length;
    if (total > 0 && b < 0)
        b = total - 1;
    if (total > 0 && a >= total) {
        char x[96];
        snprintf(x, sizeof x, "Content-Range: bytes */%lld\r\n", total);
        reply(fd, "416 Range Not Satisfiable", x);
        return;
    }
    char hdr[512];
    if (total > 0)
        snprintf(hdr, sizeof hdr,
                 "Content-Type: application/octet-stream\r\nAccept-Ranges: bytes\r\n"
                 "Content-Length: %lld\r\nContent-Range: bytes %lld-%lld/%lld\r\n",
                 b - a + 1, a, b, total);
    else
        snprintf(hdr, sizeof hdr, "Content-Type: application/octet-stream\r\n");
    reply(fd, total > 0 && a > 0 ? "206 Partial Content" : "200 OK", hdr);

    struct curl_slist *hl = NULL;
    if (e->referer) {
        char line[256];
        snprintf(line, sizeof line, "Origin: %s", e->referer);
        hl = curl_slist_append(hl, line);
        snprintf(line, sizeof line, "Referer: %s/", e->referer);
        hl = curl_slist_append(hl, line);
    }
    long long pos = a;
    int rn = 0, fails = 0;
    sink k = {fd, 0, 0};
    while (!k.gone && (b < 0 || pos <= b)) {
        long long end = pos + SP_CHUNK - 1;
        if (b >= 0 && end > b)
            end = b;
        char *u;
        if (asprintf(&u, "%s%srange=%lld-%lld&rn=%d", e->url, strchr(e->url, '?') ? "&" : "?", pos, end,
                     rn++) < 0)
            break;
        CURL *c = new_curl(u, hl);
        k.sent = 0;
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_sock);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &k);
        CURLcode rc = curl_easy_perform(c);
        long code = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_cleanup(c);
        DBG("range %ld rc=%d +%lld pos=%lld", code, rc, k.sent, pos);
        free(u);
        pos += k.sent;
        if (k.gone)
            break;
        if (rc != CURLE_OK || code >= 400) {
            if (++fails > 4)
                break;
            sleep(1);
            continue;
        }
        fails = 0;
        if (b < 0 && k.sent < SP_CHUNK)
            break;
    }
    curl_slist_free_all(hl);
}

typedef struct {
    long long seq;
    double dur;
    char *uri;
    int ad;
} seg;

static int parse_media(const char *base, char *text, int skip_ads, seg **out, int *ended, double *target,
                       char **map)
{
    long long seq = 0;
    int n = 0, cap = 0;
    seg *s = NULL;
    double dur = 0;
    int ad = 0, have_inf = 0;
    char *save, *line;
    *ended = 0;
    *target = 2;
    for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        line[strcspn(line, "\r")] = 0;
        if (!strncmp(line, "#EXT-X-MEDIA-SEQUENCE:", 22))
            seq = atoll(line + 22);
        else if (!strncmp(line, "#EXT-X-TARGETDURATION:", 22))
            *target = atof(line + 22);
        else if (!strncmp(line, "#EXT-X-ENDLIST", 14))
            *ended = 1;
        else if (!strncmp(line, "#EXT-X-MAP:", 11) && map && !*map) {
            char a[2048];
            if (attr(line, "URI", a, sizeof a))
                *map = resolve(base, a);
        } else if (!strncmp(line, "#EXTINF:", 8)) {
            dur = atof(line + 8);
            const char *t = strchr(line, ',');
            ad = skip_ads && !(t && strstr(t, "live"));
            have_inf = 1;
        } else if (line[0] && line[0] != '#' && have_inf) {
            if (n == cap) {
                cap = cap ? cap * 2 : 32;
                s = realloc(s, cap * sizeof *s);
            }
            s[n].seq = seq++;
            s[n].dur = dur;
            s[n].uri = resolve(base, line);
            s[n].ad = ad;
            n++;
            have_inf = 0;
        }
    }
    *out = s;
    return n;
}

static int send_url(CURL *c, const char *url, sink *k)
{
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_sock);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, k);
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    DBG("seg %ld rc=%d sent=%lld %.100s", code, rc, k->sent, url);
    return rc == CURLE_OK && code < 400 ? 0 : -1;
}

/* YouTube's separate audio track: raw ADTS AAC segments whose time is only in an ID3 tag
   (com.apple.streaming.transportStreamTimestamp). mplayer would play them from 0 against a
   video that starts at the broadcast clock, so each frame goes out as MPEG-TS with its PTS. */
typedef struct {
    sbuf out;
    unsigned cc[3];
    long long pts;
} aac_ts;

static unsigned mpeg_crc(const unsigned char *p, size_t n)
{
    unsigned c = 0xffffffff;
    while (n--) {
        c ^= (unsigned)*p++ << 24;
        for (int i = 0; i < 8; i++)
            c = c & 0x80000000 ? (c << 1) ^ 0x04c11db7 : c << 1;
    }
    return c;
}

/* one 188-byte packet: payload up to 184 bytes, padded with adaptation-field stuffing;
   pcr >= 0 adds a PCR (only on the first packet of a PES) */
static size_t ts_packet(aac_ts *t, int ix, int pid, int start, long long pcr, const unsigned char *p, size_t n)
{
    unsigned char k[188];
    size_t af = pcr >= 0 ? 8 : 0;
    if (n > 184 - af)
        n = 184 - af;
    if (n < 184 && !af)
        af = 184 - n == 1 ? 1 : 2;
    if (af && n + af < 184)
        af = 184 - n;
    k[0] = 0x47;
    k[1] = (start ? 0x40 : 0) | pid >> 8;
    k[2] = pid & 0xff;
    k[3] = (af ? 0x30 : 0x10) | (t->cc[ix]++ & 15);
    if (af) {
        k[4] = af - 1;
        if (af > 1) {
            memset(k + 5, 0xff, af - 1);
            k[5] = 0;
            if (pcr >= 0) {
                k[5] = 0x10;
                k[6] = pcr >> 25, k[7] = pcr >> 17, k[8] = pcr >> 9, k[9] = pcr >> 1;
                k[10] = (pcr & 1) << 7 | 0x7e, k[11] = 0;
            }
        }
    }
    memcpy(k + 4 + af, p, n);
    sbuf_add(&t->out, (const char *)k, 188);
    return n;
}

static void ts_section(aac_ts *t, int ix, int pid, const unsigned char *sec, size_t n)
{
    unsigned char b[64];
    b[0] = 0;
    memcpy(b + 1, sec, n);
    unsigned c = mpeg_crc(sec, n);
    b[n + 1] = c >> 24, b[n + 2] = c >> 16, b[n + 3] = c >> 8, b[n + 4] = c;
    ts_packet(t, ix, pid, 1, -1, b, n + 5);
}

static void aac_to_ts(aac_ts *t, const unsigned char *d, size_t n)
{
    static const int rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
    static const unsigned char pat[] = {0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xe1, 0x00};
    static const unsigned char pmt[] = {0x02, 0xb0, 0x12, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xe1, 0x01,
                                        0xf0, 0x00, 0x0f, 0xe1, 0x01, 0xf0, 0x00};
    size_t i = 0;
    if (n > 10 && !memcmp(d, "ID3", 3)) {
        size_t sz = 10 + ((d[6] & 0x7f) << 21 | (d[7] & 0x7f) << 14 | (d[8] & 0x7f) << 7 | (d[9] & 0x7f));
        static const char key[] = "com.apple.streaming.transportStreamTimestamp";
        for (size_t j = 10; j + sizeof key + 8 <= sz && j + sizeof key + 8 <= n; j++)
            if (!memcmp(d + j, key, sizeof key)) {
                const unsigned char *q = d + j + sizeof key;
                long long v = 0;
                for (int b = 0; b < 8; b++)
                    v = v << 8 | q[b];
                t->pts = v & 0x1ffffffffLL;
                break;
            }
        i = sz;
    }
    ts_section(t, 0, 0, pat, sizeof pat);
    ts_section(t, 1, 0x100, pmt, sizeof pmt);
    while (i + 7 <= n) {
        if (d[i] != 0xff || (d[i + 1] & 0xf6) != 0xf0) {
            i++;
            continue;
        }
        size_t len = (d[i + 3] & 3) << 11 | d[i + 4] << 3 | d[i + 5] >> 5;
        int sr = (d[i + 2] >> 2) & 15;
        if (len < 7 || i + len > n || sr > 12)
            break;
        long long pts = t->pts < 0 ? 0 : t->pts;
        unsigned char h[14] = {0, 0, 1, 0xc0, (len + 8) >> 8, (len + 8) & 0xff, 0x80, 0x80, 5,
                               0x21 | (pts >> 29 & 0x0e), pts >> 22, (pts >> 14 & 0xfe) | 1, pts >> 7, (pts << 1 & 0xfe) | 1};
        unsigned char *pes = malloc(14 + len);
        memcpy(pes, h, 14);
        memcpy(pes + 14, d + i, len);
        size_t off = 0;
        while (off < 14 + len)
            off += ts_packet(t, 2, 0x101, off == 0, off == 0 ? pts : -1, pes + off, 14 + len - off);
        free(pes);
        t->pts = (pts + 1024LL * ((d[i + 6] & 3) + 1) * 90000 / rates[sr]) & 0x1ffffffffLL;
        i += len;
    }
}

static int send_aac_seg(const char *url, sink *k, aac_ts *t)
{
    sbuf b = {0};
    if (fetch_text(url, &b) || !b.data) {
        free(b.data);
        return -1;
    }
    t->out.len = 0;
    aac_to_ts(t, (const unsigned char *)b.data, b.len);
    free(b.data);
    if (send_all(k->fd, t->out.data, t->out.len)) {
        k->gone = 1;
        return -1;
    }
    k->sent += t->out.len;
    return 0;
}

static void serve_hls(int fd, sp_entry *e)
{
    aac_ts at = {{0}, {0}, -1};
    reply(fd, "200 OK", "Content-Type: video/mp2t\r\n");
    sink k = {fd, 0, 0};
    long long next = -1;
    int fails = 0, map_sent = 0;
    char *map = NULL;
    CURL *c = new_curl(e->url, NULL);
    long long last_sent = -1;
    time_t last_data = time(NULL);
    while (!k.gone) {
        sbuf b = {0};
        if (fetch_text(e->url, &b) || !b.data) {
            free(b.data);
            if (++fails > 6)
                break;
            sleep(2);
            continue;
        }
        fails = 0;
        seg *s;
        int ended;
        double target;
        int n = parse_media(e->url, b.data, e->flags & SP_SKIP_ADS, &s, &ended, &target, &map);
        free(b.data);
        if (map && !map_sent) {
            send_url(c, map, &k);
            map_sent = 1;
        }
        if (next < 0 && n) {
            int i = 0;
            if (!ended && (e->flags & SP_LIVE) && e->start <= 0) {
                i = n - 3 < 0 ? 0 : n - 3;
            } else {
                double t = 0;
                while (i < n - 1 && t + s[i].dur <= e->start)
                    t += s[i++].dur;
            }
            next = s[i].seq;
        }
        if (n && s[0].seq > next)
            next = s[0].seq;
        int got = 0;
        for (int i = 0; i < n && !k.gone; i++) {
            if (s[i].seq < next)
                continue;
            if (s[i].ad) {
                ad_seen = time(NULL);
                DBG("anuncio %lld %.1fs", s[i].seq, s[i].dur);
            } else {
                for (int tries = 0; tries < 3; tries++) {
                    long long before = k.sent;
                    int r = e->flags & SP_AAC_TS ? send_aac_seg(s[i].uri, &k, &at) : send_url(c, s[i].uri, &k);
                    if (!r || k.gone || k.sent != before)
                        break;
                }
            }
            next = s[i].seq + 1;
            got++;
        }
        for (int i = 0; i < n; i++)
            free(s[i].uri);
        free(s);
        if (ended)
            break;
        /* mplayer da fin de archivo si pasan ~14 s sin datos: mientras se salta un
           anuncio se manda relleno que el demuxer descarta */
        if (k.sent != last_sent) {
            last_sent = k.sent;
            last_data = time(NULL);
        } else if ((e->flags & SP_SKIP_ADS) && time(NULL) - last_data >= 2) {
            static const unsigned char free_box[16] = {0, 0, 0, 16, 'f', 'r', 'e', 'e'};
            unsigned char null_ts[188];
            memset(null_ts, 0xff, sizeof null_ts);
            null_ts[0] = 0x47, null_ts[1] = 0x1f, null_ts[2] = 0xff, null_ts[3] = 0x10;
            if (map ? send_all(fd, (const char *)free_box, sizeof free_box)
                    : send_all(fd, (const char *)null_ts, sizeof null_ts))
                k.gone = 1;
            last_data = time(NULL);
        }
        if (!got) {
            double w = target / 2;
            if (w < 1)
                w = 1;
            struct timespec ts = {(time_t)w, (long)((w - (int)w) * 1e9)};
            nanosleep(&ts, NULL);
        }
    }
    curl_easy_cleanup(c);
    free(map);
    free(at.out.data);
}

static void *conn_thread(void *arg)
{
    int fd = (int)(long)arg;
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 5);
    char req[8192];
    int len = 0;
    while (len < (int)sizeof req - 1) {
        ssize_t r = recv(fd, req + len, sizeof req - 1 - len, 0);
        if (r <= 0)
            break;
        len += r;
        req[len] = 0;
        if (strstr(req, "\r\n\r\n"))
            break;
    }
    req[len] = 0;
    DBG("req %.120s", req);
    int id = 0;
    long long a = 0, b = -1;
    if (sscanf(req, "GET /s/%d", &id) == 1 || sscanf(req, "HEAD /s/%d", &id) == 1) {
        const char *r = strcasestr(req, "\nRange: bytes=");
        if (r) {
            r += 14;
            a = atoll(r);
            const char *dash = strchr(r, '-');
            if (dash && dash[1] >= '0' && dash[1] <= '9')
                b = atoll(dash + 1);
        }
    }
    sp_entry e = {0};
    if (!id || !find(id, &e)) {
        reply(fd, "404 Not Found", "Content-Length: 0\r\n");
    } else {
        if (!strncmp(req, "HEAD", 4)) {
            char h[256];
            if (e.kind == K_RANGE && e.length > 0)
                snprintf(h, sizeof h, "Accept-Ranges: bytes\r\nContent-Length: %lld\r\n", e.length);
            else
                snprintf(h, sizeof h, "Content-Type: video/mp2t\r\n");
            reply(fd, "200 OK", h);
        } else if (e.kind == K_RANGE) {
            serve_range(fd, &e, a, b);
        } else if (e.kind == K_DIRECT) {
            serve_direct(fd, &e, a, b);
        } else {
            serve_hls(fd, &e);
        }
        free(e.url);
        free(e.referer);
    }
    shutdown(fd, SHUT_RDWR);
    close(fd);
    return NULL;
}

static void *accept_thread(void *arg)
{
    for (;;) {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            sleep(1);
            continue;
        }
        pthread_t t;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&at, 256 * 1024);
        if (pthread_create(&t, &at, conn_thread, (void *)(long)fd))
            close(fd);
        pthread_attr_destroy(&at);
    }
    return NULL;
}

int sp_port(void)
{
    return port;
}

int sp_start(void)
{
    if (listen_fd >= 0)
        return port;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listen_fd, (struct sockaddr *)&sa, sizeof sa) || listen(listen_fd, 16)) {
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }
    socklen_t sl = sizeof sa;
    getsockname(listen_fd, (struct sockaddr *)&sa, &sl);
    port = ntohs(sa.sin_port);
    pthread_t t;
    pthread_create(&t, NULL, accept_thread, NULL);
    pthread_detach(t);
    return port;
}

static pid_t hs_pid = -1;

static void stop_hs_locked(void)
{
    if (hs_pid > 0) {
        pid_t p = hs_pid;
        hs_pid = -1;
        kill(-p, SIGTERM);
        kill(p, SIGTERM);
        int st = 0;
        for (int i = 0; i < 20 && waitpid(p, &st, WNOHANG) == 0; i++)
            usleep(50000);
        if (waitpid(p, &st, WNOHANG) == 0) {
            kill(-p, SIGKILL);
            kill(p, SIGKILL);
            waitpid(p, &st, 0);
        }
    }
}

void sp_stop_hackstream(void)
{
    pthread_mutex_lock(&lock);
    stop_hs_locked();
    pthread_mutex_unlock(&lock);
    usleep(500000); /* dar tiempo al SO a cerrar los sockets del CDN */
}

static char *run_hackstream_proxy_url(const char *url)
{
    int pout[2];
    if (pipe(pout) != 0)
        return NULL;
    pid_t pid = fork();
    if (pid < 0) {
        close(pout[0]);
        close(pout[1]);
        return NULL;
    }
    if (pid == 0) {
        setpgid(0, 0);
        close(pout[0]);
        dup2(pout[1], STDOUT_FILENO);
        if (pout[1] != STDOUT_FILENO)
            close(pout[1]);
        if (!getenv("SP_DEBUG")) {
            int dn = open("/dev/null", O_WRONLY);
            if (dn >= 0) {
                dup2(dn, STDERR_FILENO);
                if (dn > STDERR_FILENO)
                    close(dn);
            }
        }
        const char *cands[] = {
            "./build/hackstream_proxy",
            "/home/j/Documents/tinytv2/build/hackstream_proxy",
            "/tmp/hackstream_proxy",
            "hackstream_proxy",
            NULL
        };
        for (int i = 0; cands[i]; i++) {
            if (strchr(cands[i], '/'))
                execl(cands[i], "hackstream_proxy", url, "--serve-only", (char *)NULL);
            else
                execlp(cands[i], "hackstream_proxy", url, "--serve-only", (char *)NULL);
        }
        _exit(127);
    }
    close(pout[1]);
    hs_pid = pid;

    char line[4096];
    size_t used = 0;
    while (used + 1 < sizeof(line)) {
        char ch = 0;
        ssize_t n = read(pout[0], &ch, 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0 || ch == '\n')
            break;
        if (ch != '\r')
            line[used++] = ch;
    }
    line[used] = '\0';
    close(pout[0]);

    if (strncmp(line, "http://", 7) != 0 && strncmp(line, "https://", 8) != 0) {
        if (getenv("SP_DEBUG"))
            fprintf(stderr, "[sp] hackstream_proxy fallo, salida: '%s'\n", line);
        stop_hs_locked();
        return NULL;
    }
    if (getenv("SP_DEBUG"))
        fprintf(stderr, "[sp] proxy OK: %s\n", line);
    return strdup(line);
}

char *sp_add_hackstream(const char *url)
{
    if (!url || !*url)
        return NULL;
    pthread_mutex_lock(&lock);
    char *proxy_url = run_hackstream_proxy_url(url);
    pthread_mutex_unlock(&lock);
    return proxy_url;
}
