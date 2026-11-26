#include "views.h"

#include <algorithm>
#include <cstring>

// --- Grid --------------------------------------------------------------------------------------------

static const char *cat_label[CAT_COUNT] = {"Videos", "En vivo", "Shorts"};

// The kinds present and the ones shown. The filter starts on the kind of the first video of the
// list (a live channel opens on its live stream) and stays while the list grows.
void Grid::sync()
{
    if (synced == items.size() && slot.size() == items.size())
        return;
    for (int &c : counts)
        c = 0;
    int first = -1;
    for (auto &it : items) {
        int c = category(it);
        counts[c]++;
        if (first < 0 && it.kind != "channel")
            first = c;
    }
    if (filter < 0 || counts[filter] == 0)
        filter = first >= 0 ? first : CAT_VIDEO;
    if (counts[filter] == 0)
        for (int c = 0; c < CAT_COUNT; c++)
            if (counts[c]) {
                filter = c;
                break;
            }
    bool all = kinds() < 2;
    shown.clear();
    slot.assign(items.size(), -1);
    for (int i = 0; i < (int)items.size(); i++)
        if (all || category(items[i]) == filter) {
            slot[i] = (int)shown.size();
            shown.push_back(i);
        }
    synced = items.size();
    if (hover >= (int)items.size() || (hover >= 0 && slot[hover] < 0))
        hover = -1;
}

int Grid::kinds()
{
    int n = 0;
    for (int c : counts)
        n += c > 0;
    return n;
}

int Grid::top()
{
    return y() + GAP + (kinds() > 1 ? CHIPS_H : 0);
}

void Grid::chip_box(int c, int &bx, int &bw)
{
    fl_font(FL_HELVETICA_BOLD, 12);
    bx = x() + GAP;
    for (int k = 0; k < CAT_COUNT; k++) {
        if (!counts[k])
            continue;
        std::string l = std::string(cat_label[k]) + "  " + std::to_string(counts[k]);
        bw = (int)fl_width(l.c_str()) + 26;
        if (k == c)
            return;
        bx += bw + 6;
    }
    bw = 0;
}

int Grid::chip_at(int mx, int my)
{
    if (kinds() < 2 || my < y() + GAP || my >= y() + GAP + 26)
        return -1;
    for (int c = 0; c < CAT_COUNT; c++) {
        int bx, bw;
        chip_box(c, bx, bw);
        if (counts[c] && mx >= bx && mx < bx + bw)
            return c;
    }
    return -1;
}

const Grid::CardText &Grid::card_text(int i)
{
    if (text_w != cw || text.size() != items.size()) {
        if (text_w != cw)
            text.clear();
        text.resize(items.size());
        text_w = cw;
    }
    CardText &t = text[i];
    if (t.done)
        return t;
    const Item &it = items[i];
    bool channel = it.kind == "channel";
    t.done = true;
    fl_font(FL_HELVETICA_BOLD, 11);
    t.title = wrap(clean(it.title), cw - 16, channel ? 1 : 2);
    fl_font(FL_HELVETICA, 10);
    if (!channel)
        t.author = fit(clean(it.author), cw - 16);
    if (channel) {
        t.meta = it.subs >= 0 ? fmt_count(it.subs) + " seguidores" : it.plat == P_MUSIC ? "Artista" : "Canal";
    } else if (it.kind == "playlist") {
        t.meta = clean(it.album);
        if (t.meta.empty())
            t.meta = "Lista";
    } else if (it.plat == P_MUSIC) {
        // Songs: the album; albums and playlists: "Álbum • 1990".
        t.meta = clean(it.album);
        if (t.meta.empty() && it.views >= 0)
            t.meta = fmt_count(it.views) + " reproducciones";
    } else {
        if (it.views >= 0)
            t.meta = fmt_count(it.views) + (it.live ? " espectadores" : " vistas");
        std::string extra = it.live ? clean(it.note) : fmt_ago(it.date);
        if (!extra.empty())
            t.meta += (t.meta.empty() ? "" : " · ") + extra;
    }
    t.meta = fit(t.meta, cw - 16);
    return t;
}

void Grid::damage_card(int i)
{
    if (i < 0 || i >= (int)slot.size() || slot[i] < 0)
        return;
    int cx, cy;
    card_xy(i, cx, cy);
    damage(FL_DAMAGE_ALL, cx, cy, cw, ch);
}

void Grid::damage_thumb(const std::string &url)
{
    for (int i = 0; i < (int)items.size(); i++)
        if (items[i].thumb == url)
            damage_card(i);
}

void Grid::clear_items()
{
    items.clear();
    text.clear();
    shown.clear();
    slot.clear();
    synced = 0;
    filter = -1;
    hover = -1;
    for (int &c : counts)
        c = 0;
}

int Grid::content_h(int width)
{
    sync();
    cols = std::max(1, (width - GAP) / (CARD_MIN + GAP));
    // In steps of 8 px: the thumbnails are scaled again only when the size really changes.
    cw = std::min(CARD_MAX, (width - GAP) / cols - GAP) / 8 * 8;
    cw = std::max(cw, std::min(CARD_MIN, width - 2 * GAP));
    th = cw * 9 / 16;
    ch = th + 62;
    int rows = ((int)shown.size() + cols - 1) / cols;
    return std::max(rows * (ch + GAP) + GAP + 40 + (kinds() > 1 ? CHIPS_H : 0), 100);
}

