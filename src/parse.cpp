#include "parse.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "stream.h"

static bool is_hackstream_url(const std::string &u)
{
    return u.find("hackstream.io/watch/") != std::string::npos
        || u.find("odysee.com/") != std::string::npos
        || u.find("odysee.tv/") != std::string::npos
        || u.find("lbry.tv/") != std::string::npos
        || u.rfind("lbry://", 0) == 0;
}

std::string jstr(cJSON *o, const char *k)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

long long jnum(cJSON *o, const char *k, long long def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (cJSON_IsNumber(v))
        return (long long)v->valuedouble;
    if (cJSON_IsString(v) && v->valuestring && *v->valuestring) {
        char *end = nullptr;
        long long n = strtoll(v->valuestring, &end, 10);
        if (end && !*end)
            return n;
    }
    return def;
}

bool jbool(cJSON *o, const char *k)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, k));
}

std::string error_of(cJSON *root)
{
    if (!root)
        return "Respuesta inválida del plugin";
    if (!jbool(root, "__error"))
        return "";
    std::string type = jstr(root, "type"), msg = jstr(root, "message");
    if (type.find("Captcha") != std::string::npos)
        return CAPTCHA_ERROR;
    if (type.find("LoginRequired") != std::string::npos)
        return "Este contenido requiere iniciar sesión.\n" + msg;
    if (type.find("Age") != std::string::npos)
        return "Contenido con restricción de edad: inicia sesión.";
    if (type.find("Unavailable") != std::string::npos)
        return "No disponible: " + msg;
    if (type == "NotImplemented")
        return "Esta función no está disponible en " + msg;
    return msg.empty() ? type : msg;
}

static std::string pick_thumb(cJSON *thumbs, int want)
{
    cJSON *src = cJSON_GetObjectItemCaseSensitive(thumbs, "sources");
    std::string best;
    long long bq = -1;
    cJSON *t;
    cJSON_ArrayForEach(t, src)
    {
        long long q = jnum(t, "quality", 0);
        std::string u = jstr(t, "url");
        if (u.empty())
            continue;
        bool better = best.empty() || (q >= want && (bq < want || q < bq)) || (bq < want && q > bq);
        if (better) {
            best = u;
            bq = q;
        }
    }
    return best;
}

Item parse_item(cJSON *o, int plat)
{
    Item it;
    it.plat = plat;
    std::string type = jstr(o, "__type");
    cJSON *id = cJSON_GetObjectItemCaseSensitive(o, "id");
    it.id = jstr(id, "value");
    it.title = jstr(o, "name");
    it.url = jstr(o, "url");
    if (type == "PlatformChannel") {
        it.kind = "channel";
        it.thumb = jstr(o, "thumbnail");
        it.subs = jnum(o, "subscribers");
        it.author = it.title;
        it.author_url = it.url;
        return it;
    }
    long long ct = jnum(o, "contentType", 1);
    it.kind = ct == 4 ? "playlist" : ct == 1 ? "video" : "other";
    it.thumb = pick_thumb(cJSON_GetObjectItemCaseSensitive(o, "thumbnails"), 180);
    if (plat == P_YOUTUBE && !it.id.empty() && it.kind == "video")
        it.thumb = "https://i.ytimg.com/vi/" + it.id + "/mqdefault.jpg";
    if (it.kind == "playlist")
        it.album = jstr(o, "sansonSubtitle");
    else if (plat == P_MUSIC)
        it.album = jstr(o, "sansonAlbum");
    it.album_url = jstr(o, "sansonAlbumUrl");
    it.note = jstr(o, "sansonGame");
    cJSON *a = cJSON_GetObjectItemCaseSensitive(o, "author");
    it.author = jstr(a, "name");
    it.author_url = jstr(a, "url");
    it.author_thumb = jstr(a, "thumbnail");
    it.duration = (int)jnum(o, "duration", 0);
    it.views = jnum(o, "viewCount");
    it.date = jnum(o, "datetime", 0);
    if (!it.date)
        it.date = jnum(o, "uploadDate", 0);
    it.live = jbool(o, "isLive");
    return it;
}

