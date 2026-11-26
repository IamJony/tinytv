// One worker thread per platform runs its plugin (plugins/<platform>), started the first time
// it is needed.

#include "backend.h"

#include <FL/Fl.H>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <deque>
#include <mutex>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <curl/curl.h>

#include "cJSON.h"
#include "engine.h"
#include "parse.h"
#include "stream.h"


int P_LatinoTV = 0;
int P_COUNT = 1;
int P_YOUTUBE = -1, P_TWITCH = -1, P_KICK = -1, P_MUSIC = -1;
static std::vector<std::string> g_plat_ids, g_plat_names;
static std::vector<bool> g_plat_comments_set, g_plat_comments_val;
static std::vector<bool> g_plat_chat_set, g_plat_chat_val;

bool backend_comments_enabled(int plat, bool &out) {
    if (plat < 0 || plat >= (int)g_plat_comments_set.size() || !g_plat_comments_set[plat]) return false;
    out = g_plat_comments_val[plat];
    return true;
}
bool backend_chat_enabled(int plat, bool &out) {
    if (plat < 0 || plat >= (int)g_plat_chat_set.size() || !g_plat_chat_set[plat]) return false;
    out = g_plat_chat_val[plat];
    return true;
}

std::string backend_plat_id(int p) {
    if (p < 0 || p >= (int)g_plat_ids.size()) return "";
    return g_plat_ids[p];
}
int backend_plat_count() { return (int)g_plat_ids.size(); }
const char *backend_plat_name_dyn(int p) {
    if (p < 0 || p >= (int)g_plat_names.size()) return "";
    return g_plat_names[p].c_str();
}
void backend_scan_plugins()
{
    g_plat_ids.clear(); g_plat_names.clear();
    g_plat_comments_set.clear(); g_plat_comments_val.clear();
    g_plat_chat_set.clear(); g_plat_chat_val.clear();
    g_plat_ids.push_back("latinotv");
    g_plat_names.push_back("LatinoTV");
    g_plat_comments_set.push_back(false); g_plat_comments_val.push_back(false);
    g_plat_chat_set.push_back(false); g_plat_chat_val.push_back(false);
    auto scan = [&](const std::string &root) {
        DIR *d = opendir(root.c_str());
        if (!d) return;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            std::string cfg = root + "/" + e->d_name + "/config.json";
            if (access(cfg.c_str(), R_OK) != 0) continue;
            bool dup = false;
            for (auto &x : g_plat_ids) if (x == e->d_name) dup = true;
            if (dup) continue;
            cJSON *r = cJSON_Parse(read_file(cfg).c_str());
            std::string name = jstr(r, "name");
            if (name.empty()) name = e->d_name;
            cJSON *jc = cJSON_GetObjectItemCaseSensitive(r, "comments");
            bool c_set = jc && cJSON_IsBool(jc), c_val = c_set && cJSON_IsTrue(jc);
            cJSON *jch = cJSON_GetObjectItemCaseSensitive(r, "chat");
            bool ch_set = jch && cJSON_IsBool(jch), ch_val = ch_set && cJSON_IsTrue(jch);
            cJSON_Delete(r);
            g_plat_ids.push_back(e->d_name);
            g_plat_names.push_back(name);
            g_plat_comments_set.push_back(c_set);
            g_plat_comments_val.push_back(c_val);
            g_plat_chat_set.push_back(ch_set);
            g_plat_chat_val.push_back(ch_val);
        }
        closedir(d);
    };
    const char *env = getenv("SANSON_SHARE");
    scan(std::string(env ? env : "/usr/local/share/tinytv") + "/plugins");
    scan(config_dir() + "/plugins");
    P_COUNT = (int)g_plat_ids.size();
    P_LatinoTV = 0;
    P_YOUTUBE = P_TWITCH = P_KICK = P_MUSIC = -1;
    for (int i = 0; i < (int)g_plat_ids.size(); i++) {
        const std::string &pid = g_plat_ids[i];
        std::string pname = plat_name(i);
        if (pid == "youtube" || pid == "0b8e7c52-3f1d-4a8e-9d3c-7973616e736f" || pname == "YouTube") {
            if (P_YOUTUBE < 0) P_YOUTUBE = i;
        } else if (pid == "music" || pid == "6f0d2c1e-5a1b-4c7e-9a43-73616e736f6e" ||
                   pname == "Música" || pname == "Music") {
            if (P_MUSIC < 0) P_MUSIC = i;
        } else if (pid == "twitch" || pname == "Twitch") {
            if (P_TWITCH < 0) P_TWITCH = i;
        } else if (pid == "kick" || pname == "Kick") {
            if (P_KICK < 0) P_KICK = i;
        }
    }
    for (int i = 0; i < P_COUNT; i++) {
        if (g_plat_ids[i] == "youtube") P_YOUTUBE = i;
        else if (g_plat_ids[i] == "twitch") P_TWITCH = i;
        else if (g_plat_ids[i] == "kick") P_KICK = i;
        else if (g_plat_ids[i] == "music") P_MUSIC = i;
    }
}
const char *plat_name(int plat)
{
    if (plat < 0 || plat >= (int)g_plat_names.size()) return "";
    return g_plat_names[plat].c_str();
}