void Grid::card_xy(int i, int &cx, int &cy)
{
    int s = i >= 0 && i < (int)slot.size() ? std::max(0, slot[i]) : 0;
    int used = cols * cw + (cols - 1) * GAP;
    int left = x() + std::max(GAP, (w() - used) / 2);
    cx = left + (s % cols) * (cw + GAP);
    cy = top() + (s / cols) * (ch + GAP);
}

int Grid::hit(int mx, int my)
{
    int used = cols * cw + (cols - 1) * GAP;
    int left = x() + std::max(GAP, (w() - used) / 2);
    if (mx < left || my < top())
        return -1;
    int c = (mx - left) / (cw + GAP), r = (my - top()) / (ch + GAP);
    int s = r * cols + c;
    if (c >= cols || s >= (int)shown.size())
        return -1;
    int i = shown[s], cx, cy;
    card_xy(i, cx, cy);
    return mx < cx + cw && my < cy + ch ? i : -1;
}

void Grid::draw_card(int i)
{
    int cx, cy;
    card_xy(i, cx, cy);
    if (!fl_not_clipped(cx, cy, cw, ch))
        return;
    const Item &it = items[i];
    bool hv = i == hover;
    fl_rectf(cx, cy, cw, ch, hv ? C_BTN : C_PANEL);
    bool channel = it.kind == "channel";
    fl_rectf(cx, cy, cw, th, C_BAR);
    Fl_RGB_Image *img = get_thumb(it.thumb, cw, th, channel || it.plat == P_MUSIC);
    if (img) {
        fl_push_clip(cx, cy, cw, th);
        img->draw(cx + (cw - img->w()) / 2, cy + (th - img->h()) / 2);
        fl_pop_clip();
    } else {
        fl_color(plat_color[it.plat]);
        fl_font(FL_HELVETICA_BOLD, 22);
        fl_draw(plat_name(it.plat), cx, cy, cw, th, FL_ALIGN_CENTER, nullptr, 0);
    }
    fl_font(FL_HELVETICA_BOLD, 10);
    if (it.live) {
        int bw = (int)fl_width("EN VIVO") + 8;
        fl_rectf(cx + 5, cy + th - 19, bw, 14, C_RED);
        fl_color(FL_WHITE);
        fl_draw("EN VIVO", cx + 5, cy + th - 19, bw, 14, FL_ALIGN_CENTER, nullptr, 0);
    } else if (it.is_short()) {
        int bw = (int)fl_width("SHORT") + 8;
        fl_rectf(cx + 5, cy + th - 19, bw, 14, fl_rgb_color(10, 10, 12));
        fl_color(FL_WHITE);
        fl_draw("SHORT", cx + 5, cy + th - 19, bw, 14, FL_ALIGN_CENTER, nullptr, 0);
    } else if (it.duration > 0) {
        std::string d = fmt_duration(it.duration);
        int bw = (int)fl_width(d.c_str()) + 8;
        fl_rectf(cx + cw - bw - 5, cy + th - 19, bw, 14, fl_rgb_color(10, 10, 12));
        fl_color(FL_WHITE);
        fl_draw(d.c_str(), cx + cw - bw - 5, cy + th - 19, bw, 14, FL_ALIGN_CENTER, nullptr, 0);
    }
    if (hv)
        fl_rectf(cx, cy + th - 3, cw, 3, plat_color[it.plat]);
    fl_rectf(cx, cy + th, 3, ch - th, plat_color[it.plat]);
    const CardText &t = card_text(i);
    fl_font(FL_HELVETICA_BOLD, 11);
    fl_color(C_TEXT);
    int ty = cy + th + 15;
    for (auto &l : t.title) {
        fl_draw(l.c_str(), cx + 9, ty);
        ty += 13;
    }
    fl_font(FL_HELVETICA, 10);
    int my = cy + ch - 20;
    fl_color(hv && !t.author.empty() ? C_ACCENT : C_DIM);
    fl_draw(t.author.c_str(), cx + 9, my);
    fl_color(C_DIM);
    fl_draw(t.meta.c_str(), cx + 9, my + 13);
}

void Grid::draw()
{
    sync();
    fl_rectf(x(), y(), w(), h(), C_BG);
    if (kinds() > 1) {
        for (int c = 0; c < CAT_COUNT; c++) {
            if (!counts[c])
                continue;
            int bx, bw;
            chip_box(c, bx, bw);
            bool on = c == filter;
            fl_rectf(bx, y() + GAP, bw, 26, on ? C_SEL : C_BTN);
            fl_font(FL_HELVETICA_BOLD, 12);
            fl_color(on ? FL_WHITE : C_TEXT);
            std::string l = std::string(cat_label[c]) + "  " + std::to_string(counts[c]);
            fl_draw(l.c_str(), bx, y() + GAP, bw, 26, FL_ALIGN_CENTER, nullptr, 0);
        }
    }
    for (int i : shown)
        draw_card(i);
    if (!message.empty()) {
        fl_font(FL_HELVETICA_BOLD, 13);
        fl_color(C_DIM);
        int my = shown.empty() ? y() + 60 : y() + h() - 34;
        fl_draw(message.c_str(), x() + 20, my, w() - 40, 24, FL_ALIGN_CENTER | FL_ALIGN_WRAP, nullptr, 0);
    }
}

