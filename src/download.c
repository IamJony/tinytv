#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "download.h"

#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DL_UA "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0"
#define DL_CHUNK (1024 * 1024)
#define DL_TRIES 6

typedef struct {
    char *url;
    long long len;
    int fd;
    char *path;
} dl_part;

struct dl_job {
    dl_part part[2];
    int nparts;
    char *out;
    int container, threads;
    pthread_t master;
    pthread_mutex_t m;
    int state;
    long long done, total;
    char err[256];
    volatile int cancel;
    int failed;
    int next_part;        /* the next piece to fetch: part and offset */
    long long next_off;
    int rn;
};

static long long url_clen(const char *url)
{
    const char *p = strstr(url, "clen=");
    while (p && p[-1] != '?' && p[-1] != '&')
        p = strstr(p + 5, "clen=");
    return p ? atoll(p + 5) : 0;
}

static void set_error(dl_job *j, const char *fmt, const char *arg)
{
    pthread_mutex_lock(&j->m);
    if (!j->failed) {
        j->failed = 1;
        snprintf(j->err, sizeof j->err, fmt, arg ? arg : "");
    }
    pthread_mutex_unlock(&j->m);
}

// --- fetching ---------------------------------------------------------------------------------

typedef struct {
    dl_job *j;
    int fd;
    long long off, got, want;
} piece;

static size_t piece_write(char *p, size_t s, size_t n, void *ud)
{
    piece *k = ud;
    size_t len = s * n;
    if (k->j->cancel || k->got + (long long)len > k->want)
        return 0;
    size_t w = 0;
    while (w < len) {
        ssize_t r = pwrite(k->fd, p + w, len - w, k->off + k->got + w);
        if (r <= 0)
            return 0;
        w += r;
    }
    k->got += len;
    pthread_mutex_lock(&k->j->m);
    k->j->done += len;
    pthread_mutex_unlock(&k->j->m);
    return len;
}

static int piece_progress(void *ud, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    return ((dl_job *)ud)->cancel;
}

static void *worker(void *arg)
{
    dl_job *j = arg;
    CURL *c = curl_easy_init();
    struct curl_slist *hl = NULL;
    hl = curl_slist_append(hl, "Origin: https://www.youtube.com");
    hl = curl_slist_append(hl, "Referer: https://www.youtube.com/");
    for (;;) {
        pthread_mutex_lock(&j->m);
        while (j->next_part < j->nparts && j->next_off >= j->part[j->next_part].len) {
            j->next_part++;
            j->next_off = 0;
        }
        if (j->cancel || j->failed || j->next_part >= j->nparts) {
            pthread_mutex_unlock(&j->m);
            break;
        }
        dl_part *pt = &j->part[j->next_part];
        long long a = j->next_off, b = a + DL_CHUNK - 1;
        if (b >= pt->len)
            b = pt->len - 1;
        j->next_off = b + 1;
        pthread_mutex_unlock(&j->m);

        piece k = {j, pt->fd, a, 0, b - a + 1};
        int ok = 0;
        for (int t = 0; t < DL_TRIES && !ok && !j->cancel && !j->failed; t++) {
            if (t)
                sleep(t < 3 ? 1 : 3);
            // A failed try starts the piece again: what it had written is counted out.
            pthread_mutex_lock(&j->m);
            j->done -= k.got;
            int rn = j->rn++;
            pthread_mutex_unlock(&j->m);
            k.got = 0;
            char *u;
            if (asprintf(&u, "%s%srange=%lld-%lld&rn=%d", pt->url, strchr(pt->url, '?') ? "&" : "?", a, b, rn) < 0)
                break;
            curl_easy_reset(c);
            curl_easy_setopt(c, CURLOPT_URL, u);
            curl_easy_setopt(c, CURLOPT_USERAGENT, DL_UA);
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
            curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
            curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
            curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, piece_write);
            curl_easy_setopt(c, CURLOPT_WRITEDATA, &k);
            curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, piece_progress);
            curl_easy_setopt(c, CURLOPT_XFERINFODATA, j);
            CURLcode rc = curl_easy_perform(c);
            long code = 0;
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
            free(u);
            if (rc == CURLE_OK && code < 400 && k.got == k.want)
                ok = 1;
            else if (code == 403 || code == 404 || code == 410) {
                char why[64];
                snprintf(why, sizeof why, "%ld", code);
                set_error(j, "YouTube rechazó la descarga (HTTP %s). Abre el video otra vez y reintenta.", why);
            }
        }
        if (!ok && !j->cancel)
            set_error(j, "Se cortó la conexión y no se pudo seguir%s.", "");
    }
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    return NULL;
}

static void cleanup_parts(dl_job *j)
{
    for (int i = 0; i < j->nparts; i++) {
        if (j->part[i].fd >= 0)
            close(j->part[i].fd);
        j->part[i].fd = -1;
        if (j->part[i].path)
            unlink(j->part[i].path);
    }
}