static void ui_call(std::function<void()> f)
{
    auto *p = new std::function<void()>(std::move(f));
    Fl::awake([](void *d) {
        auto *q = static_cast<std::function<void()> *>(d);
        (*q)();
        delete q;
    }, p);
}

static void low_priority(int nice)
{
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), nice);
}

static std::string json_args(std::initializer_list<std::string> args)
{
    cJSON *a = cJSON_CreateArray();
    for (auto &s : args)
        cJSON_AddItemToArray(a, cJSON_CreateString(s.c_str()));
    char *s = cJSON_PrintUnformatted(a);
    std::string r = s;
    free(s);
    cJSON_Delete(a);
    return r;
}

// [[url, url...]]: one argument, a list of urls.
static std::string json_urls(const std::vector<std::string> &urls)
{
    cJSON *a = cJSON_CreateArray(), *l = cJSON_CreateArray();
    for (auto &u : urls)
        cJSON_AddItemToArray(l, cJSON_CreateString(u.c_str()));
    cJSON_AddItemToArray(a, l);
    char *s = cJSON_PrintUnformatted(a);
    std::string r = s;
    free(s);
    cJSON_Delete(a);
    return r;
}

static std::string error_json(const std::string &type, const std::string &msg)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "__error", 1);
    cJSON_AddStringToObject(o, "type", type.c_str());
    cJSON_AddStringToObject(o, "message", msg.c_str());
    char *s = cJSON_PrintUnformatted(o);
    std::string r = s;
    free(s);
    cJSON_Delete(o);
    return r;
}

// --- workers ---------------------------------------------------------------------------------------

struct Worker {
    int plat = 0;
    sj_plugin *p = nullptr;
    std::string open_error;
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::function<void(Worker &)>> q;
    std::thread t;
};

static Worker workers[P_COUNT_MAX];
static std::string g_plugins, g_host;
static std::mutex auth_lock;

static void log_cb(const char *msg, void *u)
{
    if (getenv("SANSON_DEBUG"))
        fprintf(stderr, "[%s] %.300s\n", backend_plat_name_dyn((long)u), msg);
}

static void say(int plat, const std::string &msg)
{
    log_cb(msg.c_str(), (void *)(long)plat);
}

static std::string auth_all()
{
    return read_file(config_dir() + "/auth.json");
}

std::string backend_get_auth(int plat)
{
    std::lock_guard<std::mutex> g(auth_lock);
    cJSON *root = cJSON_Parse(auth_all().c_str());
    cJSON *a = cJSON_GetObjectItemCaseSensitive(root, backend_plat_id(plat).c_str());
    std::string r;
    if (a) {
        char *s = cJSON_PrintUnformatted(a);
        r = s;
        free(s);
    }
    cJSON_Delete(root);
    return r;
}

// Verification passed in the browser ({"cookies", "ua"} per platform, Música uses YouTube's).
static void clearance_of(int plat, std::string &cookies, std::string &ua)
{
    std::lock_guard<std::mutex> g(auth_lock);
    cJSON *root = cJSON_Parse(read_file(config_dir() + "/verify.json").c_str());
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, backend_plat_id(plat == P_MUSIC ? P_YOUTUBE : plat).c_str());
    cookies = jstr(v, "cookies");
    ua = jstr(v, "ua");
    cJSON_Delete(root);
}

bool backend_logged(int plat)
{
    return !backend_get_auth(plat).empty();
}