int Grid::handle(int e)
{
    switch (e) {
    case FL_ENTER:
        return 1;
    case FL_MOVE: {
        int hv = hit(Fl::event_x(), Fl::event_y());
        bool hand = hv >= 0 || chip_at(Fl::event_x(), Fl::event_y()) >= 0;
        fl_cursor(hand ? FL_CURSOR_HAND : FL_CURSOR_DEFAULT);
        if (hv != hover) {
            damage_card(hover);
            damage_card(hv);
            hover = hv;
        }
        return 1;
    }
    case FL_LEAVE:
        damage_card(hover);
        hover = -1;
        fl_cursor(FL_CURSOR_DEFAULT);
        return 1;
    case FL_PUSH: {
        int chip = chip_at(Fl::event_x(), Fl::event_y());
        if (chip >= 0) {
            if (chip != filter) {
                filter = chip;
                synced = (size_t)-1;
                sync();
                if (on_filter)
                    on_filter();
            }
            return 1;
        }
        int i = hit(Fl::event_x(), Fl::event_y());
        if (i < 0)
            return 0;
        if (Fl::event_button() == FL_RIGHT_MOUSE) {
            if (on_menu)
                on_menu(i);
            return 1;
        }
        int cx, cy;
        card_xy(i, cx, cy);
        bool author = Fl::event_y() > cy + ch - 31 && Fl::event_y() < cy + ch - 16 &&
                      items[i].kind != "channel" && !items[i].author_url.empty();
        if (author && on_author)
            on_author(i);
        else if (on_open)
            on_open(i);
        return 1;
    }
    }
    return Fl_Widget::handle(e);
}

// --- VideoBox ------------------------------------------------------------------------------------------

VideoBox::VideoBox(int x, int y, int w, int h) : Fl_Window(x, y, w, h)
{
    color(FL_BLACK);
    box(FL_FLAT_BOX);
    end();
}

void VideoBox::draw()
{
    fl_rectf(0, 0, w(), h(), FL_BLACK);
    if (!cover.empty()) {
        int max_box = std::min(w() - 24, std::max(64, h() * 58 / 100));
        int side = std::max(48, (max_box / 16) * 16);
        Fl_RGB_Image *img = get_thumb(cover, side, side, true);
        if (img) {
            int ix = (w() - img->w()) / 2;
            int iy = (h() - img->h()) / 2;
            fl_rect(ix - 2, iy - 2, img->w() + 4, img->h() + 4, C_LINE);
            img->draw(ix, iy);
        }
    }
    if (!text.empty()) {
        if (!cover.empty()) // a band under the text so it reads over the cover
            fl_rectf(0, h() / 2 - 22, w(), 44, fl_rgb_color(10, 10, 12));
        fl_font(FL_HELVETICA_BOLD, 15);
        fl_color(C_DIM);
        fl_draw(text.c_str(), 10, 0, w() - 20, h(), FL_ALIGN_CENTER | FL_ALIGN_WRAP, nullptr, 0);
    }
}

int VideoBox::handle(int e)
{
    if (e == FL_PUSH) {
        if (Fl::event_clicks() > 0) {
            Fl::event_clicks(0);
            if (on_double)
                on_double();
        } else if (on_click) {
            on_click();
        }
        return 1;
    }
    return Fl_Window::handle(e);
}

// --- ChatView ------------------------------------------------------------------------------------------

static Fl_Color parse_color(const std::string &c, Fl_Color def)
{
    if (c.size() < 7 || c[0] != '#')
        return def;
    unsigned v = strtoul(c.substr(1, 6).c_str(), nullptr, 16);
    int r = v >> 16 & 255, g = v >> 8 & 255, b = v & 255;
    if (r * 3 + g * 6 + b < 1100) { // too dark on the panel: lighter
        r = (r + 255) / 2;
        g = (g + 255) / 2;
        b = (b + 255) / 2;
    }
    return fl_rgb_color(r, g, b);
}

struct Emote {
    Fl_RGB_Image *img = nullptr;
    bool pending = false;
};

static std::map<std::string, Emote> emotes;

static Fl_RGB_Image *scale_emote(Fl_RGB_Image *raw, int height)
{
    if (!raw)
        return nullptr;
    int w = std::max(1, raw->w() * height / raw->h());
    Fl_RGB_Image *img = (Fl_RGB_Image *)raw->copy(w, height);
    delete raw;
    return img;
}

Fl_RGB_Image *ChatView::emote(const std::string &key)
{
    auto it = emoji_urls.find(key);
    if (it == emoji_urls.end())
        return nullptr;
    std::string url = it->second;
    size_t d = url.find("/emoticons/v2/");
    if (d != std::string::npos) { // Twitch: the still version of animated emotes
        size_t def = url.find("/default/", d);
        if (def != std::string::npos)
            url.replace(def, 9, "/static/");
    }
    Emote &e = emotes[url];
    if (!e.img && !e.pending) {
        e.pending = true;
        thumbs_fetch_work(url, [url, this](std::string data) -> std::function<void()> {
            bool gif = data.size() > 6 && !memcmp(data.data(), "GIF8", 4);
            Fl_RGB_Image *img = data.empty() || gif ? nullptr : scale_emote(decode_raw(data), EMOTE_H);
            return [url, this, img, gif, data] {
                emotes[url].img = gif ? scale_emote(decode_raw(data), EMOTE_H) : img;
                for (auto &m : msgs)
                    m.layout_w = -1;
                redraw();
            };
        });
    }
    return e.img;
}

