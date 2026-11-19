#include "chatsend.h"

#include <FL/Fl.H>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <curl/curl.h>

#include "backend.h"
#include "cJSON.h"
#include "engine.h"
#include "ws.h"

#define UA "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0"

struct SendJob {
    int plat = 0;
    std::string url, text;
    std::function<void(std::string)> done;
    bool reset = false;
};

struct Auth {
    std::string cookies;
    std::map<std::string, std::string> headers;
};

static std::mutex q_lock;
static std::condition_variable q_cv;
static std::deque<SendJob> q;
static bool started;

static void ui_done(std::function<void(std::string)> done, std::string err)
{
    auto *p = new std::function<void()>([done, err] { done(err); });
    Fl::awake([](void *d) {
        auto *f = static_cast<std::function<void()> *>(d);
        (*f)();
        delete f;
    }, p);
}

static Auth load_auth(int plat)
{
    Auth a;
    cJSON *o = cJSON_Parse(backend_get_auth(plat).c_str());
    cJSON *c = cJSON_GetObjectItemCaseSensitive(o, "cookies");
    if (cJSON_IsString(c))
        a.cookies = c->valuestring;
    cJSON *h;
    cJSON_ArrayForEach(h, cJSON_GetObjectItemCaseSensitive(o, "headers"))
    {
        if (cJSON_IsString(h) && h->string)
            a.headers[h->string] = h->valuestring;
    }
    cJSON_Delete(o);
    return a;
}

static size_t to_string(char *p, size_t s, size_t n, void *ud)
{
    static_cast<std::string *>(ud)->append(p, s * n);
    return s * n;
}

static std::string request(const std::string &url, const std::map<std::string, std::string> &headers,
                           const std::string *body, long &code)
{
    CURL *c = curl_easy_init();
    struct curl_slist *hl = nullptr;
    for (auto &h : headers)
        hl = curl_slist_append(hl, (h.first + ": " + h.second).c_str());
    std::string out;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, UA);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_string);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    if (body) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body->c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body->size());
    }
    code = 0;
    if (curl_easy_perform(c) == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    if (getenv("SANSON_DEBUG"))
        fprintf(stderr, "[chat] %ld %s -> %.300s\n", code, url.c_str(), out.c_str());
    return out;
}

static std::string path_part(const std::string &url, const std::string &host, int index)
{
    size_t h = url.find(host);
    if (h == std::string::npos)
        return "";
    size_t p = h + host.size();
    for (int i = 0; i <= index; i++) {
        if (p >= url.size() || url[p] != '/')
            return "";
        size_t e = url.find_first_of("/?#", p + 1);
        if (e == std::string::npos)
            e = url.size();
        if (i == index)
            return url.substr(p + 1, e - p - 1);
        p = e;
    }
    return "";
}

static std::string lower(std::string s)
{
    for (auto &c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

static std::string one_line(std::string s)
{
    for (auto &c : s)
        if (c == '\r' || c == '\n')
            c = ' ';
    return s;
}

struct Irc {
    int ws = 0;
    std::string login, token, joined;
    std::chrono::steady_clock::time_point last;
};

static Irc irc;

static std::vector<std::string> irc_lines(bool &closed)
{
    std::vector<std::string> lines;
    char *r = ws_poll(irc.ws);
    cJSON *o = cJSON_Parse(r ? r : "");
    free(r);
    closed = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "closed"));
    cJSON *m;
    cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(o, "messages"))
    {
        if (!cJSON_IsString(m))
            continue;
        std::string all = m->valuestring;
        size_t i = 0;
        while (i < all.size()) {
            size_t e = all.find("\r\n", i);
            if (e == std::string::npos)
                e = all.size();
            if (e > i)
                lines.push_back(all.substr(i, e - i));
            i = e + 2;
        }
    }
    cJSON_Delete(o);
    for (auto &l : lines)
        if (l.rfind("PING", 0) == 0)
            ws_send(irc.ws, ("PONG" + l.substr(4)).c_str());
    return lines;
}