static std::string state_path(int plat)
{
    return config_dir() + "/state-" + backend_plat_id(plat).c_str() + ".json";
}

static void save_state(Worker &w)
{
    if (!w.p || w.plat != P_YOUTUBE)
        return;
    char *st = sj_eval(w.p, "typeof source.saveState === 'function' ? source.saveState() : null");
    cJSON *sv = cJSON_Parse(st ? st : "");
    if (cJSON_IsString(sv) && sv->valuestring)
        write_file(state_path(w.plat), sv->valuestring);
    cJSON_Delete(sv);
    free(st);
}

// Message for the screen when a plugin does not start. It is almost always the network (e.g.
// Sansón opened right after turning the computer on, with the Wi-Fi still connecting).
static std::string open_error_text(int plat, const char *err)
{
    cJSON *root = cJSON_Parse(err ? err : "");
    std::string type = jstr(root, "type"), msg = jstr(root, "message");
    cJSON_Delete(root);
    if (type.find("Captcha") != std::string::npos || msg.find("Captcha") != std::string::npos)
        return CAPTCHA_ERROR;
    return std::string("No se pudo conectar con ") + backend_plat_name_dyn(plat) +
           ".\nRevisa la conexión a internet y pulsa Recargar (F5).";
}

// A failure to start is not kept: it is tried again with the next request.
static bool ensure_open(Worker &w)
{
    if (w.p)
        return true;
    // Buscar primero en la carpeta del usuario, luego en la del sistema
    std::string dir = config_dir() + "/plugins/" + backend_plat_id(w.plat).c_str();
    if (access((dir + "/config.json").c_str(), R_OK) != 0)
        dir = g_plugins + "/" + backend_plat_id(w.plat).c_str();
    std::string auth = backend_get_auth(w.plat);
    std::string state = read_file(state_path(w.plat));
    char *err = nullptr;
    w.p = sj_open(dir.c_str(), g_host.c_str(), "{}", auth.empty() ? nullptr : auth.c_str(),
                  state.empty() ? nullptr : state.c_str(), log_cb, (void *)(long)w.plat, &err);
    if (w.p) {
        std::string cookies, ua;
        clearance_of(w.plat, cookies, ua);
        sj_set_clearance(w.p, cookies.c_str(), ua.c_str());
    } else {
        log_cb(err ? err : "enable failed", (void *)(long)w.plat);
    }
    w.open_error = w.p ? "" : open_error_text(w.plat, err);
    free(err);
    return w.p != nullptr;
}

static void worker_loop(Worker *w)
{
    low_priority(10);
    for (;;) {
        std::function<void(Worker &)> job;
        {
            std::unique_lock<std::mutex> lk(w->m);
            w->cv.wait(lk, [w] { return !w->q.empty(); });
            job = std::move(w->q.front());
            w->q.pop_front();
        }
        job(*w);
    }
}

static void post(int plat, std::function<void(Worker &)> job)
{
    if (!job)
        return;
    int limit = backend_plat_count();
    if (plat < 0 || plat >= limit) {
        fprintf(stderr, "[post] plataforma fuera de rango o no disponible: plat=%d (count=%d)\n", plat, limit);
        return;
    }
    Worker &w = workers[plat];
    {
        std::lock_guard<std::mutex> g(w.m);
        w.q.push_back(std::move(job));
    }
    w.cv.notify_one();
}

void backend_start(const std::string &plugins_dir, const std::string &host_js)
{
    g_plugins = plugins_dir;
    backend_scan_plugins();
    g_host = host_js;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    sp_start();
    for (int i = 0; i < P_COUNT; i++) {
        workers[i].plat = i;
        workers[i].t = std::thread(worker_loop, &workers[i]);
        workers[i].t.detach();
    }
}

// --- calls ---------------------------------------------------------------------------------------------

static std::string call(Worker &w, const char *method, const std::string &args)
{
    if (!ensure_open(w))
        return error_json("Open", w.open_error);
    char *r = sj_call(w.p, method, args.c_str());
    std::string s = r ? r : "";
    free(r);
    if (strstr(s.c_str(), "\"__error\""))
        say(w.plat, std::string(method) + ": " + s.substr(0, 300));
    return s;
}

static Page call_page(Worker &w, const char *method, const std::string &args)
{
    return parse_page(call(w, method, args).c_str(), w.plat);
}