void ChatView::layout_msg(Msg &m, int width)
{
    m.layout_w = width;
    m.lines.clear();
    std::vector<Tok> toks;
    Fl_Color nc = m.m.kind == CHAT_DONATION ? C_ACCENT : parse_color(m.m.color, C_ACCENT);
    std::string name = clean(m.m.name);
    if (m.m.kind == CHAT_RAID) {
        toks.push_back({"Raid hacia " + name, nullptr, 0, true, C_ACCENT});
    } else {
        toks.push_back({name + (m.m.kind == CHAT_DONATION ? " $" : ":"), nullptr, 0, true, nc});
        const std::string &text = m.m.text;
        size_t i = 0;
        while (i <= text.size()) {
            size_t sp = text.find(' ', i);
            if (sp == std::string::npos)
                sp = text.size();
            std::string word = text.substr(i, sp - i);
            i = sp + 1;
            if (word.empty())
                continue;
            bool had = false;
            // Emotes come as __name__ inside the text.
            while (!word.empty()) {
                size_t a = word.find("__");
                size_t b = a == std::string::npos ? a : word.find("__", a + 2);
                while (b != std::string::npos && !emoji_urls.count(word.substr(a + 2, b - a - 2))) {
                    a = b;
                    b = word.find("__", a + 2);
                }
                if (b == std::string::npos)
                    break;
                std::string key = word.substr(a + 2, b - a - 2);
                if (a > 0) {
                    std::string pre = clean(word.substr(0, a));
                    if (!pre.empty())
                        toks.push_back({pre, nullptr, 0, false, C_TEXT});
                }
                Fl_RGB_Image *img = emote(key);
                if (img)
                    toks.push_back({"", img, img->w(), false, C_TEXT});
                else
                    toks.push_back({"[emote]", nullptr, 0, false, C_DIM});
                word = word.substr(b + 2);
                had = true;
            }
            if (had && word.empty())
                continue;
            std::string c = clean(word);
            if (!c.empty())
                toks.push_back({c, nullptr, 0, false, C_TEXT});
        }
    }
    std::vector<Tok> line;
    int x = 0;
    fl_font(FL_HELVETICA, 12);
    int space = (int)fl_width(" ");
    for (auto &t : toks) {
        if (!t.img) {
            fl_font(t.bold ? FL_HELVETICA_BOLD : FL_HELVETICA, 12);
            t.w = (int)fl_width(t.text.c_str());
            if (t.w > width) {
                t.text = fit(t.text, width);
                t.w = (int)fl_width(t.text.c_str());
            }
        }
        if (!line.empty() && x + space + t.w > width) {
            m.lines.push_back(line);
            line.clear();
            x = 0;
        }
        x += (line.empty() ? 0 : space) + t.w;
        line.push_back(t);
    }
    if (!line.empty())
        m.lines.push_back(line);
}

void ChatView::clear_all()
{
    msgs.clear();
    emoji_urls.clear();
    back = 0;
    viewers = -1;
    redraw();
}

void ChatView::clear_msgs()
{
    msgs.clear();
    back = 0;
    redraw();
}

void ChatView::add(const ChatUpdate &u)
{
    for (auto &e : u.emojis)
        emoji_urls[e.first] = e.second;
    if (u.viewers >= 0)
        viewers = u.viewers;
    for (auto &m : u.msgs) {
        Msg x;
        x.m = m;
        msgs.push_back(x);
    }
    while (msgs.size() > 300)
        msgs.pop_front();
    if (!u.msgs.empty() || !u.emojis.empty() || u.viewers >= 0)
        redraw();
}

void ChatView::draw()
{
    fl_push_clip(x(), y(), w(), h());
    fl_rectf(x(), y(), w(), h(), C_PANEL);
    fl_rectf(x(), y(), w(), HEAD, C_BAR);
    fl_font(FL_HELVETICA_BOLD, 12);
    fl_color(C_TEXT);
    std::string head = title;
    if (viewers >= 0)
        head += " · " + fmt_count(viewers) + " espectadores";
    fl_draw(head.c_str(), x() + 10, y(), w() - 20, HEAD, FL_ALIGN_LEFT, nullptr, 0);
    int width = w() - 20;
    int bottom = y() + h() - 6 + back;
    fl_push_clip(x(), y() + HEAD, w(), h() - HEAD);
    for (int i = (int)msgs.size() - 1; i >= 0 && bottom > y() + HEAD; i--) {
        Msg &m = msgs[i];
        if (m.layout_w != width)
            layout_msg(m, width);
        int mh = (int)m.lines.size() * LINE + 4;
        int top = bottom - mh;
        if (top < y() + h()) {
            int ly = top;
            for (auto &line : m.lines) {
                int lx = x() + 10;
                for (auto &t : line) {
                    if (t.img) {
                        t.img->draw(lx, ly + (LINE - EMOTE_H) / 2);
                    } else {
                        fl_font(t.bold ? FL_HELVETICA_BOLD : FL_HELVETICA, 12);
                        fl_color(t.color);
                        fl_draw(t.text.c_str(), lx, ly + LINE - 5);
                    }
                    lx += t.w + (int)fl_width(" ");
                }
                ly += LINE;
            }
        }
        bottom = top;
    }
    fl_pop_clip();
    if (msgs.empty() && !status.empty()) {
        fl_font(FL_HELVETICA, 12);
        fl_color(C_DIM);
        fl_draw(status.c_str(), x() + 10, y() + HEAD + 10, w() - 20, 60, FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP,
                nullptr, 0);
    } else if (!status.empty()) {
        fl_font(FL_HELVETICA, 11);
        fl_color(C_DIM);
        int sw = (int)fl_width(status.c_str());
        fl_draw(status.c_str(), x() + w() - 10 - sw, y(), sw, HEAD, FL_ALIGN_RIGHT);
    }
    if (back > 0) {
        fl_rectf(x(), y() + h() - 22, w(), 22, C_SEL);
        fl_font(FL_HELVETICA_BOLD, 11);
        fl_color(FL_WHITE);
        fl_draw("Mensajes anteriores · clic para volver al final", x(), y() + h() - 22, w(), 22, FL_ALIGN_CENTER);
    }
    fl_pop_clip();
}

