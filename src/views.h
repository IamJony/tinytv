#ifndef SANSON_VIEWS_H
#define SANSON_VIEWS_H

// The big widgets: the card grid, the video box and the side panels (chat, comments, what
// comes next, lyrics, queue).

#include <FL/Fl_Window.H>
#include <deque>
#include <map>

#include "ui.h"

// Cards stretch between CARD_MIN and CARD_MAX: as many columns as fit, no empty margin.
// Cards of videos, channels and lists. When the list mixes normal videos, live streams and
// shorts, a row of buttons on top shows one kind at a time. Indexes given to and by the grid
// are always indexes of `items`.
class Grid : public Fl_Widget {
    struct CardText {
        bool done = false;
        std::vector<std::string> title;
        std::string author, meta;
    };
    std::vector<CardText> text;
    int text_w = -1;
    int cw = 176, th = 99, ch = 161; // card width, thumbnail height, card height
    std::vector<int> shown;          // the items of the chosen kind, in order
    std::vector<int> slot;           // per item: its place among the shown ones, or -1
    size_t synced = 0;               // items.size() when `shown` was made
    int counts[CAT_COUNT] = {};

    const CardText &card_text(int i);
    void damage_card(int i);
    void draw_card(int i);
    void sync();
    int top();     // y of the first row of cards
    int kinds();   // how many kinds the list has
    int chip_at(int mx, int my);
    void chip_box(int c, int &bx, int &bw);

public:
    static const int CARD_MIN = 168, CARD_MAX = 236, GAP = 10, CHIPS_H = 36;
    std::vector<Item> items;
    std::string message;
    int hover = -1, cols = 1;
    int filter = -1; // kind shown (CAT_*), chosen when the first items arrive
    std::function<void(int)> on_open, on_author, on_menu; // on_menu: right click on a card
    std::function<void()> on_filter;

    Grid(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void damage_thumb(const std::string &url);
    void clear_items();
    int content_h(int width); // also sets the columns and the card size for that width
    void card_xy(int i, int &cx, int &cy);
    int hit(int mx, int my);
    void draw() override;
    int handle(int e) override;
};

class VideoBox : public Fl_Window {
public:
    std::string text;
    std::string cover; // Música: the cover is shown instead of a picture
    std::function<void()> on_double, on_click;
    VideoBox(int x, int y, int w, int h);
    void draw() override;
    int handle(int e) override;
};

class ChatView : public Fl_Widget {
    struct Tok {
        std::string text;
        Fl_RGB_Image *img;
        int w;
        bool bold;
        Fl_Color color;
    };
    struct Msg {
        ChatMsg m;
        std::vector<std::vector<Tok>> lines;
        int layout_w = -1;
    };
    std::deque<Msg> msgs;
    int back = 0;
    static const int HEAD = 26, LINE = 17, EMOTE_H = 17;

    Fl_RGB_Image *emote(const std::string &key);
    void layout_msg(Msg &m, int width);

public:
    std::map<std::string, std::string> emoji_urls;
    std::string status, title = "Chat";
    long long viewers = -1;

    ChatView(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void clear_all();
    void clear_msgs();
    void add(const ChatUpdate &u);
    void draw() override;
    int handle(int e) override;
};

class CommentView : public Fl_Widget {
    struct Entry {
        Comment c;
        std::vector<std::string> lines;
        std::string head, foot;
        int layout_w = -1;
    };
    std::vector<Entry> entries;
    int scroll_y = 0;
    static const int HEAD = 26, LINE = 15;

    void layout_entry(Entry &e, int width);
    int entry_h(Entry &e);
    int total_h();

public:
    std::string status, count;
    std::function<void()> on_more;
    bool can_more = false;

    CommentView(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void clear_all();
    void add(const std::vector<Comment> &list, bool front = false);
    void check_more();
    void draw() override;
    int handle(int e) override;
};

// What comes next: one row per video; the one continuous play will open is marked.
class RelatedView : public Fl_Widget {
    int scroll_y = 0;
    static const int HEAD = 26, ROW = 64, TW = 96, TH = 54;
    int row_at(int my);

public:
    std::vector<Item> items;
    int next = -1, hover = -1;
    std::string status;
    std::function<void(int)> on_open;

    RelatedView(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void set(const std::vector<Item> &v, int next_index);
    void damage_thumb(const std::string &url);
    void draw() override;
    int handle(int e) override;
};

// Música: lyrics next to the cover. Synced lines follow the song (the current one highlighted
// and kept in the middle; a click jumps there); plain lyrics just scroll.
class LyricsView : public Fl_Widget {
    std::vector<std::pair<long long, std::string>> lines;
    std::vector<std::string> wrapped; // plain lyrics, or synced lines wrapped (line index in owner)
    std::vector<int> owner;
    int layout_w = -1, scroll_y = 0, cur = -1;
    double manual_until = 0; // the wheel takes over for a while
    static const int HEAD = 26, LINE = 18;

    void relayout();
    int first_row(int line);

public:
    std::string plain, source, status;
    std::function<void(double)> on_seek;

    LyricsView(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void set(const Lyrics &l);
    void clear_all(const std::string &st);
    void set_time(long long ms, double now);
    void draw() override;
    int handle(int e) override;
};

// Música: the play queue. Double click plays a song; right click: play, move, remove, clear.
class QueueView : public Fl_Widget {
    int scroll_y = 0;
    static const int HEAD = 26, ROW = 34;

public:
    std::vector<Item> *items = nullptr;
    int current = -1, hover = -1;
    std::string mode; // "aleatorio, repetir todo"...
    std::function<void(int)> on_play;
    std::function<void(int, int)> on_menu; // row, action

    QueueView(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void show_current();
    int row_at(int my);
    void draw() override;
    int handle(int e) override;
};

#endif