static void parse_items(std::vector<Item> &out, cJSON *list, int plat)
{
    cJSON *o;
    cJSON_ArrayForEach(o, list)
    {
        Item it = parse_item(o, plat);
        if ((it.kind == "video" || it.kind == "channel" || it.kind == "playlist") && !it.url.empty())
            out.push_back(it);
    }
}

Page parse_page(const char *json, int plat)
{
    Page pg;
    pg.plat = plat;
    cJSON *root = cJSON_Parse(json ? json : "");
    pg.error = error_of(root);
    if (pg.error.empty()) {
        pg.pager = (int)jnum(root, "__pager", 0);
        pg.more = jbool(root, "hasMore");
        parse_items(pg.items, cJSON_GetObjectItemCaseSensitive(root, "results"), plat);
    }
    cJSON_Delete(root);
    return pg;
}

// --- sources --------------------------------------------------------------------------------------

static bool is_hls(cJSON *s)
{
    std::string t = jstr(s, "__type"), c = jstr(s, "container"), u = jstr(s, "url");
    return t == "HLSSource" || c.find("mpegurl") != std::string::npos || u.find(".m3u8") != std::string::npos;
}

// Fl_Choice merges entries with the same label (and shifts the indexes): repeated variants go.
static void add_hls(Details &d, const std::string &master)
{
    sp_variant *v = nullptr;
    int n = sp_hls_variants(master.c_str(), &v);
    for (int i = 0; i < n; i++) {
        Source s;
        s.kind = S_HLS;
        s.url = v[i].url;
        s.height = v[i].height;
        s.label = v[i].name;
        if (v[i].audio)
            s.audio = v[i].audio;
        if (s.label == "audio_only" || (!s.height && s.label != "Auto"))
            s.label = "Solo audio";
        bool dup = false;
        for (auto &x : d.sources)
            if (x.label == s.label)
                dup = true;
        if (!dup)
            d.sources.push_back(s);
    }
    sp_free_variants(v, n > 0 ? n : 0);
}

static std::string codec_tag(const std::string &codec)
{
    if (codec.rfind("avc1", 0) == 0)
        return "";
    if (codec.rfind("vp9", 0) == 0 || codec.rfind("vp09", 0) == 0)
        return " VP9";
    if (codec.rfind("av01", 0) == 0)
        return " AV1";
    return " " + codec;
}

// googlevideo says it in the URL (xtags acont=original / acont=dubbed-auto): more reliable than
// the plugin's flag, which comes from the track name.
static bool original_audio(cJSON *s, const std::string &u)
{
    if (u.find("acont%3Doriginal") != std::string::npos || u.find("acont=original") != std::string::npos)
        return true;
    if (u.find("acont%3Ddubbed") != std::string::npos || u.find("acont=dubbed") != std::string::npos)
        return false;
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(s, "original"));
}

static std::string audio_label(cJSON *s, bool orig)
{
    std::string label = jstr(s, "name");
    for (const char *prefix : {"VisionOS ", "iOS "})
        if (label.rfind(prefix, 0) == 0)
            label = label.substr(strlen(prefix));
    size_t o = label.find(" original");
    if (o != std::string::npos)
        label = label.substr(0, o) + " (original)";
    else if (orig)
        label += " (original)";
    return label;
}

// Downloads: the best AAC (M4A) and the best Opus (WebM) of the original language, AAC first.
static void download_audio(Details &d, cJSON *as)
{
    struct Best {
        std::string url;
        long long score = -1, kbps = 0;
    } aac, opus;
    cJSON *s;
    cJSON_ArrayForEach(s, as)
    {
        std::string u = jstr(s, "url"), codec = jstr(s, "codec");
        if (u.find("googlevideo.com") == std::string::npos)
            continue;
        bool is_opus = codec.rfind("opus", 0) == 0;
        if (!is_opus && codec.rfind("mp4a", 0) != 0)
            continue;
        long long br = jnum(s, "bitrate", 0), score = br;
        if (codec.rfind("mp4a.40.5", 0) == 0)
            score /= 4; // HE-AAC: low quality, only if nothing else
        bool dubbed = u.find("acont%3Ddubbed") != std::string::npos || u.find("acont=dubbed") != std::string::npos;
        if (original_audio(s, u))
            score += 2000000000LL;
        else if (!dubbed)
            score += 1000000000LL;
        Best &b = is_opus ? opus : aac;
        if (score > b.score) {
            b.score = score;
            b.url = u;
            b.kbps = br / 1000;
        }
    }
    for (Best *b : {&aac, &opus}) {
        if (b->url.empty())
            continue;
        AudioTrack t;
        t.url = b->url;
        t.len = sp_url_clen(b->url.c_str());
        t.label = std::string(b == &aac ? "AAC" : "Opus") + (b->kbps > 0 ? " " + std::to_string(b->kbps) + " kbps" : "");
        d.dl_audio.push_back(t);
    }
}

