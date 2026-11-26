#ifndef SANSON_BACKEND_H
#define SANSON_BACKEND_H

#include <functional>
#include <string>
#include <vector>

#define P_COUNT_MAX 32
extern int P_LatinoTV;
extern int P_COUNT;
extern int P_YOUTUBE, P_TWITCH, P_KICK, P_MUSIC;

void backend_scan_plugins();
int backend_plat_count();
const char *backend_plat_name_dyn(int plat);
std::string backend_plat_id(int plat);
bool backend_comments_enabled(int plat, bool &out);
bool backend_chat_enabled(int plat, bool &out);

struct Item {
    int plat = 0;
    std::string kind; // video, channel, playlist
    std::string id, title, author, author_url, author_thumb, thumb, url;
    std::string album, album_url; // Música: album of a song; playlists: "Álbum • 1990", "50 vídeos"
    std::string note;             // Twitch: the game being played
    long long views = -1, date = 0, subs = -1;
    int duration = 0;
    bool live = false;
    bool is_short() const { return url.find("/shorts/") != std::string::npos; }
};

// What a video is, to keep lists and continuous play apart: normal videos (and channels and
// lists), live streams, YouTube shorts.
enum { CAT_VIDEO, CAT_LIVE, CAT_SHORT, CAT_COUNT };
inline int category(const Item &it)
{
    return it.is_short() ? CAT_SHORT : it.live ? CAT_LIVE : CAT_VIDEO;
}

struct Page {
    int plat = 0;
    int pager = 0;
    bool more = false;
    std::vector<Item> items;
    std::string error;
};

struct Source {
    std::string label;
    std::string url;
    std::string audio;
    int height = 0;
    int kind = 0;
    long long vlen = 0, alen = 0;
    bool raw_player = false;
    std::vector<std::string> mplayer_args;
};

enum { S_HLS, S_RANGE, S_DIRECT, S_HACKSTREAM };

struct AudioTrack {
    std::string label, url;
    long long len = 0;
};

struct Details {
    Item item;
    std::string description;
    std::vector<Source> sources;
    std::vector<AudioTrack> audios;    // one per language, the original first
    std::vector<AudioTrack> dl_audio;  // downloads: the best AAC and the best Opus (original language)
    std::vector<std::string> subtitles;
    std::vector<Item> related;         // what comes next (continuous play)
    std::string error;
    long long likes = -1;
    std::string lyrics_id;  // Música: YouTube Music lyrics page
    std::string fallback;   // Música: where the audio came from when YouTube gave none
};

// Música: synced lines (ms, text) or plain text.
struct Lyrics {
    std::vector<std::pair<long long, std::string>> lines;
    std::string plain, source, error;
};

struct Channel {
    int plat = 0;
    std::string name, url, thumb, banner, description;
    long long subs = -1;
    std::string error;
};

struct ChatMsg {
    int kind = 0;
    std::string name, text, color;
    long long time = -1;
};

struct ChatUpdate {
    int pager = 0;
    bool more = false;
    int interval_ms = 2000;
    long long viewers = -1;
    std::vector<ChatMsg> msgs;
    std::vector<std::pair<std::string, std::string>> emojis;
    std::string error;
};

enum { CHAT_MESSAGE, CHAT_DONATION, CHAT_RAID };

struct Comment {
    std::string author, text, when, likes, replies;
};

struct CommentPage {
    std::vector<Comment> items;
    std::string next, create, count, error;
    bool disabled = false;
};

const char *plat_name(int plat);

// backend.cpp ------------------------------------------------------------------------------
void backend_start(const std::string &plugins_dir, const std::string &host_js);

void backend_home(int plat, std::function<void(Page)> cb);
void backend_search(int plat, const std::string &query, std::function<void(Page)> cb);
void backend_next(int plat, int pager, std::function<void(Page)> cb);
void backend_drop(int plat, int pager);
void backend_details(int plat, const std::string &url, std::function<void(Details)> cb);
void backend_related(int plat, const std::string &url, std::function<void(Page)> cb);
void backend_channel(int plat, const std::string &url, std::function<void(Channel)> cb);
void backend_channel_videos(int plat, const std::string &url, std::function<void(Page)> cb);
// What is new in the channels followed (the account's and the ones followed in Sansón).
void backend_feed(int plat, std::function<void(Page)> cb);
// Channels the account follows (empty without a session).
void backend_following(int plat, std::function<void(std::vector<Item>, std::string error)> cb);
// Subtitle `index` of the video last opened, as SubRip text.
void backend_subtitle(int plat, int index, std::function<void(std::string srt, std::string error)> cb);
// Música
void backend_lyrics(const Details &d, std::function<void(Lyrics)> cb);
void backend_radio(const std::string &url, std::function<void(Page)> cb);

void backend_set_auth(int plat, const std::string &auth_json);
// The platform asks to prove you are not a robot (the app offers to do it in the browser).
#define CAPTCHA_ERROR "La plataforma pide verificar que no eres un robot."
inline bool error_is_captcha(const std::string &e) { return e.rfind(CAPTCHA_ERROR, 0) == 0; }
// Cookies of a verification ("not a robot") passed in the browser, sent with every request.
void backend_set_clearance(int plat, const std::string &cookies, const std::string &ua);
std::string backend_get_auth(int plat);
bool backend_logged(int plat);
void backend_reload(int plat);

void backend_chat_open(int plat, const std::string &url, std::function<void(ChatUpdate)> cb);
void backend_chat_poll(int plat, int pager, std::function<void(ChatUpdate)> cb);
void backend_vod_chat_open(int plat, const std::string &url, std::function<void(ChatUpdate)> cb);
void backend_vod_chat_poll(int plat, int pager, long long ms, std::function<void(ChatUpdate)> cb);
void backend_comments(int plat, const std::string &url, const std::string &token, std::function<void(CommentPage)> cb);
void backend_post_comment(int plat, const std::string &create, const std::string &text,
                          std::function<void(std::string)> cb);

void thumbs_fetch(const std::string &url, std::function<void(std::string)> cb);
void thumbs_fetch_work(const std::string &url, std::function<std::function<void()>(std::string)> work);

// store.cpp --------------------------------------------------------------------------------
std::string config_dir();
std::string read_file(const std::string &path);
void write_file(const std::string &path, const std::string &text, int mode = 0600);

std::string setting(const std::string &key, const std::string &def = "");
void set_setting(const std::string &key, const std::string &value);

std::vector<Item> subs_list();
bool subs_has(const std::string &url);
void subs_toggle(const Item &channel);
std::vector<Item> history_list();
void history_add(const Item &it);
std::vector<Item> music_queue_list();
void music_queue_save(const std::vector<Item> &v);

std::string fmt_count(long long n);
std::string fmt_duration(int secs);
std::string fmt_ago(long long epoch);

// store.cpp: tienda de plugins remota -------------------------------------
struct StorePlugin {
    std::string id, name, author, version, icon_url, zip_url, description;
    bool installed = false;
    std::string local_version;
};
void store_fetch(std::function<void(std::vector<StorePlugin>, std::string)> cb);
void store_install(const StorePlugin &p, std::function<void(bool, std::string)> cb);
void store_uninstall(const std::string &id, std::function<void(bool, std::string)> cb);
bool store_installed(const std::string &id);
std::string store_local_version(const std::string &id);
bool store_has_update(const StorePlugin &p);
void store_restart_app();
void store_install_local(const std::string &zip_path, std::function<void(bool, std::string)> cb);

#endif