static void drop_now(Worker &w, int pager)
{
    if (pager > 0 && w.p)
        free(sj_drop(w.p, pager));
}

static void page_call(int plat, const std::string &method, const std::string &args, std::function<void(Page)> cb)
{
    post(plat, [=](Worker &w) {
        Page pg = call_page(w, method.c_str(), args);
        save_state(w);
        ui_call([cb, pg] { cb(pg); });
    });
}

// --- lists -------------------------------------------------------------------------------------------

static std::vector<std::string> urls_of(const std::vector<Item> &items, int plat, const char *kind, size_t max)
{
    std::vector<std::string> v;
    for (auto &it : items)
        if (it.plat == plat && (!kind || it.kind == kind) && v.size() < max)
            v.push_back(it.url);
    return v;
}

void backend_home(int plat, std::function<void(Page)> cb)
{
    // YouTube without an account builds its home from what was watched last.
    page_call(plat, "getHome", json_urls(urls_of(history_list(), plat, "video", 5)), cb);
}

void backend_search(int plat, const std::string &query, std::function<void(Page)> cb)
{
    // Channels first. When search() brings none, the ones of searchChannels go in front.
    std::string args = json_args({query});
    post(plat, [=](Worker &w) {
        Page pg = call_page(w, "search", args);
        bool has_channel = std::any_of(pg.items.begin(), pg.items.end(), [](const Item &i) { return i.kind == "channel"; });
        if (!has_channel) {
            Page ch = call_page(w, "searchChannels", args);
            drop_now(w, ch.pager);
            size_t keep = plat == P_KICK ? 20 : 4;
            std::vector<Item> items;
            for (auto &c : ch.items)
                if (c.kind == "channel" && items.size() < keep)
                    items.push_back(c);
            items.insert(items.end(), pg.items.begin(), pg.items.end());
            pg.items = items;
            if (!pg.error.empty() && !items.empty())
                pg.error.clear();
        } else {
            // The channels together at the top, whatever order the plugin used.
            std::stable_partition(pg.items.begin(), pg.items.end(), [](const Item &i) { return i.kind == "channel"; });
        }
        ui_call([cb, pg] { cb(pg); });
    });
}

void backend_channel_videos(int plat, const std::string &url, std::function<void(Page)> cb)
{
    page_call(plat, "getChannelContents", json_args({url}), cb);
}

void backend_related(int plat, const std::string &url, std::function<void(Page)> cb)
{
    page_call(plat, "getContentRecommendations", json_args({url}), cb);
}

void backend_feed(int plat, std::function<void(Page)> cb)
{
    page_call(plat, "sansonFeed", json_urls(urls_of(subs_list(), plat, nullptr, 200)), cb);
}

void backend_following(int plat, std::function<void(std::vector<Item>, std::string)> cb)
{
    post(plat, [=](Worker &w) {
        std::vector<Item> items;
        std::string err;
        if (backend_logged(plat)) {
            Page pg = call_page(w, "sansonFollowing", "[]");
            drop_now(w, pg.pager);
            items = pg.items;
            err = pg.error;
        }
        ui_call([cb, items, err] { cb(items, err); });
    });
}

void backend_next(int plat, int pager, std::function<void(Page)> cb)
{
    post(plat, [=](Worker &w) {
        Page pg;
        pg.plat = plat;
        if (!w.p) {
            pg.error = "La lista ya no está disponible: recarga la vista.";
        } else {
            char *r = sj_next(w.p, pager, nullptr);
            pg = parse_page(r, plat);
            free(r);
        }
        ui_call([cb, pg] { cb(pg); });
    });
}

void backend_drop(int plat, int pager)
{
    if (pager <= 0)
        return;
    post(plat, [=](Worker &w) { drop_now(w, pager); });
}

// --- chat and comments ---------------------------------------------------------------------------------

static void chat_call(int plat, const char *method, const std::string &args, std::function<void(ChatUpdate)> cb)
{
    std::string m = method;
    post(plat, [=](Worker &w) {
        ChatUpdate u = parse_chat(call(w, m.c_str(), args).c_str());
        ui_call([cb, u] { cb(u); });
    });
}