static void parse_sources(Details &d, cJSON *root)
{
    cJSON *video = cJSON_GetObjectItemCaseSensitive(root, "video");
    cJSON *vs = cJSON_GetObjectItemCaseSensitive(video, "videoSources");
    cJSON *as = cJSON_GetObjectItemCaseSensitive(video, "audioSources");
    std::string hls;
    cJSON *s;
    cJSON_ArrayForEach(s, vs)
    {
        if (is_hls(s) && hls.empty())
            hls = jstr(s, "url");
    }
    if (hls.empty()) {
        for (const char *k : {"hls", "live"}) {
            cJSON *h = cJSON_GetObjectItemCaseSensitive(root, k);
            if (h && !jstr(h, "url").empty() && is_hls(h)) {
                hls = jstr(h, "url");
                break;
            }
        }
    }
    if (!d.item.live)
        download_audio(d, as);
    if (!hls.empty() && (d.item.live || !cJSON_GetArraySize(as))) {
        add_hls(d, hls);
        if (!d.sources.empty())
            return;
    }
    // With automatic dubbing there is one track per language: the best of each one is kept
    // (AAC first, then bitrate) and the original language is the default.
    std::string audio;
    long long alen = 0, abr = -1;
    struct Cand {
        std::string lang, label, url;
        long long score;
        bool orig;
    };
    std::vector<Cand> langs;
    cJSON_ArrayForEach(s, as)
    {
        std::string codec = jstr(s, "codec"), u = jstr(s, "url");
        if (u.empty())
            continue;
        long long br = jnum(s, "bitrate", 0);
        bool aac = codec.rfind("mp4a.40.2", 0) == 0;
        bool orig = original_audio(s, u);
        long long score = (aac ? 1000000000LL : 0) + br;
        if ((orig ? 2000000000LL : 0) + score > abr) {
            abr = (orig ? 2000000000LL : 0) + score;
            audio = u;
        }
        std::string label = audio_label(s, orig);
        std::string lang = jstr(s, "language");
        if (lang.empty() || lang == "Unknown")
            lang = label;
        bool found = false;
        for (auto &c : langs)
            if (c.lang == lang) {
                found = true;
                if (score > c.score)
                    c = {lang, label, u, score, orig};
            }
        if (!found)
            langs.push_back({lang, label, u, score, orig});
    }
    if (!audio.empty())
        alen = sp_url_clen(audio.c_str());
    if (langs.size() > 1) {
        std::stable_sort(langs.begin(), langs.end(), [](const Cand &a, const Cand &b) {
            if (a.orig != b.orig)
                return a.orig;
            return a.label < b.label;
        });
        for (auto &c : langs) {
            AudioTrack t;
            t.label = c.label;
            t.url = c.url;
            t.len = c.url == audio ? alen : sp_url_clen(c.url.c_str());
            d.audios.push_back(t);
        }
        if (!langs[0].orig) {
            // No track is marked as original: the default one goes first.
            for (size_t i = 0; i < d.audios.size(); i++)
                if (d.audios[i].url == audio)
                    std::swap(d.audios[0], d.audios[i]);
        }
    }
    std::vector<Source> avc, other;
    cJSON_ArrayForEach(s, vs)
    {
        if (is_hls(s))
            continue;
        std::string u = jstr(s, "url");
        if (u.empty())
            continue;
        Source src;
        src.url = u;
        src.height = (int)jnum(s, "height", 0);
        std::string codec = jstr(s, "codec");
        // Qualities are named after the short side: a vertical 1080x1920 video is 1080p, not 1920p.
        int wd = (int)jnum(s, "width", 0), q = wd > 0 && wd < src.height ? wd : src.height;
        src.label = (q ? std::to_string(q) + "p" : jstr(s, "name")) + codec_tag(codec);
        if (is_hackstream_url(u)) {
            src.kind = S_HACKSTREAM;
            } else if (u.find("googlevideo.com") != std::string::npos) {
            src.kind = S_RANGE;
            src.audio = audio;
            src.alen = alen;
        } else {
            src.kind = S_DIRECT;
            if (!d.item.live && cJSON_GetArraySize(as))
                src.audio = audio;
        }
        bool dup = false;
        for (auto &x : avc)
            dup = dup || x.label == src.label;
        for (auto &x : other)
            dup = dup || x.label == src.label;
        if (dup)
            continue;
        (codec_tag(codec).empty() ? avc : other).push_back(src);
    }
    for (auto &o : other) {
        bool have = false;
        for (auto &a : avc)
            have = have || a.height == o.height;
        if (!have || o.label.find("VP9") != std::string::npos)
            avc.push_back(o);
    }
    std::stable_sort(avc.begin(), avc.end(), [](const Source &a, const Source &b) {
        if (a.height != b.height)
            return a.height > b.height;
        return a.label.size() < b.label.size();
    });
    // The size of each googlevideo file is asked only for the qualities kept (one request each).
    for (auto &x : avc)
        if (x.kind == S_RANGE)
            x.vlen = sp_url_clen(x.url.c_str());
    d.sources = avc;
    if (d.sources.empty() && !hls.empty())
        add_hls(d, hls);
    if (d.sources.empty() && !audio.empty()) {
        Source a;
        a.kind = is_hackstream_url(audio) ? S_HACKSTREAM : (audio.find("googlevideo.com") != std::string::npos ? S_RANGE : S_DIRECT);
        a.url = audio;
        a.vlen = alen;
        a.label = "Solo audio";
        d.sources.push_back(a);
    }
}