int ChatView::handle(int e)
{
    if (e == FL_ENTER || e == FL_MOVE)
        return 1;
    if (e == FL_MOUSEWHEEL) {
        back = std::max(0, back - Fl::event_dy() * LINE * 3);
        redraw();
        return 1;
    }
    if (e == FL_PUSH && back > 0 && Fl::event_y() > y() + h() - 22) {
        back = 0;
        redraw();
        return 1;
    }
    return Fl_Widget::handle(e);
}

// --- CommentView ---------------------------------------------------------------------------------------

void CommentView::layout_entry(Entry &e, int width)
{
    e.layout_w = width;
    fl_font(FL_HELVETICA, 12);
    e.lines.clear();
    const std::string &text = e.c.text;
    size_t i = 0;
    while (i <= text.size()) {
        size_t nl = text.find('\n', i);
        if (nl == std::string::npos)
            nl = text.size();
        std::string para = clean(text.substr(i, nl - i));
        for (auto &l : wrap(para, width, 1000))
            e.lines.push_back(l);
        if (para.empty() && nl < text.size())
            e.lines.push_back("");
        i = nl + 1;
    }
    fl_font(FL_HELVETICA_BOLD, 11);
    e.head = fit(clean(e.c.author), width - 90);
    std::string f;
    if (!e.c.likes.empty() && e.c.likes != "0")
        f = "Me gusta: " + e.c.likes;
    if (!e.c.replies.empty() && e.c.replies != "0")
        f += (f.empty() ? "" : " · ") + std::string("Respuestas: ") + e.c.replies;
    e.foot = f;
}

int CommentView::entry_h(Entry &e)
{
    int width = w() - 30;
    if (e.layout_w != width)
        layout_entry(e, width);
    return 18 + (int)e.lines.size() * LINE + (e.foot.empty() ? 0 : 15) + 10;
}

int CommentView::total_h()
{
    int t = 0;
    for (auto &e : entries)
        t += entry_h(e);
    return t;
}

void CommentView::clear_all()
{
    entries.clear();
    scroll_y = 0;
    count.clear();
    can_more = false;
    redraw();
}

void CommentView::add(const std::vector<Comment> &list, bool front)
{
    std::vector<Entry> add;
    for (auto &c : list) {
        Entry e;
        e.c = c;
        add.push_back(e);
    }
    entries.insert(front ? entries.begin() : entries.end(), add.begin(), add.end());
    redraw();
}

void CommentView::check_more()
{
    int view = h() - HEAD;
    if (can_more && on_more && scroll_y + view > total_h() - 300)
        on_more();
}

void CommentView::draw()
{
    fl_push_clip(x(), y(), w(), h());
    fl_rectf(x(), y(), w(), h(), C_PANEL);
    fl_rectf(x(), y(), w(), HEAD, C_BAR);
    fl_font(FL_HELVETICA_BOLD, 12);
    fl_color(C_TEXT);
    std::string head = "Comentarios" + (count.empty() ? std::string() : " · " + count);
    fl_draw(fit(head, w() - 20).c_str(), x() + 10, y(), w() - 20, HEAD, FL_ALIGN_LEFT, nullptr, 0);
    fl_push_clip(x(), y() + HEAD, w() - 8, h() - HEAD);
    int cy = y() + HEAD + 6 - scroll_y;
    for (auto &e : entries) {
        int eh = entry_h(e);
        if (cy + eh > y() + HEAD && cy < y() + h()) {
            fl_font(FL_HELVETICA_BOLD, 11);
            fl_color(C_ACCENT);
            fl_draw(e.head.c_str(), x() + 10, cy + 12);
            int hw = (int)fl_width(e.head.c_str());
            fl_font(FL_HELVETICA, 10);
            fl_color(C_DIM);
            fl_draw(clean(e.c.when).c_str(), x() + 18 + hw, cy + 12);
            fl_font(FL_HELVETICA, 12);
            fl_color(C_TEXT);
            int ly = cy + 18;
            for (auto &l : e.lines) {
                fl_draw(l.c_str(), x() + 10, ly + 11);
                ly += LINE;
            }
            if (!e.foot.empty()) {
                fl_font(FL_HELVETICA, 10);
                fl_color(C_DIM);
                fl_draw(e.foot.c_str(), x() + 10, ly + 11);
            }
            fl_color(C_LINE);
            fl_xyline(x() + 8, cy + eh - 4, x() + w() - 16);
        }
        cy += eh;
    }
    fl_pop_clip();
    int th = total_h(), view = h() - HEAD;
    if (th > view) {
        int sh = std::max(20, view * view / th);
        int sy = y() + HEAD + (view - sh) * scroll_y / std::max(1, th - view);
        fl_rectf(x() + w() - 6, sy, 4, sh, C_BTN_HI);
    }
    if (!status.empty()) {
        fl_font(FL_HELVETICA, 12);
        fl_color(C_DIM);
        int sy = entries.empty() ? y() + HEAD + 10 : std::min(y() + h() - 24, cy + 4);
        fl_draw(status.c_str(), x() + 10, sy, w() - 20, 40, FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP, nullptr, 0);
    }
    fl_pop_clip();
}