static void *master(void *arg)
{
    dl_job *j = arg;
    for (int i = 0; i < j->nparts; i++) {
        dl_part *p = &j->part[i];
        if (p->len <= 0)
            p->len = url_clen(p->url);
        if (p->len <= 0) {
            set_error(j, "No se conoce el tamaño del archivo%s.", "");
            goto end;
        }
        if (asprintf(&p->path, "%s.%s.part", j->out, i == 0 && j->nparts == 2 ? "video" : "audio") < 0)
            goto end;
        p->fd = open(p->path, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (p->fd < 0 || ftruncate(p->fd, p->len) < 0) {
            set_error(j, "No se puede escribir en el directorio: %s", strerror(errno));
            goto end;
        }
        j->total += p->len;
    }
    {
        int n = j->threads < 1 ? 1 : j->threads > 16 ? 16 : j->threads;
        long long pieces = (j->total + DL_CHUNK - 1) / DL_CHUNK;
        if (n > pieces)
            n = (int)pieces;
        pthread_t th[16];
        int started = 0;
        for (int i = 0; i < n; i++)
            if (!pthread_create(&th[i], NULL, worker, j))
                started++;
            else
                break;
        for (int i = 0; i < started; i++)
            pthread_join(th[i], NULL);
    }
    if (j->cancel || j->failed)
        goto end;
    pthread_mutex_lock(&j->m);
    j->state = DL_JOINING;
    pthread_mutex_unlock(&j->m);
    for (int i = 0; i < j->nparts; i++) {
        close(j->part[i].fd);
        j->part[i].fd = -1;
    }
    {
        char *tmp, err[256] = "";
        if (asprintf(&tmp, "%s.part", j->out) < 0)
            goto end;
        const char *in[2] = {j->part[0].path, j->nparts > 1 ? j->part[1].path : NULL};
        int r = j->container == DL_WEBM ? dl_join_webm(in, j->nparts, tmp, err, sizeof err)
                                        : dl_join_mp4(in, j->nparts, tmp, err, sizeof err);
        if (r == 0 && rename(tmp, j->out) != 0) {
            snprintf(err, sizeof err, "No se pudo guardar el archivo: %s", strerror(errno));
            r = -1;
        }
        if (r != 0) {
            unlink(tmp);
            set_error(j, "%s", err);
        }
        free(tmp);
    }
end:
    cleanup_parts(j);
    pthread_mutex_lock(&j->m);
    j->state = j->cancel ? DL_CANCELLED : j->failed ? DL_ERROR : DL_DONE;
    pthread_mutex_unlock(&j->m);
    return NULL;
}

dl_job *dl_start(const dl_params *p)
{
    dl_job *j = calloc(1, sizeof *j);
    pthread_mutex_init(&j->m, NULL);
    j->out = strdup(p->out_path);
    j->container = p->container;
    j->threads = p->threads;
    const char *urls[2] = {p->video_url, p->audio_url};
    long long lens[2] = {p->video_len, p->audio_len};
    for (int i = 0; i < 2; i++)
        if (urls[i] && *urls[i]) {
            j->part[j->nparts].url = strdup(urls[i]);
            j->part[j->nparts].len = lens[i];
            j->part[j->nparts].fd = -1;
            j->nparts++;
        }
    j->state = DL_RUNNING;
    if (!j->nparts) {
        j->state = DL_ERROR;
        snprintf(j->err, sizeof j->err, "Nada que descargar.");
        return j;
    }
    if (pthread_create(&j->master, NULL, master, j)) {
        j->state = DL_ERROR;
        snprintf(j->err, sizeof j->err, "No se pudo iniciar la descarga.");
        j->nparts = 0;
    }
    return j;
}

void dl_get(dl_job *j, dl_status *s)
{
    pthread_mutex_lock(&j->m);
    s->state = j->state;
    s->done = j->done;
    s->total = j->total;
    snprintf(s->error, sizeof s->error, "%s", j->err);
    pthread_mutex_unlock(&j->m);
}

void dl_cancel(dl_job *j)
{
    j->cancel = 1;
}

void dl_free(dl_job *j)
{
    if (!j)
        return;
    if (j->nparts)
        pthread_join(j->master, NULL);
    for (int i = 0; i < 2; i++) {
        free(j->part[i].url);
        free(j->part[i].path);
    }
    free(j->out);
    pthread_mutex_destroy(&j->m);
    free(j);
}

// --- output helpers ---------------------------------------------------------------------------

typedef struct {
    FILE *f;
    int bad;
} outf;

static void put(outf *o, const void *p, size_t n)
{
    if (!o->bad && n && fwrite(p, 1, n, o->f) != n)
        o->bad = 1;
}

static int read_at(int fd, void *p, size_t n, long long off)
{
    size_t r = 0;
    while (r < n) {
        ssize_t x = pread(fd, (char *)p + r, n - r, off + r);
        if (x <= 0)
            return -1;
        r += x;
    }
    return 0;
}

static int copy_range(int fd, long long off, long long len, outf *o)
{
    static __thread char buf[65536];
    while (len > 0) {
        size_t n = len > (long long)sizeof buf ? sizeof buf : (size_t)len;
        if (read_at(fd, buf, n, off))
            return -1;
        put(o, buf, n);
        off += n;
        len -= n;
    }
    return o->bad ? -1 : 0;
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static uint64_t rd64(const unsigned char *p)
{
    return (uint64_t)rd32(p) << 32 | rd32(p + 4);
}

static void wr32(unsigned char *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static void wr64(unsigned char *p, uint64_t v)
{
    wr32(p, v >> 32);
    wr32(p + 4, (uint32_t)v);
}

// --- MP4 (fragmented, as googlevideo gives it) ------------------------------------------------

typedef struct {
    long long off, len;
    double t;
    int in;
} m4frag;

typedef struct {
    int fd;
    unsigned char *moov;
    uint32_t moov_len;
    unsigned char *mvhd, *trak, *trex;
    uint32_t trak_len, trex_len;
    uint32_t timescale, trex_dur;
    uint64_t dur;
    m4frag *fr;
    int nfr, cap;
} m4in;

// The child box `type` inside [p, p+len): its start (header included) and size.
static unsigned char *m4_child(unsigned char *p, uint32_t len, const char *type, uint32_t *size)
{
    uint32_t o = 0;
    while (o + 8 <= len) {
        uint32_t s = rd32(p + o);
        if (s < 8 || o + s > len)
            return NULL;
        if (!memcmp(p + o + 4, type, 4)) {
            if (size)
                *size = s;
            return p + o;
        }
        o += s;
    }
    return NULL;
}

// A moof's start time (seconds) and the end of its samples (track units), from tfdt and trun.
static void m4_moof_times(m4in *m, unsigned char *moof, uint32_t len, double *t)
{
    uint32_t tl;
    unsigned char *traf = m4_child(moof + 8, len - 8, "traf", &tl);
    *t = 0;
    if (!traf)
        return;
    uint32_t hl, dl, rl;
    unsigned char *tfhd = m4_child(traf + 8, tl - 8, "tfhd", &hl);
    unsigned char *tfdt = m4_child(traf + 8, tl - 8, "tfdt", &dl);
    uint64_t base = 0;
    if (tfdt && dl >= 16)
        base = tfdt[8] == 1 && dl >= 20 ? rd64(tfdt + 12) : rd32(tfdt + 12);
    if (m->timescale)
        *t = (double)base / m->timescale;
    uint32_t defdur = m->trex_dur;
    if (tfhd && hl >= 16) {
        uint32_t fl = rd32(tfhd + 8) & 0xffffff, o = 16;
        if (fl & 0x01)
            o += 8;
        if (fl & 0x02)
            o += 4;
        if ((fl & 0x08) && o + 4 <= hl)
            defdur = rd32(tfhd + o);
    }
    uint64_t sum = 0;
    unsigned char *trun = m4_child(traf + 8, tl - 8, "trun", &rl);
    if (trun && rl >= 16) {
        uint32_t fl = rd32(trun + 8) & 0xffffff, cnt = rd32(trun + 12), o = 16;
        if (fl & 0x01)
            o += 4;
        if (fl & 0x04)
            o += 4;
        uint32_t per = ((fl & 0x100) ? 4 : 0) + ((fl & 0x200) ? 4 : 0) + ((fl & 0x400) ? 4 : 0) + ((fl & 0x800) ? 4 : 0);
        for (uint32_t i = 0; i < cnt; i++) {
            if (fl & 0x100) {
                if (o + 4 > rl)
                    break;
                sum += rd32(trun + o);
            } else {
                sum += defdur;
            }
            o += per;
        }
    }
    if (base + sum > m->dur)
        m->dur = base + sum;
}

static int m4_open(m4in *m, const char *path, int idx, char *err, int errsz)
{
    memset(m, 0, sizeof *m);
    m->fd = open(path, O_RDONLY);
    if (m->fd < 0) {
        snprintf(err, errsz, "No se pudo abrir lo descargado.");
        return -1;
    }
    struct stat st;
    fstat(m->fd, &st);
    long long size = st.st_size, o = 0;
    m4frag *cur = NULL;
    while (o + 8 <= size) {
        unsigned char h[16];
        if (read_at(m->fd, h, 8, o))
            break;
        long long s = rd32(h), hs = 8;
        if (s == 1) {
            if (read_at(m->fd, h + 8, 8, o + 8))
                break;
            s = (long long)rd64(h + 8);
            hs = 16;
        } else if (s == 0) {
            s = size - o;
        }
        if (s < hs || o + s > size)
            break;
        if (!memcmp(h + 4, "moov", 4) && !m->moov && s < 16 * 1024 * 1024) {
            m->moov = malloc(s);
            m->moov_len = (uint32_t)s;
            if (read_at(m->fd, m->moov, s, o))
                break;
            m->mvhd = m4_child(m->moov + 8, m->moov_len - 8, "mvhd", NULL);
            m->trak = m4_child(m->moov + 8, m->moov_len - 8, "trak", &m->trak_len);
            uint32_t xl;
            unsigned char *mvex = m4_child(m->moov + 8, m->moov_len - 8, "mvex", &xl);
            if (mvex)
                m->trex = m4_child(mvex + 8, xl - 8, "trex", &m->trex_len);
            if (m->trex && m->trex_len >= 32)
                m->trex_dur = rd32(m->trex + 20);
            if (m->trak) {
                uint32_t al, nl;
                unsigned char *mdia = m4_child(m->trak + 8, m->trak_len - 8, "mdia", &al);
                unsigned char *mdhd = mdia ? m4_child(mdia + 8, al - 8, "mdhd", &nl) : NULL;
                if (mdhd && nl >= 32)
                    m->timescale = rd32(mdhd + (mdhd[8] == 1 ? 28 : 20));
            }
            cur = NULL;
        } else if (!memcmp(h + 4, "moof", 4) && s < 64 * 1024 * 1024) {
            unsigned char *b = malloc(s);
            if (!b || read_at(m->fd, b, s, o)) {
                free(b);
                break;
            }
            if (m->nfr == m->cap) {
                m->cap = m->cap ? m->cap * 2 : 256;
                m->fr = realloc(m->fr, m->cap * sizeof *m->fr);
            }
            cur = &m->fr[m->nfr++];
            cur->off = o;
            cur->len = s;
            cur->in = idx;
            m4_moof_times(m, b, (uint32_t)s, &cur->t);
            free(b);
        } else if (cur && (!memcmp(h + 4, "mdat", 4))) {
            cur->len = o + s - cur->off; // the samples go with their moof
        } else {
            cur = NULL; // sidx, mfra, free...: not copied
        }
        o += s;
    }
    if (!m->moov || !m->mvhd || !m->trak || !m->trex || !m->nfr || !m->timescale) {
        snprintf(err, errsz, "Lo descargado no es un MP4 fragmentado (no se puede unir).");
        return -1;
    }
    return 0;
}

static void m4_close(m4in *m)
{
    if (m->fd >= 0)
        close(m->fd);
    free(m->moov);
    free(m->fr);
}

static int frag_cmp(const void *a, const void *b)
{
    const m4frag *x = a, *y = b;
    if (x->t != y->t)
        return x->t < y->t ? -1 : 1;
    return x->in - y->in;
}

static void box_head(outf *o, uint32_t size, const char *type)
{
    unsigned char h[8];
    wr32(h, size);
    memcpy(h + 4, type, 4);
    put(o, h, 8);
}

int dl_join_mp4(const char **paths, int n, const char *out, char *err, int errsz)
{
    m4in in[2];
    int opened = 0, ret = -1;
    m4frag *all = NULL;
    outf o = {NULL, 0};
    for (; opened < n; opened++)
        if (m4_open(&in[opened], paths[opened], opened, err, errsz)) {
            opened++;
            goto done;
        }
    o.f = fopen(out, "wb");
    if (!o.f) {
        snprintf(err, errsz, "No se puede escribir %s: %s", out, strerror(errno));
        goto done;
    }
    setvbuf(o.f, NULL, _IOFBF, 256 * 1024);
    // ftyp
    {
        int video = 0;
        for (int i = 0; i < n; i++) {
            uint32_t al, hl;
            unsigned char *mdia = m4_child(in[i].trak + 8, in[i].trak_len - 8, "mdia", &al);
            unsigned char *hdlr = mdia ? m4_child(mdia + 8, al - 8, "hdlr", &hl) : NULL;
            if (hdlr && hl >= 20 && !memcmp(hdlr + 16, "vide", 4))
                video = 1;
        }
        const char *brands = video ? "isomiso2iso6mp41" : "M4A isomiso2iso6mp41";
        uint32_t bl = (uint32_t)strlen(brands);
        box_head(&o, 16 + bl, "ftyp");
        put(&o, video ? "isom" : "M4A ", 4);
        unsigned char minor[4] = {0, 0, 2, 0};
        put(&o, minor, 4);
        put(&o, brands, bl);
    }
    // moov: the first file's mvhd, the tracks numbered 1..n, mvex with the length (mehd)
    {
        unsigned char *mvhd = in[0].mvhd;
        uint32_t mvl = rd32(mvhd), v = mvhd[8];
        uint32_t mts = rd32(mvhd + (v == 1 ? 28 : 20));
        if (!mts)
            mts = 1000;
        double secs = 0;
        for (int i = 0; i < n; i++)
            if ((double)in[i].dur / in[i].timescale > secs)
                secs = (double)in[i].dur / in[i].timescale;
        uint64_t mdur = (uint64_t)(secs * mts + 0.5);
        unsigned char *mv = malloc(mvl);
        memcpy(mv, mvhd, mvl);
        if (v == 1)
            wr64(mv + 32, mdur);
        else
            wr32(mv + 24, (uint32_t)mdur);
        wr32(mv + mvl - 4, n + 1);
        uint32_t total = 8 + mvl + 8 + 20 + n * 32;
        for (int i = 0; i < n; i++)
            total += in[i].trak_len;
        box_head(&o, total, "moov");
        put(&o, mv, mvl);
        free(mv);
        for (int i = 0; i < n; i++) {
            unsigned char *t = malloc(in[i].trak_len);
            memcpy(t, in[i].trak, in[i].trak_len);
            uint32_t kl, al, nl;
            unsigned char *tkhd = m4_child(t + 8, in[i].trak_len - 8, "tkhd", &kl);
            if (tkhd && kl >= 32) {
                int tv = tkhd[8] == 1;
                wr32(tkhd + (tv ? 28 : 20), i + 1);
                if (tv)
                    wr64(tkhd + 36, mdur);
                else
                    wr32(tkhd + 28, (uint32_t)mdur);
            }
            unsigned char *mdia = m4_child(t + 8, in[i].trak_len - 8, "mdia", &al);
            unsigned char *mdhd = mdia ? m4_child(mdia + 8, al - 8, "mdhd", &nl) : NULL;
            if (mdhd && nl >= 32) {
                if (mdhd[8] == 1)
                    wr64(mdhd + 32, in[i].dur);
                else
                    wr32(mdhd + 24, (uint32_t)in[i].dur);
            }
            put(&o, t, in[i].trak_len);
            free(t);
        }
        box_head(&o, 8 + 20 + n * 32, "mvex");
        unsigned char mehd[20] = {0, 0, 0, 20, 'm', 'e', 'h', 'd', 1, 0, 0, 0};
        wr64(mehd + 12, mdur);
        put(&o, mehd, 20);
        for (int i = 0; i < n; i++) {
            unsigned char tx[32];
            memcpy(tx, in[i].trex, 32);
            wr32(tx, 32);
            wr32(tx + 12, i + 1);
            put(&o, tx, 32);
        }
    }
    // The fragments of both files in time order, each one with its track number.
    {
        int count = 0;
        for (int i = 0; i < n; i++)
            count += in[i].nfr;
        all = malloc(count * sizeof *all);
        count = 0;
        for (int i = 0; i < n; i++) {
            memcpy(all + count, in[i].fr, in[i].nfr * sizeof *all);
            count += in[i].nfr;
        }
        qsort(all, count, sizeof *all, frag_cmp);
        uint32_t seq = 1;
        for (int f = 0; f < count && !o.bad; f++) {
            m4in *m = &in[all[f].in];
            unsigned char h[8];
            if (read_at(m->fd, h, 8, all[f].off))
                break;
            uint32_t ml = rd32(h);
            unsigned char *moof = malloc(ml);
            if (read_at(m->fd, moof, ml, all[f].off)) {
                free(moof);
                break;
            }
            uint32_t fl;
            unsigned char *mfhd = m4_child(moof + 8, ml - 8, "mfhd", &fl);
            if (mfhd && fl >= 16)
                wr32(mfhd + 12, seq++);
            uint32_t p = 8;
            while (p + 8 <= ml) { // every traf
                uint32_t s = rd32(moof + p);
                if (s < 8 || p + s > ml)
                    break;
                if (!memcmp(moof + p + 4, "traf", 4)) {
                    uint32_t hl;
                    unsigned char *tfhd = m4_child(moof + p + 8, s - 8, "tfhd", &hl);
                    if (tfhd && hl >= 16)
                        wr32(tfhd + 12, all[f].in + 1);
                }
                p += s;
            }
            put(&o, moof, ml);
            free(moof);
            if (copy_range(m->fd, all[f].off + ml, all[f].len - ml, &o))
                break;
            if (f == count - 1)
                ret = 0;
        }
    }
    if (o.bad || ret) {
        ret = -1;
        snprintf(err, errsz, "No se pudo escribir el archivo final (¿disco lleno?).");
    }
done:
    if (o.f && fclose(o.f) && !ret) {
        ret = -1;
        snprintf(err, errsz, "No se pudo escribir el archivo final (¿disco lleno?).");
    }
    for (int i = 0; i < opened; i++)
        m4_close(&in[i]);
    free(all);
    return ret;
}

// --- WebM (Matroska) ----------------------------------------------------------------------

#define ID_EBML 0x1A45DFA3
#define ID_SEGMENT 0x18538067
#define ID_SEEKHEAD 0x114D9B74
#define ID_SEEK 0x4DBB
#define ID_SEEKID 0x53AB
#define ID_SEEKPOS 0x53AC
#define ID_INFO 0x1549A966
#define ID_TCSCALE 0x2AD7B1
#define ID_DURATION 0x4489
#define ID_TRACKS 0x1654AE6B
#define ID_TRACKENTRY 0xAE
#define ID_TRACKNUM 0xD7
#define ID_TRACKUID 0x73C5
#define ID_CLUSTER 0x1F43B675
#define ID_TIMECODE 0xE7
#define ID_SIMPLEBLOCK 0xA3
#define ID_BLOCKGROUP 0xA0
#define ID_BLOCK 0xA1
#define ID_CUES 0x1C53BB6B
#define ID_CUEPOINT 0xBB
#define ID_CUETIME 0xB3
#define ID_CUEPOS 0xB7
#define ID_CUETRACK 0xF7
#define ID_CUECLUSTER 0xF1

// An element id (markers kept) or size (marker removed) at p; its length in *l. 0 on error.
static int vint(const unsigned char *p, const unsigned char *end, int keep, uint64_t *v)
{
    if (p >= end || !*p)
        return 0;
    int l = 1;
    unsigned m = 0x80;
    while (!(p[0] & m)) {
        m >>= 1;
        l++;
    }
    if (p + l > end)
        return 0;
    uint64_t x = keep ? p[0] : (p[0] & (m - 1));
    int all_ones = (p[0] & (m - 1)) == m - 1;
    for (int i = 1; i < l; i++) {
        x = x << 8 | p[i];
        all_ones = all_ones && p[i] == 0xff;
    }
    *v = !keep && all_ones ? UINT64_MAX : x; // unknown size
    return l;
}

// An element's id and size read from a file at `off`; returns the header length (0 on error).
static int ebml_head_at(int fd, long long off, long long size, uint64_t *id, uint64_t *len)
{
    unsigned char h[12];
    size_t n = size - off < 12 ? (size_t)(size - off) : 12;
    if (n < 2 || read_at(fd, h, n, off))
        return 0;
    int a = vint(h, h + n, 1, id);
    int b = a ? vint(h + a, h + n, 0, len) : 0;
    return a && b ? a + b : 0;
}

typedef struct {
    long long off, len;
    uint64_t tc;
    int in;
} wbcluster;

typedef struct {
    int fd;
    long long size;
    unsigned char *ebml, *info, *tracks;
    uint64_t ebml_len, info_len, tracks_len;
    long long seg_data;
    uint64_t tcscale;
    double duration; // in timecode units
    wbcluster *cl;
    int ncl, cap;
} wbin;

static unsigned char *read_elem(int fd, long long off, uint64_t len)
{
    if (len > 8 * 1024 * 1024)
        return NULL;
    unsigned char *b = malloc(len ? len : 1);
    if (b && read_at(fd, b, len, off)) {
        free(b);
        return NULL;
    }
    return b;
}

static uint64_t uint_be(const unsigned char *p, uint64_t n)
{
    uint64_t v = 0;
    for (uint64_t i = 0; i < n && i < 8; i++)
        v = v << 8 | p[i];
    return v;
}

static double float_be(const unsigned char *p, uint64_t n)
{
    if (n == 4) {
        uint32_t u = rd32(p);
        float f;
        memcpy(&f, &u, 4);
        return f;
    }
    if (n == 8) {
        uint64_t u = rd64(p);
        double d;
        memcpy(&d, &u, 8);
        return d;
    }
    return 0;
}

static int wb_open(wbin *w, const char *path, int idx, char *err, int errsz)
{
    memset(w, 0, sizeof *w);
    w->tcscale = 1000000;
    w->fd = open(path, O_RDONLY);
    if (w->fd < 0) {
        snprintf(err, errsz, "No se pudo abrir lo descargado.");
        return -1;
    }
    struct stat st;
    fstat(w->fd, &st);
    w->size = st.st_size;
    uint64_t id, len;
    int h = ebml_head_at(w->fd, 0, w->size, &id, &len);
    if (!h || id != ID_EBML || len > 4096)
        goto bad;
    w->ebml_len = h + len;
    w->ebml = read_elem(w->fd, 0, w->ebml_len);
    long long o = w->ebml_len;
    h = ebml_head_at(w->fd, o, w->size, &id, &len);
    if (!h || id != ID_SEGMENT)
        goto bad;
    w->seg_data = o + h;
    long long end = len == UINT64_MAX || (long long)(o + h + len) > w->size ? w->size : (long long)(o + h + len);
    o += h;
    while (o < end) {
        h = ebml_head_at(w->fd, o, end, &id, &len);
        if (!h)
            break;
        if (len == UINT64_MAX) {
            snprintf(err, errsz, "WebM con partes de largo desconocido (no se puede unir).");
            return -1;
        }
        if (id == ID_INFO && !w->info) {
            w->info = read_elem(w->fd, o + h, len);
            w->info_len = len;
            const unsigned char *p = w->info, *e = w->info ? w->info + len : NULL;
            while (p && p < e) {
                uint64_t cid, cl;
                int a = vint(p, e, 1, &cid), b = a ? vint(p + a, e, 0, &cl) : 0;
                if (!a || !b || p + a + b + cl > e)
                    break;
                if (cid == ID_TCSCALE)
                    w->tcscale = uint_be(p + a + b, cl);
                else if (cid == ID_DURATION)
                    w->duration = float_be(p + a + b, cl);
                p += a + b + cl;
            }
        } else if (id == ID_TRACKS && !w->tracks) {
            w->tracks = read_elem(w->fd, o + h, len);
            w->tracks_len = len;
        } else if (id == ID_CLUSTER) {
            if (w->ncl == w->cap) {
                w->cap = w->cap ? w->cap * 2 : 256;
                w->cl = realloc(w->cl, w->cap * sizeof *w->cl);
            }
            wbcluster *c = &w->cl[w->ncl++];
            c->off = o;
            c->len = h + len;
            c->in = idx;
            c->tc = 0;
            uint64_t cid, cl;
            int a = ebml_head_at(w->fd, o + h, end, &cid, &cl);
            if (a && cid == ID_TIMECODE && cl <= 8) {
                unsigned char v[8];
                if (!read_at(w->fd, v, cl, o + h + a))
                    c->tc = uint_be(v, cl);
            }
        }
        o += h + len;
    }
    if (!w->info || !w->tracks || !w->ncl)
        goto bad;
    return 0;
bad:
    snprintf(err, errsz, "Lo descargado no es un WebM válido (no se puede unir).");
    return -1;
}

static void wb_close(wbin *w)
{
    if (w->fd >= 0)
        close(w->fd);
    free(w->ebml);
    free(w->info);
    free(w->tracks);
    free(w->cl);
}

typedef struct {
    unsigned char *p;
    size_t len, cap;
} mbuf;

static void mb_add(mbuf *b, const void *p, size_t n)
{
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2 + 64;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->len, p, n);
    b->len += n;
}

static void mb_id(mbuf *b, uint32_t id)
{
    unsigned char t[4];
    int n = id > 0xFFFFFF ? 4 : id > 0xFFFF ? 3 : id > 0xFF ? 2 : 1;
    for (int i = 0; i < n; i++)
        t[i] = id >> (8 * (n - 1 - i));
    mb_add(b, t, n);
}

static void mb_size8(mbuf *b, uint64_t v)
{
    unsigned char t[8] = {0x01};
    for (int i = 1; i < 8; i++)
        t[i] = v >> (8 * (7 - i));
    mb_add(b, t, 8);
}

static void mb_uint(mbuf *b, uint32_t id, uint64_t v)
{
    unsigned char t[8];
    mb_id(b, id);
    mb_size8(b, 8);
    wr64(t, v);
    mb_add(b, t, 8);
}

static void mb_elem(mbuf *b, uint32_t id, const mbuf *body)
{
    mb_id(b, id);
    mb_size8(b, body->len);
    mb_add(b, body->p, body->len);
}

// Reads the blocks of a WebM file one by one, a cluster at a time.
typedef struct {
    wbin *w;
    int idx, ci;
    unsigned char *buf;
    long long blen, pos;
    uint64_t ctc;
    unsigned char *el, *blk; // the block element (header included) and its data (track number first)
    uint64_t ellen;
    uint64_t t;
    int key, have;
} brd;

// 1: a block, 0: no more, -1: damaged file.
static int brd_next(brd *r)
{
    r->have = 0;
    for (;;) {
        if (r->buf && r->pos < r->blen) {
            uint64_t id, len;
            unsigned char *p = r->buf + r->pos, *e = r->buf + r->blen;
            int a = vint(p, e, 1, &id), b = a ? vint(p + a, e, 0, &len) : 0;
            if (!a || !b || p + a + b + len > e)
                return -1;
            r->pos += a + b + len;
            unsigned char *d = p + a + b, *blk = NULL;
            int key = 0;
            if (id == ID_TIMECODE) {
                r->ctc = uint_be(d, len);
            } else if (id == ID_SIMPLEBLOCK) {
                blk = d;
                key = len > 3 && (d[3] & 0x80);
            } else if (id == ID_BLOCKGROUP) {
                key = 1;
                unsigned char *q = d, *qe = d + len;
                while (q < qe) {
                    uint64_t gid, gl;
                    int u = vint(q, qe, 1, &gid), v = u ? vint(q + u, qe, 0, &gl) : 0;
                    if (!u || !v || q + u + v + gl > qe)
                        break;
                    if (gid == ID_BLOCK)
                        blk = q + u + v;
                    else if (gid == 0xFB) // ReferenceBlock: not a key frame
                        key = 0;
                    q += u + v + gl;
                }
            }
            if (blk && (blk[0] & 0x80) && blk + 4 <= e) {
                r->el = p;
                r->ellen = a + b + len;
                r->blk = blk;
                r->t = r->ctc + (int16_t)(blk[1] << 8 | blk[2]);
                r->key = key;
                r->have = 1;
                return 1;
            }
            continue;
        }
        free(r->buf);
        r->buf = NULL;
        if (++r->ci >= r->w->ncl)
            return 0;
        wbcluster *c = &r->w->cl[r->ci];
        if (c->len > 256LL * 1024 * 1024 || !(r->buf = malloc(c->len)) || read_at(r->w->fd, r->buf, c->len, c->off))
            return -1;
        uint64_t id, len;
        int a = vint(r->buf, r->buf + c->len, 1, &id), b = a ? vint(r->buf + a, r->buf + c->len, 0, &len) : 0;
        if (!a || !b)
            return -1;
        r->blen = c->len;
        r->pos = a + b;
        r->ctc = c->tc;
    }
}

int dl_join_webm(const char **paths, int n, const char *out, char *err, int errsz)
{
    wbin in[2];
    int opened = 0, ret = -1;
    outf o = {NULL, 0};
    mbuf cues = {0}, tmp = {0};
    for (; opened < n; opened++)
        if (wb_open(&in[opened], paths[opened], opened, err, errsz)) {
            opened++;
            goto done;
        }
    for (int i = 1; i < n; i++)
        if (in[i].tcscale != in[0].tcscale) {
            snprintf(err, errsz, "Video y audio con escalas de tiempo distintas (no se puede unir).");
            goto done;
        }
    o.f = fopen(out, "wb");
    if (!o.f) {
        snprintf(err, errsz, "No se puede escribir %s: %s", out, strerror(errno));
        goto done;
    }
    setvbuf(o.f, NULL, _IOFBF, 256 * 1024);
    put(&o, in[0].ebml, in[0].ebml_len);
    unsigned char seg[12] = {0x18, 0x53, 0x80, 0x67, 0x01};
    long long seg_pos = ftello(o.f);
    put(&o, seg, 12);
    long long seg_data = seg_pos + 12;

    // SeekHead: Info, Tracks and Cues (the last one filled in at the end).
    mbuf sh = {0}, one = {0};
    uint32_t ids[3] = {ID_INFO, ID_TRACKS, ID_CUES};
    long long pos_at[3];
    for (int k = 0; k < 3; k++) {
        one.len = 0;
        mb_id(&one, ID_SEEKID);
        mb_size8(&one, 4);
        unsigned char t[4];
        wr32(t, ids[k]);
        mb_add(&one, t, 4);
        mb_uint(&one, ID_SEEKPOS, 0);
        pos_at[k] = sh.len + 12 /* seekhead header */ + 10 /* seek header */ + one.len - 8;
        mb_elem(&sh, ID_SEEK, &one);
    }
    tmp.len = 0;
    mb_elem(&tmp, ID_SEEKHEAD, &sh);
    long long sh_pos = ftello(o.f);
    long long info_pos = sh_pos + tmp.len;
    // Info: the first file's, with the length of the longest one.
    mbuf info = {0};
    {
        const unsigned char *p = in[0].info, *e = p + in[0].info_len;
        while (p < e) {
            uint64_t cid, cl;
            int a = vint(p, e, 1, &cid), b = a ? vint(p + a, e, 0, &cl) : 0;
            if (!a || !b || p + a + b + cl > e)
                break;
            if (cid != ID_DURATION)
                mb_add(&info, p, a + b + cl);
            p += a + b + cl;
        }
        double d = 0;
        for (int i = 0; i < n; i++)
            if (in[i].duration > d)
                d = in[i].duration;
        if (d > 0) {
            uint64_t u;
            memcpy(&u, &d, 8);
            unsigned char t[8];
            wr64(t, u);
            mb_id(&info, ID_DURATION);
            mb_size8(&info, 8);
            mb_add(&info, t, 8);
        }
    }
    mbuf infoel = {0};
    mb_elem(&infoel, ID_INFO, &info);
    long long tracks_pos = info_pos + infoel.len;
    // Tracks: each file's entries, numbered 1..n.
    mbuf tracks = {0};
    for (int i = 0; i < n; i++) {
        const unsigned char *p = in[i].tracks, *e = p + in[i].tracks_len;
        while (p < e) {
            uint64_t cid, cl;
            int a = vint(p, e, 1, &cid), b = a ? vint(p + a, e, 0, &cl) : 0;
            if (!a || !b || p + a + b + cl > e)
                break;
            if (cid == ID_TRACKENTRY) {
                mbuf te = {0};
                const unsigned char *q = p + a + b, *qe = q + cl;
                while (q < qe) {
                    uint64_t did, dl;
                    int c = vint(q, qe, 1, &did), d = c ? vint(q + c, qe, 0, &dl) : 0;
                    if (!c || !d || q + c + d + dl > qe)
                        break;
                    if (did == ID_TRACKNUM)
                        mb_uint(&te, ID_TRACKNUM, i + 1);
                    else if (did == ID_TRACKUID)
                        mb_uint(&te, ID_TRACKUID, i + 1);
                    else
                        mb_add(&te, q, c + d + dl);
                    q += c + d + dl;
                }
                mb_elem(&tracks, ID_TRACKENTRY, &te);
                free(te.p);
                break; // one track per file
            }
            p += a + b + cl;
        }
    }
    mbuf tracksel = {0};
    mb_elem(&tracksel, ID_TRACKS, &tracks);
    // Positions are counted from the start of the segment's data.
    for (int k = 0; k < 2; k++)
        wr64(tmp.p + pos_at[k], (k == 0 ? info_pos : tracks_pos) - seg_data);
    put(&o, tmp.p, tmp.len);
    put(&o, infoel.p, infoel.len);
    put(&o, tracksel.p, tracksel.len);
    free(sh.p);
    free(one.p);
    free(info.p);
    free(infoel.p);
    free(tracks.p);
    free(tracksel.p);

    // The blocks of both files in time order, in new clusters that start at each key frame
    // of the video (players seek to a cluster: there they must find the audio too).
    brd rd[2];
    for (int i = 0; i < n; i++) {
        memset(&rd[i], 0, sizeof rd[i]);
        rd[i].w = &in[i];
        rd[i].idx = i;
        rd[i].ci = -1;
        brd_next(&rd[i]);
    }
    mbuf cl = {0}, cp = {0};
    uint64_t ctc = 0;
    int cl_key = 0, ok = 1;
    uint64_t span = 30000000000ULL / in[0].tcscale; // at most 30 s per cluster
    uint64_t audio_span = 5000000000ULL / in[0].tcscale;
    for (;;) {
        brd *r = NULL;
        for (int i = 0; i < n; i++)
            if (rd[i].have && (!r || rd[i].t < r->t))
                r = &rd[i];
        int video_key = r && r->idx == 0 && r->key && n > 1;
        if (cl.len && (!r || video_key || r->t - ctc >= (n > 1 ? span : audio_span) || cl.len > 32 * 1024 * 1024)) {
            long long at = ftello(o.f);
            if (cl_key) {
                one.p = NULL;
                one.len = one.cap = 0;
                mb_uint(&one, ID_CUETRACK, 1);
                mb_uint(&one, ID_CUECLUSTER, at - seg_data);
                cp.len = 0;
                mb_uint(&cp, ID_CUETIME, ctc);
                mb_elem(&cp, ID_CUEPOS, &one);
                mb_elem(&cues, ID_CUEPOINT, &cp);
                free(one.p);
            }
            tmp.len = 0;
            mb_id(&tmp, ID_CLUSTER);
            mb_size8(&tmp, cl.len);
            put(&o, tmp.p, tmp.len);
            put(&o, cl.p, cl.len);
            cl.len = 0;
        }
        if (!r || o.bad)
            break;
        if (!cl.len) {
            ctc = r->t;
            cl_key = n == 1 || video_key;
            mb_uint(&cl, ID_TIMECODE, ctc);
        }
        // the track number and the time relative to the new cluster
        int64_t rel = (int64_t)(r->t - ctc);
        r->blk[0] = 0x80 | (r->idx + 1);
        r->blk[1] = (unsigned char)(rel >> 8);
        r->blk[2] = (unsigned char)rel;
        mb_add(&cl, r->el, r->ellen);
        if (brd_next(r) < 0)
            ok = 0;
    }
    for (int i = 0; i < n; i++)
        free(rd[i].buf);
    free(cl.p);
    free(cp.p);
    if (!ok || o.bad)
        goto write_err;
    {
        long long cues_pos = ftello(o.f);
        tmp.len = 0;
        mb_elem(&tmp, ID_CUES, &cues);
        put(&o, tmp.p, tmp.len);
        long long end = ftello(o.f);
        unsigned char t[8];
        // segment size and the Cues position in the SeekHead
        if (fseeko(o.f, seg_pos + 4, SEEK_SET))
            goto write_err;
        unsigned char sz[8] = {0x01};
        for (int i = 1; i < 8; i++)
            sz[i] = (uint64_t)(end - seg_data) >> (8 * (7 - i));
        put(&o, sz, 8);
        if (fseeko(o.f, sh_pos + pos_at[2], SEEK_SET))
            goto write_err;
        wr64(t, cues_pos - seg_data);
        put(&o, t, 8);
        if (o.bad)
            goto write_err;
        ret = 0;
    }
    goto done;
write_err:
    snprintf(err, errsz, "No se pudo escribir el archivo final (¿disco lleno?).");
done:
    if (o.f && fclose(o.f) && !ret) {
        ret = -1;
        snprintf(err, errsz, "No se pudo escribir el archivo final (¿disco lleno?).");
    }
    for (int i = 0; i < opened; i++)
        wb_close(&in[i]);
    free(cues.p);
    free(tmp.p);
    return ret;
}