static void parse_one_raw_source(Details &d, cJSON *s)
{
    if (!s)
        return;
    std::string u = jstr(s, "url");
    if (u.empty())
        return;
    Source src;
    src.kind = S_DIRECT;
    src.url = u;
    src.label = jstr(s, "label");
    if (src.label.empty())
        src.label = "Directo";
    src.audio = jstr(s, "audioUrl");
    src.raw_player = true;
    cJSON *args = cJSON_GetObjectItemCaseSensitive(s, "args");
    cJSON *a;
    cJSON_ArrayForEach(a, args)
    {
        if (cJSON_IsString(a) && a->valuestring)
            src.mplayer_args.push_back(a->valuestring);
    }
    d.sources.push_back(src);
}

static void parse_raw_sources(Details &d, cJSON *root)
{
    cJSON *rs = cJSON_GetObjectItemCaseSensitive(root, "rawSources");
    cJSON *s;
    cJSON_ArrayForEach(s, rs)
        parse_one_raw_source(d, s);

    cJSON *video = cJSON_GetObjectItemCaseSensitive(root, "video");
    cJSON *vs = cJSON_GetObjectItemCaseSensitive(video, "videoSources");
    cJSON_ArrayForEach(s, vs)
    {
        if (jstr(s, "__type") == "RawPlayerSource")
            parse_one_raw_source(d, s);
    }
}

void parse_details(Details &d, cJSON *v)
{
    int plat = d.item.plat;
    std::string url = d.item.url;
    d.item = parse_item(v, plat);
    if (d.item.url.empty())
        d.item.url = url;
    d.item.kind = "video";
    d.description = jstr(v, "description");
    cJSON *rating = cJSON_GetObjectItemCaseSensitive(v, "rating");
    d.likes = jnum(rating, "likes");
    parse_sources(d, v);
    parse_raw_sources(d, v);
    cJSON *sb;
    cJSON_ArrayForEach(sb, cJSON_GetObjectItemCaseSensitive(v, "subtitles"))
    {
        std::string n = jstr(sb, "name");
        d.subtitles.push_back(n.empty() ? jstr(sb, "language") : n);
    }
    parse_items(d.related, cJSON_GetObjectItemCaseSensitive(v, "sansonRelated"), plat);
    if (d.sources.empty())
        d.error = "No se encontraron fuentes reproducibles.";
}