static void irc_close()
{
    if (irc.ws)
        ws_close(irc.ws);
    irc.ws = 0;
    irc.joined.clear();
}

static std::string notice_text(const std::string &l)
{
    size_t p = l.find(" :", l.find(" NOTICE "));
    return p == std::string::npos ? "Twitch rechazó el mensaje" : l.substr(p + 2);
}

static std::string send_twitch(const std::string &url, const std::string &text)
{
    Auth a = load_auth(P_TWITCH);
    std::string token = a.headers["Authorization"];
    if (token.rfind("OAuth ", 0) == 0)
        token = token.substr(6);
    if (token.empty())
        return "Inicia sesión en Twitch para escribir";
    std::string chan = lower(path_part(url, "twitch.tv", 0));
    if (chan.empty() || chan == "videos")
        return "Solo se puede escribir en directos";
    if (token != irc.token) {
        irc_close();
        irc.token = token;
        irc.login.clear();
    }
    if (irc.login.empty()) {
        long code;
        std::string r = request("https://id.twitch.tv/oauth2/validate", {{"Authorization", "OAuth " + token}}, nullptr, code);
        cJSON *o = cJSON_Parse(r.c_str());
        cJSON *l = cJSON_GetObjectItemCaseSensitive(o, "login");
        if (cJSON_IsString(l))
            irc.login = l->valuestring;
        cJSON_Delete(o);
        if (irc.login.empty())
            return code == 401 ? "La sesión de Twitch caducó: vuelve a iniciarla en Ajustes" : "No se pudo validar la sesión de Twitch";
    }
    bool closed = false;
    if (irc.ws) {
        irc_lines(closed);
        if (closed)
            irc_close();
    }
    if (!irc.ws) {
        irc.ws = ws_open("wss://irc-ws.chat.twitch.tv:443", nullptr);
        if (!irc.ws)
            return "No se pudo conectar al chat de Twitch";
        ws_send(irc.ws, "CAP REQ :twitch.tv/tags twitch.tv/commands");
        ws_send(irc.ws, ("PASS oauth:" + token).c_str());
        ws_send(irc.ws, ("NICK " + irc.login).c_str());
        bool ok = false;
        for (int i = 0; i < 100 && !ok; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            for (auto &l : irc_lines(closed)) {
                if (l.find(" 001 ") != std::string::npos)
                    ok = true;
                else if (l.find("NOTICE") != std::string::npos && l.find("auth") != std::string::npos) {
                    irc_close();
                    return "Twitch rechazó la sesión: vuelve a iniciarla en Ajustes";
                }
            }
            if (closed)
                break;
        }
        if (!ok) {
            irc_close();
            return "No se pudo entrar al chat de Twitch";
        }
    }
    if (irc.joined != chan) {
        if (!irc.joined.empty())
            ws_send(irc.ws, ("PART #" + irc.joined).c_str());
        ws_send(irc.ws, ("JOIN #" + chan).c_str());
        irc.joined = chan;
    }
    ws_send(irc.ws, ("PRIVMSG #" + chan + " :" + one_line(text)).c_str());
    irc.last = std::chrono::steady_clock::now();
    for (int i = 0; i < 15; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (auto &l : irc_lines(closed)) {
            if (l.find(" NOTICE #" + chan) != std::string::npos)
                return notice_text(l);
        }
        if (closed) {
            irc_close();
            return "Se cortó la conexión con el chat de Twitch";
        }
    }
    return "";
}

static std::map<std::string, std::string> kick_rooms;