static void chat_next(int plat, int pager, const char *arg, std::function<void(ChatUpdate)> cb)
{
    std::string a = arg ? arg : "";
    bool has = arg != nullptr;
    post(plat, [=](Worker &w) {
        ChatUpdate u;
        if (!w.p) {
            u.error = "El chat se cerró.";
        } else {
            char *r = sj_next(w.p, pager, has ? a.c_str() : nullptr);
            u = parse_chat(r);
            free(r);
        }
        ui_call([cb, u] { cb(u); });
    });
}

void backend_chat_open(int plat, const std::string &url, std::function<void(ChatUpdate)> cb)
{
    chat_call(plat, "getLiveEvents", json_args({url}), cb);
}

void backend_chat_poll(int plat, int pager, std::function<void(ChatUpdate)> cb)
{
    chat_next(plat, pager, nullptr, cb);
}

void backend_vod_chat_open(int plat, const std::string &url, std::function<void(ChatUpdate)> cb)
{
    chat_call(plat, "getVODEvents", json_args({url}), cb);
}

void backend_vod_chat_poll(int plat, int pager, long long ms, std::function<void(ChatUpdate)> cb)
{
    chat_next(plat, pager, std::to_string(ms).c_str(), cb);
}

void backend_comments(int plat, const std::string &url, const std::string &token, std::function<void(CommentPage)> cb)
{
    post(plat, [=](Worker &w) {
        CommentPage cp = parse_comments(call(w, "sansonComments", json_args({url, token})).c_str());
        ui_call([cb, cp] { cb(cp); });
    });
}

void backend_post_comment(int plat, const std::string &create, const std::string &text,
                          std::function<void(std::string)> cb)
{
    post(plat, [=](Worker &w) {
        std::string r = call(w, "sansonPostComment", json_args({create, text}));
        cJSON *root = cJSON_Parse(r.c_str());
        std::string err = error_of(root);
        cJSON_Delete(root);
        ui_call([cb, err] { cb(err); });
    });
}

// --- details -------------------------------------------------------------------------------------------

static void music_details(const std::string &url, std::function<void(Details)> cb);

void backend_details(int plat, const std::string &url, std::function<void(Details)> cb)
{
    if (plat == P_MUSIC) {
        music_details(url, cb);
        return;
    }
    post(plat, [=](Worker &w) {
        Details d;
        d.item.plat = plat;
        d.item.url = url;
        std::string r = call(w, "getContentDetails", json_args({url}));
        cJSON *root = cJSON_Parse(r.c_str());
        d.error = error_of(root);
        cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "value");
        if (d.error.empty() && v)
            parse_details(d, v);
        cJSON_Delete(root);
        save_state(w);
        ui_call([cb, d] { cb(d); });
    });
}

void backend_subtitle(int plat, int index, std::function<void(std::string srt, std::string error)> cb)
{
    post(plat, [=](Worker &w) {
        std::string srt;
        std::string r = call(w, "sansonSubtitles", "[" + std::to_string(index) + "]");
        cJSON *root = cJSON_Parse(r.c_str());
        std::string err = error_of(root);
        cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "value");
        if (err.empty() && cJSON_IsString(v) && v->valuestring)
            srt = vtt_to_srt(v->valuestring);
        if (err.empty() && srt.empty())
            err = "El subtítulo está vacío.";
        cJSON_Delete(root);
        ui_call([cb, srt, err] { cb(srt, err); });
    });
}

void backend_channel(int plat, const std::string &url, std::function<void(Channel)> cb)
{
    post(plat, [=](Worker &w) {
        Channel c;
        c.plat = plat;
        c.url = url;
        std::string r = call(w, "getChannel", json_args({url}));
        cJSON *root = cJSON_Parse(r.c_str());
        c.error = error_of(root);
        cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "value");
        if (c.error.empty() && v) {
            c.name = jstr(v, "name");
            c.thumb = jstr(v, "thumbnail");
            c.banner = jstr(v, "banner");
            c.description = jstr(v, "description");
            c.subs = jnum(v, "subscribers");
            if (!jstr(v, "url").empty())
                c.url = jstr(v, "url");
        }
        cJSON_Delete(root);
        ui_call([cb, c] { cb(c); });
    });
}

// --- session -------------------------------------------------------------------------------------------

