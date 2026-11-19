#ifndef SANSON_APP_H
#define SANSON_APP_H

// The application: one window with the platform tabs and the search on top, the sections on
// the left and the current screen (cards, a video, the settings) in the middle.
//   app.cpp          layout, navigation and the card screens; main()
//   app_watch.cpp    the video screen: player, continuous play, panels, chat, comments
//   app_music.cpp    Música: queue, shuffle, repeat, radio
//   app_settings.cpp accounts and preferences
//   app_download.cpp downloads (YouTube and Música): the dialog and the Descargas page

#include <FL/Fl_Box.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <deque>
#include <set>

#include "views.h"
#include "store.h"
class DarkScroll;

#define VERSION "1.0.0"

enum View { V_HOME, V_SEARCH, V_FEED, V_SUBS, V_HISTORY, V_CHANNEL, V_WATCH, V_SETTINGS, V_DOWNLOADS, V_STORE };
enum { NAV_HOME, NAV_FEED, NAV_SUBS, NAV_HISTORY, NAV_DOWNLOADS, NAV_SETTINGS, NAV_STORE, NAV_RESTART, NAV_EXIT, NAV_COUNT };

class MainWindow : public Fl_Double_Window {
public:
    MainWindow(int w, int h, const char *l) : Fl_Double_Window(w, h, l) {}
    void resize(int X, int Y, int W, int H) override;
};

struct App {
    enum { PANEL_NONE, PANEL_LIVE, PANEL_REPLAY, PANEL_COMMENTS, PANEL_LYRICS, PANEL_QUEUE };

    bool ready = false;
    MainWindow *win;
    Bar *top, *side, *status_bar, *header, *panel_tabs;
    Fl_Window *ovl; // the controls over the bottom of the video, shown while the pointer is on it
    FlatButton *tabs[P_COUNT_MAX];
    Fl_Input *search;
    FlatButton *search_btn, *nav[NAV_COUNT], *back_btn, *sub_btn, *reload_btn;
    Label *logo, *header_title, *status, *time_label, *watch_title, *watch_meta;
    FlatButton *watch_author;
    DarkScroll *scroll;
    Grid *grid;
    Fl_Group *watch;
    DarkScroll *settings;
    VideoBox *video;
    Fl_Box *video_bg;
    FlatButton *play_btn, *back10, *fwd10, *vol_down, *vol_up, *full_btn, *subscribe_watch;
    Seek *seek;
    Fl_Choice *quality;
    FlatButton *lang_btn, *panel_btn, *auto_btn, *dl_btn, *tab_btn[2];
    int audio_sel = 0, sub_sel = 0, sub_count = 0;
    std::string sub_path;
    Fl_Text_Display *desc;
    Fl_Text_Buffer *desc_buf;
    ChatView *chat;
    CommentView *comments;
    RelatedView *related;
    FlatButton *chat_send_btn;
    Fl_Input *chat_in;
    Label *chat_hint;
    bool chat_sending = false;
    int chat_pager = 0, chat_gen = 0, chat_fails = 0;
    int panel = PANEL_NONE;
    bool related_tab = false; // videos: the second tab of the panel ("Siguientes") is shown
    std::deque<ChatMsg> vod_pending;
    std::set<std::string> vod_seen;
    long long vod_last = -1;
    bool vod_busy = false;
    std::string com_next, com_create;
    bool com_loading = false;
    double reload_pos = 0;
    int vid_w = 0, vid_h = 0;

    // Continuous play: when a video ends the next one opens after a short countdown. The next
    // one is the first related video not watched yet, or the next card of the list it was
    // opened from.
    std::vector<Item> source_list;
    std::set<std::string> watched;
    int countdown = 0;
    Item next_item;
    bool autoplay();
    std::vector<Item> upcoming();
    bool pick_next(Item &out);
    void on_video_end();
    void cancel_countdown();
    void play_next();
    void update_related();

    // Música
    LyricsView *lyrics;
    QueueView *queue_view;
    FlatButton *prev_btn, *next_btn, *shuffle_btn, *repeat_btn, *radio_btn;
    FlatButton *play_all_btn, *shuffle_all_btn;
    std::vector<Item> queue, queue_orig; // play order, and the order before shuffling
    int qpos = -1;
    int repeat_mode = 0; // 0 no, 1 all, 2 one
    bool shuffle = false, radio = true, end_handled = false, radio_busy = false;
    // Música keeps playing while browsing: the watch screen is hidden, not stopped.
    bool music_bg = false;
    FlatButton *now_btn;
    void play_list(const std::vector<Item> &list, int idx, bool shuffled);
    void play_queue(int i);
    void next_track(bool automatic);
    void prev_track();
    void queue_add(const std::vector<Item> &list, bool next);
    void queue_menu(int row, int action);
    void set_shuffle(bool on);
    void queue_save();
    void queue_load();
    void music_buttons();
    void music_menu(int i);
    void with_songs(const Item &list, std::function<void(std::vector<Item>)> f);
    void leave_watch();
    void show_watch();

    int plat = P_YOUTUBE;
    View view = V_HOME;
    View grid_view = V_HOME;
    int gen = 0;       // grid requests (pages)
    int watch_gen = 0; // the open video: separate so opening one does not drop a page still loading
    int pager = 0;
    bool more = false, loading = false;
    std::string query, channel_url, channel_name, grid_title;
    Details cur;
    int cur_source = -1;
    bool fullscreen = false;
    unsigned long fs_win = 0;
    int fs_w = 0, fs_h = 0;
    double wd_pos = -1, wd_since = 0;
    std::deque<double> stalls;

    void set_title(const std::string &t);
    void set_status(const std::string &s) { status->set(s); }

    void build();
    void build_watch(); // app_watch.cpp
    void layout();
    void layout_top(int W, int H, int side_w);
    void layout_watch(int cx, int by, int cw, int bh);
    void layout_overlay(int ow, int KH, bool music, bool live);
    void select_plat(int p);
    void nav_to(int i);
    void go_back();
    void select_nav(int i);
    void show_grid(View v);
    void load_page(View v);
    void on_page(const Page &pg, int g, bool append);
    void open_card(int i);
    void open_item(const Item &it);
    void open_channel(int p, const std::string &url, const std::string &name);
    void reload();
    void clear_search();

    void play_source(int idx, double start);
    void update_controls();
    void watchdog();
    void lang_menu();
    void select_audio(int v);
    void select_subtitle(int v);
    void toggle_fullscreen();
    void stop_watch();
    void seek_rel(double d);

    bool panel_visible();
    bool panel_below(); // narrow window: the panel goes under the video
    void toggle_panel();
    void select_tab(int t);
    void start_chat();
    void stop_chat();
    void on_chat(const ChatUpdate &u, int g);
    void update_chat_input();
    void send_chat();
    void vod_step();
    void on_vod_chat(const ChatUpdate &u, int g);
    void load_comments();
    void on_comments(const CommentPage &cp, int g, bool first);
    void post_comment();

    void show_settings();
    int settings_w = 0; // width the settings page was built for

    void show_downloads();
    void show_store();
    std::vector<StorePlugin> store_items;
    int store_gen = 0;
};

extern App *app;

void quit_app();
void verify_captcha(int plat); // app_settings.cpp
// app_download.cpp
bool can_download(const Details &d);
void download_dialog();
void downloads_layout();
void clear_downloads_view();
bool downloads_quit(); // false: the user chose to stay (downloads running)
void search_now();
void player_changed();
int fs_events(void *event, void *);
void watchdog_tick(void *);

#endif