int CommentView::handle(int e)
{
    if (e == FL_ENTER || e == FL_MOVE)
        return 1;
    if (e == FL_MOUSEWHEEL) {
        int max = std::max(0, total_h() - (h() - HEAD) + 30);
        scroll_y = std::max(0, std::min(max, scroll_y + Fl::event_dy() * LINE * 3));
        redraw();
        check_more();
        return 1;
    }
    return Fl_Widget::handle(e);
}

// --- RelatedView ----------------------------------------------------------------------------------------

void RelatedView::set(const std::vector<Item> &v, int next_index)
{
    items = v;
    next = next_index;
    scroll_y = 0;
    hover = -1;
    redraw();
}

void RelatedView::damage_thumb(const std::string &url)
{
    for (auto &it : items)
        if (it.thumb == url) {
            redraw();
            return;
        }
}

int RelatedView::row_at(int my)
{
    int r = (my - (y() + HEAD) + scroll_y) / ROW;
    return my > y() + HEAD && r >= 0 && r < (int)items.size() ? r : -1;
}

void RelatedView::draw()
{
    fl_push_clip(x(), y(), w(), h());
    fl_rectf(x(), y(), w(), h(), C_PANEL);
    fl_rectf(x(), y(), w(), HEAD, C_BAR);
    fl_font(FL_HELVETICA_BOLD, 12);
    fl_color(C_TEXT);
    fl_draw("A continuación", x() + 10, y(), w() - 20, HEAD, FL_ALIGN_LEFT, nullptr, 0);
    fl_push_clip(x(), y() + HEAD, w(), h() - HEAD);
    int ry = y() + HEAD - scroll_y;
    int tw = w() < 200 ? 0 : TW; // very narrow: text only
    for (int i = 0; i < (int)items.size(); i++, ry += ROW) {
        if (ry + ROW < y() + HEAD || ry > y() + h())
            continue;
        const Item &it = items[i];
        if (i == hover)
            fl_rectf(x(), ry, w(), ROW, C_BTN);
        if (i == next)
            fl_rectf(x(), ry, 3, ROW, C_ACCENT);
        int tx = x() + 8;
        if (tw) {
            fl_rectf(tx, ry + 5, tw, TH, C_BAR);
            Fl_RGB_Image *img = get_thumb(it.thumb, tw, TH, it.kind == "channel" || it.plat == P_MUSIC);
            if (img) {
                fl_push_clip(tx, ry + 5, tw, TH);
                img->draw(tx + (tw - img->w()) / 2, ry + 5 + (TH - img->h()) / 2);
                fl_pop_clip();
            }
            fl_font(FL_HELVETICA_BOLD, 9);
            std::string badge = it.live ? "EN VIVO" : fmt_duration(it.duration);
            if (!badge.empty()) {
                int bw = (int)fl_width(badge.c_str()) + 6;
                fl_rectf(tx + tw - bw - 2, ry + 5 + TH - 14, bw, 12, it.live ? C_RED : fl_rgb_color(10, 10, 12));
                fl_color(FL_WHITE);
                fl_draw(badge.c_str(), tx + tw - bw - 2, ry + 5 + TH - 14, bw, 12, FL_ALIGN_CENTER, nullptr, 0);
            }
            tx += tw + 8;
        }
        int lw = x() + w() - 8 - tx;
        fl_font(FL_HELVETICA_BOLD, 11);
        fl_color(C_TEXT);
        int ly = ry + 16;
        for (auto &l : wrap(clean(it.title), lw, 2)) {
            fl_draw(l.c_str(), tx, ly);
            ly += 13;
        }
        fl_font(FL_HELVETICA, 10);
        fl_color(i == next ? C_ACCENT : C_DIM);
        std::string sub = i == next ? "Siguiente · " + clean(it.author) : clean(it.author);
        fl_draw(fit(sub, lw).c_str(), tx, ry + ROW - 9);
    }
    fl_pop_clip();
    if (items.empty() && !status.empty()) {
        fl_font(FL_HELVETICA, 12);
        fl_color(C_DIM);
        fl_draw(status.c_str(), x() + 10, y() + HEAD + 10, w() - 20, 60, FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP,
                nullptr, 0);
    }
    fl_pop_clip();
}