void parse_music_audio(Details &m, cJSON *v)
{
    cJSON *video = cJSON_GetObjectItemCaseSensitive(v, "video");
    cJSON *as = cJSON_GetObjectItemCaseSensitive(video, "audioSources");
    struct Best {
        std::string url;
        long long br = -1, real = 0; // br: score used to choose, real: bitrate shown
    } aac, opus;
    cJSON *s;
    cJSON_ArrayForEach(s, as)
    {
        std::string u = jstr(s, "url"), codec = jstr(s, "codec");
        if (u.empty() || u.find("acont%3Ddubbed") != std::string::npos || u.find("acont=dubbed") != std::string::npos)
            continue;
        long long real = jnum(s, "bitrate", 0), br = real;
        Best &b = codec.rfind("opus", 0) == 0 ? opus : aac;
        if (codec.rfind("mp4a.40.5", 0) == 0)
            br /= 4; // HE-AAC: low quality, only if nothing else
        if (br > b.br) {
            b.br = br;
            b.real = real;
            b.url = u;
        }
    }
    for (Best *b : {&aac, &opus}) {
        if (b->url.empty())
            continue;
        Source src;
        src.url = b->url;
        src.kind = b->url.find("googlevideo.com") != std::string::npos ? S_RANGE : S_DIRECT;
        src.vlen = src.kind == S_RANGE ? sp_url_clen(b->url.c_str()) : 0;
        long long kbps = b->real / 1000;
        src.label = std::string(b == &aac ? "AAC" : "Opus") + (kbps > 0 ? " " + std::to_string(kbps) + " kbps" : "");
        m.sources.push_back(src);
    }
}

// --- chat and comments ------------------------------------------------------------------------------

ChatUpdate parse_chat(const char *json)
{
    ChatUpdate u;
    cJSON *root = cJSON_Parse(json ? json : "");
    u.error = error_of(root);
    if (u.error.empty()) {
        u.pager = (int)jnum(root, "__pager", 0);
        u.more = jbool(root, "hasMore");
        long long next = jnum(root, "nextRequest", 2000);
        u.interval_ms = (int)std::max(1000LL, std::min(2500LL, next));
        cJSON *ev;
        cJSON_ArrayForEach(ev, cJSON_GetObjectItemCaseSensitive(root, "results"))
        {
            int type = (int)jnum(ev, "type", 0);
            ChatMsg m;
            if (type == 4) {
                cJSON *e;
                cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(ev, "emojis"))
                {
                    if (cJSON_IsString(e) && e->string)
                        u.emojis.push_back({e->string, e->valuestring});
                }
                continue;
            } else if (type == 10) {
                u.viewers = jnum(ev, "viewCount", -1);
                continue;
            } else if (type == 1) {
                m.kind = CHAT_MESSAGE;
                m.name = jstr(ev, "name");
                m.text = jstr(ev, "message");
                m.color = jstr(ev, "colorName");
                m.time = jnum(ev, "time", -1);
            } else if (type == 5) {
                m.kind = CHAT_DONATION;
                m.name = jstr(ev, "name");
                std::string amount = jstr(ev, "amount"), msg = jstr(ev, "message");
                m.text = amount.empty() ? msg : msg.empty() ? amount : amount + " · " + msg;
                m.color = jstr(ev, "colorDonation");
            } else if (type == 100) {
                m.kind = CHAT_RAID;
                m.name = jstr(ev, "targetName");
                m.text = jstr(ev, "targetUrl");
            } else {
                continue;
            }
            u.msgs.push_back(m);
        }
    }
    cJSON_Delete(root);
    return u;
}

CommentPage parse_comments(const char *json)
{
    CommentPage cp;
    cJSON *root = cJSON_Parse(json ? json : "");
    cp.error = error_of(root);
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "value");
    if (cp.error.empty()) {
        cJSON *c;
        cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(v, "comments"))
        {
            Comment cm;
            cm.author = jstr(c, "author");
            cm.text = jstr(c, "text");
            cm.when = jstr(c, "when");
            cm.likes = jstr(c, "likes");
            cm.replies = jstr(c, "replies");
            cp.items.push_back(cm);
        }
        cp.next = jstr(v, "next");
        cp.create = jstr(v, "create");
        cp.count = jstr(v, "count");
        cp.disabled = jbool(v, "disabled");
    }
    cJSON_Delete(root);
    return cp;
}