void backend_set_auth(int plat, const std::string &auth_json)
{
    {
        std::lock_guard<std::mutex> g(auth_lock);
        cJSON *root = cJSON_Parse(auth_all().c_str());
        if (!cJSON_IsObject(root)) {
            cJSON_Delete(root);
            root = cJSON_CreateObject();
        }
        cJSON_DeleteItemFromObjectCaseSensitive(root, backend_plat_id(plat).c_str());
        cJSON *a = auth_json.empty() ? nullptr : cJSON_Parse(auth_json.c_str());
        if (a)
            cJSON_AddItemToObject(root, backend_plat_id(plat).c_str(), a);
        char *s = cJSON_Print(root);
        write_file(config_dir() + "/auth.json", s);
        free(s);
        cJSON_Delete(root);
    }
    remove(state_path(plat).c_str());
    backend_reload(plat);
}

void backend_set_clearance(int plat, const std::string &cookies, const std::string &ua)
{
    {
        std::lock_guard<std::mutex> g(auth_lock);
        std::string path = config_dir() + "/verify.json";
        cJSON *root = cJSON_Parse(read_file(path).c_str());
        if (!cJSON_IsObject(root)) {
            cJSON_Delete(root);
            root = cJSON_CreateObject();
        }
        cJSON_DeleteItemFromObjectCaseSensitive(root, backend_plat_id(plat).c_str());
        cJSON *v = cJSON_CreateObject();
        cJSON_AddStringToObject(v, "cookies", cookies.c_str());
        if (!ua.empty())
            cJSON_AddStringToObject(v, "ua", ua.c_str());
        cJSON_AddItemToObject(root, backend_plat_id(plat).c_str(), v);
        char *s = cJSON_Print(root);
        write_file(path, s);
        free(s);
        cJSON_Delete(root);
    }
    backend_reload(plat);
    if (plat == P_YOUTUBE)
        backend_reload(P_MUSIC);
}

void backend_reload(int plat)
{
    post(plat, [](Worker &w) {
        if (w.p)
            sj_close(w.p);
        w.p = nullptr;
    });
}

// --- thumbnails ----------------------------------------------------------------------------------------

struct ThumbJob {
    std::string url;
    std::function<std::function<void()>(std::string)> work;
};

static std::mutex thumb_m;
static std::condition_variable thumb_cv;
static std::deque<ThumbJob> thumb_q;
static bool thumb_started;

static size_t to_string(char *p, size_t s, size_t n, void *ud)
{
    static_cast<std::string *>(ud)->append(p, s * n);
    return s * n;
}

// Newest request first: the cards on screen now matter more than the ones scrolled past.
static void thumb_loop()
{
    low_priority(10);
    CURL *c = curl_easy_init();
    for (;;) {
        ThumbJob j;
        {
            std::unique_lock<std::mutex> lk(thumb_m);
            thumb_cv.wait(lk, [] { return !thumb_q.empty(); });
            j = std::move(thumb_q.back());
            thumb_q.pop_back();
        }
        std::string body;
        curl_easy_reset(c);
        curl_easy_setopt(c, CURLOPT_URL, j.url.c_str());
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_USERAGENT,
                         "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0");
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, nullptr);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_string);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
        long code = 0;
        if (curl_easy_perform(c) == CURLE_OK)
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        if (code != 200)
            body.clear();
        ui_call(j.work(body));
    }
}

void thumbs_fetch(const std::string &url, std::function<void(std::string)> cb)
{
    thumbs_fetch_work(url, [cb](std::string body) -> std::function<void()> { return [cb, body] { cb(body); }; });
}

void thumbs_fetch_work(const std::string &url, std::function<std::function<void()>(std::string)> work)
{
    std::lock_guard<std::mutex> g(thumb_m);
    if (!thumb_started) {
        thumb_started = true;
        for (int i = 0; i < 2; i++)
            std::thread(thumb_loop).detach();
    }
    thumb_q.push_back({url, work});
    while (thumb_q.size() > 120) {
        auto old = thumb_q.front();
        thumb_q.pop_front();
        ui_call(old.work(""));
    }
    thumb_cv.notify_one();
}

// --- Música --------------------------------------------------------------------------------------------