static std::string send_kick(const std::string &url, const std::string &text)
{
    Auth a = load_auth(P_KICK);
    if (a.headers["Authorization"].empty())
        return "Inicia sesión en Kick para escribir";
    std::string slug = lower(path_part(url, "kick.com", 0));
    if (slug.empty() || slug == "video")
        return "Solo se puede escribir en directos";
    std::map<std::string, std::string> h = {{"Authorization", a.headers["Authorization"]},
                                            {"Accept", "application/json"}};
    if (!a.cookies.empty())
        h["Cookie"] = a.cookies;
    long code;
    std::string &room = kick_rooms[slug];
    if (room.empty()) {
        std::string r = request("https://kick.com/api/v2/channels/" + slug, h, nullptr, code);
        cJSON *o = cJSON_Parse(r.c_str());
        cJSON *id = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(o, "chatroom"), "id");
        if (cJSON_IsNumber(id))
            room = std::to_string((long long)id->valuedouble);
        cJSON_Delete(o);
        if (room.empty())
            return "No se encontró el chat de este canal";
    }
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "content", one_line(text).c_str());
    cJSON_AddStringToObject(b, "type", "message");
    char *bs = cJSON_PrintUnformatted(b);
    std::string body = bs;
    free(bs);
    cJSON_Delete(b);
    h["Content-Type"] = "application/json";
    h["Origin"] = "https://kick.com";
    h["Referer"] = "https://kick.com/" + slug;
    std::string r = request("https://kick.com/api/v2/messages/send/" + room, h, &body, code);
    if (code == 200 || code == 201)
        return "";
    if (code == 401)
        return "La sesión de Kick caducó: vuelve a iniciarla en Ajustes";
    std::string msg;
    cJSON *o = cJSON_Parse(r.c_str());
    cJSON *m = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(o, "status"), "message");
    if (!cJSON_IsString(m))
        m = cJSON_GetObjectItemCaseSensitive(o, "message");
    if (cJSON_IsString(m))
        msg = m->valuestring;
    cJSON_Delete(o);
    return msg.empty() ? "Kick rechazó el mensaje (" + std::to_string(code) + ")" : "Kick: " + msg;
}

struct YtChat {
    std::string params, version;
};

static std::map<std::string, YtChat> yt_chats;

static std::string json_string_after(const std::string &page, const std::string &key)
{
    size_t p = page.find("\"" + key + "\":\"");
    if (p == std::string::npos)
        return "";
    p += key.size() + 4;
    size_t e = page.find('"', p);
    return e == std::string::npos ? "" : page.substr(p, e - p);
}

static std::string youtube_id(const std::string &url)
{
    size_t v = url.find("v=");
    if (v != std::string::npos)
        return url.substr(v + 2, url.find_first_of("&#", v + 2) - v - 2);
    std::string live = path_part(url, "youtube.com", 1);
    if (path_part(url, "youtube.com", 0) == "live" && !live.empty())
        return live;
    return path_part(url, "youtu.be", 0);
}