// WebVTT (what YouTube gives) to SubRip, which every mplayer build reads.
std::string vtt_to_srt(const std::string &vtt)
{
    std::string out, line, text;
    std::string times;
    int n = 0;
    auto stamp = [](std::string s) {
        // "MM:SS.mmm" or "HH:MM:SS.mmm" -> "HH:MM:SS,mmm"
        if (std::count(s.begin(), s.end(), ':') == 1)
            s = "00:" + s;
        for (auto &c : s)
            if (c == '.')
                c = ',';
        return s;
    };
    auto flush = [&] {
        if (!times.empty() && !text.empty())
            out += std::to_string(++n) + "\n" + times + "\n" + text + "\n\n";
        times.clear();
        text.clear();
    };
    size_t p = 0;
    while (p <= vtt.size()) {
        size_t e = vtt.find('\n', p);
        if (e == std::string::npos)
            e = vtt.size();
        line = vtt.substr(p, e - p);
        p = e + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        size_t arrow = line.find("-->");
        if (arrow != std::string::npos) {
            flush();
            std::string a = line.substr(0, arrow), b = line.substr(arrow + 3);
            a.erase(0, a.find_first_not_of(" \t"));
            a.erase(a.find_last_not_of(" \t") + 1);
            b.erase(0, b.find_first_not_of(" \t"));
            b = b.substr(0, b.find_first_of(" \t"));
            times = stamp(a) + " --> " + stamp(b);
        } else if (line.empty()) {
            flush();
        } else if (!times.empty()) {
            std::string clean;
            bool tag = false;
            for (char c : line) {
                if (c == '<')
                    tag = true;
                else if (c == '>' && tag)
                    tag = false;
                else if (!tag)
                    clean += c;
            }
            for (auto &r : std::vector<std::pair<std::string, std::string>>{
                     {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&nbsp;", " "}})
                for (size_t q = 0; (q = clean.find(r.first, q)) != std::string::npos; q += r.second.size())
                    clean.replace(q, r.first.size(), r.second);
            if (!clean.empty())
                text += (text.empty() ? "" : "\n") + clean;
        }
        if (e == vtt.size())
            break;
    }
    flush();
    return out;
}

// --- items on disk ---------------------------------------------------------------------------------

cJSON *item_to_json(const Item &it)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "plat", it.plat);
    cJSON_AddStringToObject(o, "kind", it.kind.c_str());
    cJSON_AddStringToObject(o, "id", it.id.c_str());
    cJSON_AddStringToObject(o, "title", it.title.c_str());
    cJSON_AddStringToObject(o, "author", it.author.c_str());
    cJSON_AddStringToObject(o, "author_url", it.author_url.c_str());
    cJSON_AddStringToObject(o, "thumb", it.thumb.c_str());
    cJSON_AddStringToObject(o, "url", it.url.c_str());
    cJSON_AddNumberToObject(o, "duration", it.duration);
    cJSON_AddNumberToObject(o, "date", (double)it.date);
    cJSON_AddBoolToObject(o, "live", it.live);
    if (it.subs >= 0)
        cJSON_AddNumberToObject(o, "subs", (double)it.subs);
    if (!it.album.empty()) {
        cJSON_AddStringToObject(o, "album", it.album.c_str());
        cJSON_AddStringToObject(o, "album_url", it.album_url.c_str());
    }
    return o;
}

Item item_from_json(cJSON *o)
{
    Item it;
    it.plat = (int)jnum(o, "plat", 0);
    it.kind = jstr(o, "kind");
    it.id = jstr(o, "id");
    it.title = jstr(o, "title");
    it.author = jstr(o, "author");
    it.author_url = jstr(o, "author_url");
    it.thumb = jstr(o, "thumb");
    it.url = jstr(o, "url");
    it.duration = (int)jnum(o, "duration", 0);
    it.date = jnum(o, "date", 0);
    it.subs = jnum(o, "subs", -1);
    it.live = jbool(o, "live");
    it.album = jstr(o, "album");
    it.album_url = jstr(o, "album_url");
    return it;
}