static void music_details(const std::string &url, std::function<void(Details)> cb)
{
    // 1) metadata from YouTube Music, 2) audio from YouTube (its own thread), 3) if YouTube gave
    // none, the same song on SoundCloud.
    post(P_MUSIC, [=](Worker &w) {
        Details d;
        d.item.plat = P_MUSIC;
        d.item.url = url;
        std::string r = call(w, "getContentDetails", json_args({url}));
        cJSON *root = cJSON_Parse(r.c_str());
        d.error = error_of(root);
        cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "value");
        if (d.error.empty() && v) {
            d.item = parse_item(v, P_MUSIC);
            d.lyrics_id = jstr(v, "sansonLyricsId");
        }
        cJSON_Delete(root);
        if (d.item.url.empty())
            d.item.url = url;
        d.item.kind = "video";
        if (!d.error.empty() || d.item.id.empty()) {
            if (d.error.empty())
                d.error = "Canción no encontrada.";
            ui_call([cb, d] { cb(d); });
            return;
        }
        post(P_YOUTUBE, [=](Worker &yw) {
            Details m = d;
            std::string r2 = call(yw, "getContentDetails", json_args({"https://www.youtube.com/watch?v=" + d.item.id}));
            cJSON *root2 = cJSON_Parse(r2.c_str());
            std::string err = error_of(root2);
            cJSON *v2 = cJSON_GetObjectItemCaseSensitive(root2, "value");
            if (err.empty() && v2)
                parse_music_audio(m, v2);
            cJSON_Delete(root2);
            save_state(yw);
            if (!m.sources.empty()) {
                ui_call([cb, m] { cb(m); });
                return;
            }
            post(P_MUSIC, [=](Worker &mw) {
                Details f = m;
                cJSON *a = cJSON_CreateArray();
                cJSON_AddItemToArray(a, cJSON_CreateString(f.item.title.c_str()));
                cJSON_AddItemToArray(a, cJSON_CreateString(f.item.author.c_str()));
                cJSON_AddItemToArray(a, cJSON_CreateNumber(f.item.duration));
                char *args = cJSON_PrintUnformatted(a);
                cJSON_Delete(a);
                std::string r3 = call(mw, "sansonFallback", args);
                free(args);
                cJSON *root3 = cJSON_Parse(r3.c_str());
                cJSON *v3 = cJSON_GetObjectItemCaseSensitive(root3, "value");
                std::string fu = jstr(v3, "url");
                if (!fu.empty()) {
                    Source s;
                    s.kind = S_HLS;
                    s.url = fu;
                    s.label = "SoundCloud";
                    f.sources.push_back(s);
                    f.fallback = "SoundCloud (" + jstr(v3, "user") + ")";
                }
                cJSON_Delete(root3);
                if (f.sources.empty())
                    f.error = "No se pudo obtener el audio de esta canción" + (err.empty() ? std::string(".") : ":\n" + err);
                ui_call([cb, f] { cb(f); });
            });
        });
    });
}

void backend_lyrics(const Details &d, std::function<void(Lyrics)> cb)
{
    cJSON *a = cJSON_CreateArray();
    cJSON_AddItemToArray(a, cJSON_CreateString(d.item.title.c_str()));
    cJSON_AddItemToArray(a, cJSON_CreateString(d.item.author.c_str()));
    cJSON_AddItemToArray(a, cJSON_CreateString(d.item.album.c_str()));
    cJSON_AddItemToArray(a, cJSON_CreateNumber(d.item.duration));
    cJSON_AddItemToArray(a, cJSON_CreateString(d.lyrics_id.c_str()));
    char *s = cJSON_PrintUnformatted(a);
    std::string args = s;
    free(s);
    cJSON_Delete(a);
    post(P_MUSIC, [=](Worker &w) {
        Lyrics l;
        std::string r = call(w, "sansonLyrics", args);
        cJSON *root = cJSON_Parse(r.c_str());
        l.error = error_of(root);
        cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "value");
        if (l.error.empty() && v) {
            cJSON *line;
            cJSON_ArrayForEach(line, cJSON_GetObjectItemCaseSensitive(v, "synced"))
            {
                cJSON *t = cJSON_GetArrayItem(line, 0), *x = cJSON_GetArrayItem(line, 1);
                if (cJSON_IsNumber(t) && cJSON_IsString(x))
                    l.lines.push_back({(long long)t->valuedouble, x->valuestring});
            }
            l.plain = jstr(v, "plain");
            l.source = jstr(v, "source");
        }
        cJSON_Delete(root);
        ui_call([cb, l] { cb(l); });
    });
}

void backend_radio(const std::string &url, std::function<void(Page)> cb)
{
    page_call(P_MUSIC, "sansonRadio", json_args({url}), cb);
}