static std::string send_youtube(const std::string &url, const std::string &text)
{
    Auth a = load_auth(P_YOUTUBE);
    if (a.cookies.empty())
        return "Inicia sesión en YouTube para escribir";
    std::string id = youtube_id(url);
    if (id.empty())
        return "No se reconoció el video";
    std::map<std::string, std::string> h = {{"Cookie", a.cookies}};
    long code;
    YtChat &chat = yt_chats[id];
    if (chat.params.empty()) {
        std::string page = request("https://www.youtube.com/live_chat?is_popout=1&v=" + id, h, nullptr, code);
        size_t ep = page.find("\"sendLiveChatMessageEndpoint\"");
        if (ep != std::string::npos)
            chat.params = json_string_after(page.substr(ep), "params");
        chat.version = json_string_after(page, "INNERTUBE_CLIENT_VERSION");
        if (chat.params.empty()) {
            std::string why = page.find("\"logged_in\",\"value\":\"1\"") == std::string::npos &&
                                      page.find("LOGGED_IN\":true") == std::string::npos
                                  ? "La sesión de YouTube no es válida: vuelve a iniciarla en Ajustes"
                                  : "Este chat no permite escribir (solo miembros, suscriptores o desactivado)";
            yt_chats.erase(id);
            return why;
        }
    }
    cJSON *b = cJSON_CreateObject();
    cJSON *client = cJSON_AddObjectToObject(cJSON_AddObjectToObject(b, "context"), "client");
    cJSON_AddStringToObject(client, "clientName", "WEB");
    cJSON_AddStringToObject(client, "clientVersion", chat.version.empty() ? "2.20251015.01.00" : chat.version.c_str());
    cJSON_AddStringToObject(b, "params", chat.params.c_str());
    cJSON *seg = cJSON_CreateObject();
    cJSON_AddStringToObject(seg, "text", one_line(text).c_str());
    cJSON *segs = cJSON_AddArrayToObject(cJSON_AddObjectToObject(b, "richMessage"), "textSegments");
    cJSON_AddItemToArray(segs, seg);
    char *bs = cJSON_PrintUnformatted(b);
    std::string body = bs;
    free(bs);
    cJSON_Delete(b);
    char *hash = sj_sapisid_header(a.cookies.c_str(), "https://www.youtube.com");
    if (!hash)
        return "Falta la cookie SAPISID de YouTube";
    h["Authorization"] = hash;
    free(hash);
    h["Content-Type"] = "application/json";
    h["Origin"] = "https://www.youtube.com";
    h["X-Origin"] = "https://www.youtube.com";
    h["X-Goog-AuthUser"] = "0";
    std::string r = request("https://www.youtube.com/youtubei/v1/live_chat/send_message?prettyPrint=false", h, &body, code);
    if (code == 200 && r.find("\"actions\"") != std::string::npos)
        return "";
    yt_chats.erase(id);
    if (code == 401)
        return "La sesión de YouTube caducó: vuelve a iniciarla en Ajustes";
    std::string msg;
    cJSON *o = cJSON_Parse(r.c_str());
    cJSON *m = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(o, "error"), "message");
    if (cJSON_IsString(m))
        msg = m->valuestring;
    cJSON_Delete(o);
    return msg.empty() ? "YouTube rechazó el mensaje (" + std::to_string(code) + ")" : "YouTube: " + msg;
}

static void send_loop()
{
    for (;;) {
        SendJob j;
        {
            std::unique_lock<std::mutex> lk(q_lock);
            if (!q_cv.wait_for(lk, std::chrono::seconds(10), [] { return !q.empty(); })) {
                lk.unlock();
                if (irc.ws) {
                    bool closed = false;
                    irc_lines(closed);
                    if (closed || std::chrono::steady_clock::now() - irc.last > std::chrono::minutes(10))
                        irc_close();
                }
                continue;
            }
            j = std::move(q.front());
            q.pop_front();
        }
        if (j.reset) {
            irc_close();
            irc.token.clear();
            irc.login.clear();
            kick_rooms.clear();
            yt_chats.clear();
            continue;
        }
        std::string err = j.plat == P_TWITCH ? send_twitch(j.url, j.text)
                          : j.plat == P_KICK ? send_kick(j.url, j.text)
                                             : send_youtube(j.url, j.text);
        ui_done(j.done, err);
    }
}

static void post(SendJob j)
{
    std::lock_guard<std::mutex> g(q_lock);
    if (!started) {
        started = true;
        std::thread(send_loop).detach();
    }
    q.push_back(std::move(j));
    q_cv.notify_one();
}

bool chat_can_send(int plat)
{
    return !backend_get_auth(plat).empty();
}

void chat_send(int plat, const std::string &url, const std::string &text, std::function<void(std::string)> done)
{
    SendJob j;
    j.plat = plat;
    j.url = url;
    j.text = text;
    j.done = done;
    post(std::move(j));
}

void chat_send_reset()
{
    SendJob j;
    j.reset = true;
    post(std::move(j));
}