int RelatedView::handle(int e)
{
    switch (e) {
    case FL_ENTER:
        return 1;
    case FL_MOVE: {
        int r = row_at(Fl::event_y());
        if (r != hover) {
            hover = r;
            fl_cursor(r >= 0 ? FL_CURSOR_HAND : FL_CURSOR_DEFAULT);
            redraw();
        }
        return 1;
    }
    case FL_LEAVE:
        hover = -1;
        fl_cursor(FL_CURSOR_DEFAULT);
        redraw();
        return 1;
    case FL_MOUSEWHEEL: {
        int max = std::max(0, (int)items.size() * ROW - (h() - HEAD) + 10);
        scroll_y = std::max(0, std::min(max, scroll_y + Fl::event_dy() * ROW));
        int r = row_at(Fl::event_y());
        hover = r;
        redraw();
        return 1;
    }
    case FL_PUSH: {
        int r = row_at(Fl::event_y());
        if (r >= 0 && on_open)
            on_open(r);
        return 1;
    }
    }
    return Fl_Widget::handle(e);
}

// --- LyricsView -----------------------------------------------------------------------------------------

void LyricsView::relayout()
{
    layout_w = w() - 24;
    wrapped.clear();
    owner.clear();
    fl_font(FL_HELVETICA_BOLD, 14); // the size of the highlighted line, so it fits too
    if (!lines.empty()) {
        for (int i = 0; i < (int)lines.size(); i++) {
            std::string t = clean(lines[i].second);
            for (auto &l : wrap(t.empty() ? "…" : t, layout_w, 6)) {
                wrapped.push_back(l);
                owner.push_back(i);
            }
        }
        return;
    }
    size_t i = 0;
    while (i <= plain.size()) {
        size_t nl = plain.find('\n', i);
        if (nl == std::string::npos)
            nl = plain.size();
        std::string para = clean(plain.substr(i, nl - i));
        if (para.empty())
            wrapped.push_back("");
        for (auto &l : wrap(para, layout_w, 1000))
            wrapped.push_back(l);
        while (owner.size() < wrapped.size())
            owner.push_back(-1);
        i = nl + 1;
    }
}

int LyricsView::first_row(int line)
{
    for (int r = 0; r < (int)owner.size(); r++)
        if (owner[r] == line)
            return r;
    return 0;
}

void LyricsView::set(const Lyrics &l)
{
    lines = l.lines;
    plain = l.lines.empty() ? l.plain : "";
    source = l.source;
    status = l.lines.empty() && l.plain.empty() ? "No se encontró la letra de esta canción." : "";
    cur = -1;
    scroll_y = 0;
    layout_w = -1;
    redraw();
}

void LyricsView::clear_all(const std::string &st)
{
    lines.clear();
    plain.clear();
    source.clear();
    status = st;
    cur = -1;
    scroll_y = 0;
    layout_w = -1;
    redraw();
}

void LyricsView::set_time(long long ms, double now)
{
    if (lines.empty())
        return;
    int c = -1;
    for (int i = 0; i < (int)lines.size() && lines[i].first <= ms; i++)
        c = i;
    if (c == cur)
        return;
    cur = c;
    if (now >= manual_until && layout_w == w() - 24) {
        int row = c < 0 ? 0 : first_row(c);
        scroll_y = std::max(0, row * LINE - (h() - HEAD) / 2 + LINE);
    }
    redraw();
}

void LyricsView::draw()
{
    if (layout_w != w() - 24) {
        relayout();
        if (cur >= 0)
            scroll_y = std::max(0, first_row(cur) * LINE - (h() - HEAD) / 2 + LINE);
    }
    fl_push_clip(x(), y(), w(), h());
    fl_rectf(x(), y(), w(), h(), C_PANEL);
    fl_rectf(x(), y(), w(), HEAD, C_BAR);
    fl_font(FL_HELVETICA_BOLD, 12);
    fl_color(C_TEXT);
    std::string head = std::string(lines.empty() && !plain.empty() ? "Letra (sin tiempos)" : "Letra") +
                       (source.empty() ? std::string() : " · " + source);
    fl_draw(fit(head, w() - 20).c_str(), x() + 10, y(), w() - 20, HEAD, FL_ALIGN_LEFT, nullptr, 0);
    fl_push_clip(x(), y() + HEAD, w(), h() - HEAD);
    int ry = y() + HEAD + 10 - scroll_y;
    for (int r = 0; r < (int)wrapped.size(); r++, ry += LINE) {
        if (ry + LINE < y() + HEAD || ry > y() + h())
            continue;
        bool now = owner[r] >= 0 && owner[r] == cur;
        bool past = owner[r] >= 0 && cur >= 0 && owner[r] < cur;
        fl_font(now ? FL_HELVETICA_BOLD : FL_HELVETICA, now ? 14 : 13);
        fl_color(lines.empty() ? C_TEXT : now ? C_ACCENT : past ? C_DIM : C_TEXT);
        fl_draw(wrapped[r].c_str(), x() + 12, ry, w() - 24, LINE, FL_ALIGN_LEFT, nullptr, 0);
    }
    fl_pop_clip();
    if (!status.empty()) {
        fl_font(FL_HELVETICA, 12);
        fl_color(C_DIM);
        fl_draw(status.c_str(), x() + 10, y() + HEAD + 10, w() - 20, 60, FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP,
                nullptr, 0);
    }
    fl_pop_clip();
}

int LyricsView::handle(int e)
{
    if (e == FL_ENTER || e == FL_MOVE)
        return 1;
    if (e == FL_MOUSEWHEEL) {
        int max = std::max(0, (int)wrapped.size() * LINE - (h() - HEAD) + 30);
        scroll_y = std::max(0, std::min(max, scroll_y + Fl::event_dy() * LINE * 3));
        manual_until = mono_now() + 6;
        redraw();
        return 1;
    }
    if (e == FL_PUSH && !lines.empty() && Fl::event_y() > y() + HEAD) {
        int r = (Fl::event_y() - (y() + HEAD + 10) + scroll_y) / LINE;
        if (r >= 0 && r < (int)owner.size() && owner[r] >= 0 && on_seek) {
            manual_until = 0;
            on_seek(lines[owner[r]].first / 1000.0);
        }
        return 1;
    }
    return Fl_Widget::handle(e);
}

// --- QueueView ------------------------------------------------------------------------------------------

void QueueView::show_current()
{
    int view = h() - HEAD;
    int top = current * ROW;
    if (top < scroll_y || top + ROW > scroll_y + view)
        scroll_y = std::max(0, top - view / 3);
    redraw();
}

int QueueView::row_at(int my)
{
    int r = (my - (y() + HEAD) + scroll_y) / ROW;
    return items && my > y() + HEAD && r >= 0 && r < (int)items->size() ? r : -1;
}

void QueueView::draw()
{
    fl_push_clip(x(), y(), w(), h());
    fl_rectf(x(), y(), w(), h(), C_PANEL);
    fl_rectf(x(), y(), w(), HEAD, C_BAR);
    fl_font(FL_HELVETICA_BOLD, 12);
    fl_color(C_TEXT);
    int n = items ? (int)items->size() : 0;
    std::string head = "Cola · " + std::to_string(n) + (n == 1 ? " canción" : " canciones") +
                       (mode.empty() ? "" : " · " + mode);
    fl_draw(fit(head, w() - 20).c_str(), x() + 10, y(), w() - 20, HEAD, FL_ALIGN_LEFT, nullptr, 0);
    fl_push_clip(x(), y() + HEAD, w(), h() - HEAD);
    int ry = y() + HEAD - scroll_y;
    for (int i = 0; i < n; i++, ry += ROW) {
        if (ry + ROW < y() + HEAD || ry > y() + h())
            continue;
        const Item &it = (*items)[i];
        if (i == current)
            fl_rectf(x(), ry, w(), ROW, C_SEL);
        else if (i == hover)
            fl_rectf(x(), ry, w(), ROW, C_BTN);
        fl_font(FL_HELVETICA_BOLD, 12);
        fl_color(i < current ? C_DIM : C_TEXT);
        std::string num = std::to_string(i + 1);
        fl_draw(num.c_str(), x() + 4, ry, 24, ROW, FL_ALIGN_RIGHT, nullptr, 0);
        std::string dur = it.duration > 0 ? fmt_duration(it.duration) : "";
        fl_font(FL_HELVETICA, 10);
        int dw = dur.empty() ? 0 : (int)fl_width(dur.c_str()) + 8;
        fl_font(FL_HELVETICA_BOLD, 12);
        fl_draw(fit(clean(it.title), w() - 44 - dw).c_str(), x() + 34, ry + 15);
        fl_font(FL_HELVETICA, 10);
        fl_color(i == current ? C_TEXT : C_DIM);
        fl_draw(fit(clean(it.author), w() - 44 - dw).c_str(), x() + 34, ry + 28);
        if (!dur.empty())
            fl_draw(dur.c_str(), x() + w() - dw - 6, ry + 15);
    }
    fl_pop_clip();
    if (!n) {
        fl_font(FL_HELVETICA, 12);
        fl_color(C_DIM);
        fl_draw("La cola está vacía. Elige una canción, o clic derecho en una tarjeta → «Agregar a la cola».",
                x() + 10, y() + HEAD + 10, w() - 20, 60, FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP, nullptr, 0);
    }
    fl_pop_clip();
}

int QueueView::handle(int e)
{
    switch (e) {
    case FL_ENTER:
        return 1;
    case FL_MOVE: {
        int r = row_at(Fl::event_y());
        if (r != hover) {
            hover = r;
            redraw();
        }
        return 1;
    }
    case FL_LEAVE:
        hover = -1;
        redraw();
        return 1;
    case FL_MOUSEWHEEL: {
        int n = items ? (int)items->size() : 0;
        int max = std::max(0, n * ROW - (h() - HEAD) + 10);
        scroll_y = std::max(0, std::min(max, scroll_y + Fl::event_dy() * ROW * 2));
        redraw();
        return 1;
    }
    case FL_PUSH: {
        int r = row_at(Fl::event_y());
        if (r < 0)
            return 1;
        if (Fl::event_button() == FL_RIGHT_MOUSE) {
            Fl_Menu_Item m[] = {{"Reproducir"}, {"Subir"}, {"Bajar"}, {"Quitar de la cola", 0, 0, 0, FL_MENU_DIVIDER},
                                {"Vaciar la cola"}, {nullptr}};
            const Fl_Menu_Item *pick = m->popup(Fl::event_x(), Fl::event_y());
            if (pick && on_menu)
                on_menu(r, (int)(pick - m));
        } else if (Fl::event_clicks() > 0 && on_play) {
            Fl::event_clicks(0);
            on_play(r);
        }
        return 1;
    }
    }
    return Fl_Widget::handle(e);
}

// Definiciones requeridas por C++11 para las constantes estáticas de Grid
const int Grid::CARD_MIN;
const int Grid::CARD_MAX;
const int Grid::GAP;
const int Grid::CHIPS_H;
