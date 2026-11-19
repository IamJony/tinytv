#include "ui.h"

#include <FL/Fl_GIF_Image.H>
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <list>
#include <map>
#include <unistd.h>

Fl_Color C_BG, C_BAR, C_SIDE, C_PANEL, C_BTN, C_BTN_HI, C_SEL, C_TEXT, C_DIM, C_ACCENT, C_RED, C_GREEN, C_LINE;
Fl_Color plat_color[P_COUNT_MAX];

void init_colors()
{ 
    plat_color[P_LatinoTV] = fl_rgb_color(205, 32, 31);
    C_BG = fl_rgb_color(38, 40, 46);
    C_BAR = fl_rgb_color(28, 30, 34);
    C_SIDE = fl_rgb_color(33, 35, 40);
    C_PANEL = fl_rgb_color(46, 49, 56);
    C_BTN = fl_rgb_color(58, 62, 70);
    C_BTN_HI = fl_rgb_color(74, 79, 90);
    C_SEL = fl_rgb_color(70, 110, 160);
    C_TEXT = fl_rgb_color(225, 228, 234);
    C_DIM = fl_rgb_color(150, 156, 168);
    C_ACCENT = fl_rgb_color(240, 190, 80);
    C_RED = fl_rgb_color(205, 32, 31);
    C_GREEN = fl_rgb_color(60, 150, 100);
    C_LINE = fl_rgb_color(30, 32, 36);
}

// --- text ------------------------------------------------------------------------------------------

std::string clean(const std::string &s)
{
    std::string r;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (n == 3 && i + 2 < s.size()) {
            unsigned cp = ((c & 0x0f) << 12) | (((unsigned char)s[i + 1] & 0x3f) << 6) | ((unsigned char)s[i + 2] & 0x3f);
            bool keep = (cp >= 0x2013 && cp <= 0x2014) || (cp >= 0x2018 && cp <= 0x201e) || cp == 0x2022 ||
                        cp == 0x2026 || cp == 0x20ac || cp < 0x2000 || (cp >= 0x3000 && cp < 0xe000);
            if (!keep || cp == 0xfe0f || cp == 0x200d) {
                i += n;
                continue;
            }
        }
        if (n == 4) {
            i += n;
            continue;
        }
        if (c == '\t')
            r += ' ';
        else
            r.append(s, i, n);
        i += n;
    }
    size_t a = r.find_first_not_of(' ');
    return a == std::string::npos ? "" : r.substr(a);
}

std::string fit(const std::string &s, int w)
{
    if (fl_width(s.c_str()) <= w)
        return s;
    std::string r = s;
    while (!r.empty() && fl_width((r + "…").c_str()) > w) {
        r.pop_back();
        while (!r.empty() && ((unsigned char)r.back() & 0xc0) == 0x80)
            r.pop_back();
    }
    return r + "…";
}

std::vector<std::string> wrap(const std::string &s, int w, int maxlines)
{
    std::vector<std::string> words;
    size_t i = 0;
    while (i < s.size()) {
        size_t sp = s.find(' ', i);
        if (sp == std::string::npos)
            sp = s.size();
        if (sp > i)
            words.push_back(s.substr(i, sp - i));
        i = sp + 1;
    }
    std::vector<std::string> lines;
    std::string cur;
    for (size_t k = 0; k < words.size(); k++) {
        std::string test = cur.empty() ? words[k] : cur + " " + words[k];
        if (cur.empty() || fl_width(test.c_str()) <= w) {
            cur = test;
            continue;
        }
        lines.push_back(cur);
        if ((int)lines.size() == maxlines - 1) {
            cur.clear();
            for (size_t r = k; r < words.size(); r++)
                cur += (cur.empty() ? "" : " ") + words[r];
            break;
        }
        cur = words[k];
    }
    if (!cur.empty())
        lines.push_back(cur);
    if ((int)lines.size() > maxlines)
        lines.resize(maxlines);
    for (auto &l : lines)
        l = fit(l, w);
    return lines;
}

double mono_now()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

std::string menu_label(std::string l)
{
    for (size_t p = 0; (p = l.find('&', p)) != std::string::npos; p += 2)
        l.insert(p, 1, '&');
    return l;
}

// --- images ----------------------------------------------------------------------------------------

typedef unsigned char *(*webp_decode_fn)(const unsigned char *, size_t, int *, int *);
typedef void (*webp_free_fn)(void *);

// libwebp is optional: loaded when present (Twitch and Kick send some pictures as WebP).
static Fl_RGB_Image *decode_webp(const std::string &data)
{
    static webp_decode_fn dec;
    static webp_free_fn wfree;
    static bool tried;
    if (!tried) {
        tried = true;
        void *h = dlopen("libwebp.so.7", RTLD_LAZY);
        if (!h)
            h = dlopen("libwebp.so", RTLD_LAZY);
        if (h) {
            dec = (webp_decode_fn)dlsym(h, "WebPDecodeRGB");
            wfree = (webp_free_fn)dlsym(h, "WebPFree");
        }
    }
    if (!dec)
        return nullptr;
    int w = 0, h = 0;
    unsigned char *px = dec((const unsigned char *)data.data(), data.size(), &w, &h);
    if (!px)
        return nullptr;
    unsigned char *copy = new unsigned char[(size_t)w * h * 3];
    memcpy(copy, px, (size_t)w * h * 3);
    if (wfree)
        wfree(px);
    else
        free(px);
    Fl_RGB_Image *img = new Fl_RGB_Image(copy, w, h, 3);
    img->alloc_array = 1;
    return img;
}

// Fl_GIF_Image only reads files (FLTK 1.3).
static Fl_RGB_Image *decode_gif(const std::string &data)
{
    char path[] = "/tmp/tinytv-gif-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return nullptr;
    bool ok = write(fd, data.data(), data.size()) == (ssize_t)data.size();
    close(fd);
    Fl_RGB_Image *img = nullptr;
    if (ok) {
        Fl_GIF_Image gif(path);
        if (gif.w() > 0 && gif.h() > 0)
            img = new Fl_RGB_Image(&gif, C_PANEL);
    }
    unlink(path);
    return img;
}

Fl_RGB_Image *decode_raw(const std::string &data)
{
    Fl_RGB_Image *src = nullptr;
    const unsigned char *d = (const unsigned char *)data.data();
    if (data.size() > 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4))
        src = decode_webp(data);
    else if (data.size() > 6 && !memcmp(d, "GIF8", 4))
        src = decode_gif(data);
    else if (data.size() > 4 && d[0] == 0xff && d[1] == 0xd8)
        src = new Fl_JPEG_Image(nullptr, d);
    else if (data.size() > 8 && d[0] == 0x89 && d[1] == 'P')
        src = new Fl_PNG_Image(nullptr, d, (int)data.size());
    if (src && (src->w() <= 0 || src->h() <= 0)) {
        delete src;
        src = nullptr;
    }
    return src;
}

// Scaled to cover w×h (cropped when drawn); square: to cover h×h (avatars, covers).
static Fl_RGB_Image *scaled(const std::string &data, int w, int h, bool square)
{
    Fl_RGB_Image *src = decode_raw(data);
    if (!src)
        return nullptr;
    int bw = square ? h : w;
    double r = std::max((double)bw / src->w(), (double)h / src->h());
    Fl_RGB_Image *out = (Fl_RGB_Image *)src->copy(std::max(1, (int)(src->w() * r)), std::max(1, (int)(src->h() * r)));
    delete src;
    return out;
}

struct Thumb {
    std::string data;
    bool pending = false;
    std::list<std::pair<int, Fl_RGB_Image *>> sizes; // most recent first
};

static std::map<std::string, Thumb> thumbs;
static std::list<std::string> thumb_order;
std::function<void(const std::string &)> thumbs_changed;

static void forget(Thumb &t)
{
    for (auto &s : t.sizes)
        delete s.second;
    t.sizes.clear();
}

Fl_RGB_Image *get_thumb(const std::string &url, int w, int h, bool square)
{
    if (url.empty())
        return nullptr;
    auto it = thumbs.find(url);
    if (it == thumbs.end()) {
        thumbs[url].pending = true;
        thumb_order.push_back(url);
        while (thumb_order.size() > 300) {
            auto old = thumbs.find(thumb_order.front());
            if (old != thumbs.end() && !old->second.pending) {
                forget(old->second);
                thumbs.erase(old);
            }
            thumb_order.pop_front();
        }
        thumbs_fetch(url, [url](std::string data) {
            auto t = thumbs.find(url);
            if (t == thumbs.end())
                return;
            t->second.pending = false;
            t->second.data = data;
            if (thumbs_changed)
                thumbs_changed(url);
        });
        return nullptr;
    }
    Thumb &t = it->second;
    if (t.data.empty())
        return nullptr;
    int key = (w * 4096 + h) * 2 + square;
    for (auto s = t.sizes.begin(); s != t.sizes.end(); ++s)
        if (s->first == key) {
            if (s != t.sizes.begin())
                t.sizes.splice(t.sizes.begin(), t.sizes, s);
            return t.sizes.front().second;
        }
    Fl_RGB_Image *img = scaled(t.data, w, h, square);
    if (!img) {
        t.data.clear(); // not an image: not decoded again on each redraw
        return nullptr;
    }
    t.sizes.push_front({key, img});
    while (t.sizes.size() > 2) {
        delete t.sizes.back().second;
        t.sizes.pop_back();
    }
    return img;
}

// --- icons ------------------------------------------------------------------------------------------

void draw_icon(Icon icon, int x, int y, int s, Fl_Color c)
{
    fl_color(c);
    int cx = x + s / 2, cy = y + s / 2;
    switch (icon) {
    case I_HOME:
        fl_polygon(x + 1, cy, cx, y + 1, x + s - 1, cy);
        fl_rectf(x + s / 5, cy, s - 2 * (s / 5), s / 2 - 1);
        fl_color(C_SIDE);
        fl_rectf(cx - s / 8, y + s - s / 3, s / 4, s / 3 - 1);
        break;
    case I_NEW: // a lightning bolt
        fl_polygon(cx + 2, y, x + 2, cy + 2, cx, cy + 2);
        fl_polygon(cx, cy - 2, x + s - 2, cy - 2, cx - 2, y + s);
        break;
    case I_FOLLOW: // a person
        fl_pie(cx - s / 5, y + 1, 2 * (s / 5), 2 * (s / 5), 0, 360);
        fl_pie(x + 2, cy + 1, s - 4, s, 0, 180);
        break;
    case I_HISTORY: // a clock
        fl_line_style(FL_SOLID, 2);
        fl_arc(x + 1, y + 1, s - 2, s - 2, 0, 360);
        fl_line(cx, y + s / 4, cx, cy, cx + s / 4, cy);
        fl_line_style(0);
        break;
    case I_SETTINGS: // sliders
        fl_line_style(FL_SOLID, 2);
        for (int i = 0; i < 3; i++) {
            int ly = y + 2 + i * (s - 4) / 2;
            fl_line(x + 1, ly, x + s - 1, ly);
            int kx = x + (i == 1 ? s - s / 3 : s / 3);
            fl_rectf(kx - 2, ly - 3, 5, 7);
        }
        fl_line_style(0);
        break;
    case I_EXIT:
        fl_line_style(FL_SOLID, 2);
        fl_line(x + s / 2, y + 1, x + 1, y + 1, x + 1, y + s - 1);
        fl_line(x + 1, y + s - 1, x + s / 2, y + s - 1);
        fl_line(x + s / 3, cy, x + s - 2, cy);
        fl_line_style(0);
        fl_polygon(x + s - 1, cy, x + s - 6, cy - 5, x + s - 6, cy + 5);
        break;
    case I_NEXT:
        fl_polygon(x + 2, y + 2, x + s - 5, cy, x + 2, y + s - 2);
        fl_rectf(x + s - 5, y + 2, 3, s - 4);
        break;
    case I_PREV:
        fl_polygon(x + s - 2, y + 2, x + 5, cy, x + s - 2, y + s - 2);
        fl_rectf(x + 2, y + 2, 3, s - 4);
        break;
    case I_LOOP: // a circle of arrows: plays the next one on its own
        fl_line_style(FL_SOLID, 2);
        fl_arc(x + 2, y + 2, s - 4, s - 4, 40, 320);
        fl_line_style(0);
        fl_polygon(x + s - 1, y + 3, x + s - 1, y + s / 2, x + s / 2, y + s / 2 - 2);
        break;
    case I_DOWNLOAD: // an arrow down into a tray
        fl_line_style(FL_SOLID, 2);
        fl_line(cx, y + 1, cx, y + s * 2 / 3 - 1);
        fl_line(x + 1, y + s - 5, x + 1, y + s - 2, x + s - 1, y + s - 2);
        fl_line(x + s - 1, y + s - 2, x + s - 1, y + s - 5);
        fl_line_style(0);
        fl_polygon(cx - s / 3, y + s / 3, cx + s / 3, y + s / 3, cx, y + s * 2 / 3 + 1);
        break;
    case I_STORE: {
        fl_rectf(x + 2, y + s/3, s - 4, s - s/3, c);
        fl_line_style(FL_SOLID, 2);
        fl_arc(x + s/4, y + 1, s/2, s/2, 0, 180);
        fl_line_style(0);
        break;
    }
    case I_NONE:
        break;
    }
}

// --- widgets -----------------------------------------------------------------------------------------

FlatButton::FlatButton(int x, int y, int w, int h, const char *l) : Fl_Button(x, y, w, h, l)
{
    bg = C_BTN;
    hi = C_BTN_HI;
    fg = C_TEXT;
    sel_color = C_SEL;
    box(FL_FLAT_BOX);
    clear_visible_focus();
}

void FlatButton::draw()
{
    Fl_Color c = selected ? sel_color : (value() ? C_SEL : hover ? hi : bg);
    fl_rectf(x(), y(), w(), h(), c);
    Fl_Color tc = active_r() ? fg : C_DIM;
    int lx = x() + 6 + align_left;
    if (icon != I_NONE) {
        int is = std::min(16, h() - 10);
        int ix = compact || !label() ? x() + (w() - is) / 2 : lx;
        draw_icon(icon, ix, y() + (h() - is) / 2, is, tc);
        lx = ix + is + 8;
        if (compact)
            return;
    }
    if (!label())
        return;
    fl_font(FL_HELVETICA_BOLD, font_size);
    fl_color(tc);
    if (symbol) {
        fl_draw_symbol(label(), x() + w() / 2 - 7, y() + h() / 2 - 7, 14, 14, tc);
        return;
    }
    int lw = x() + w() - 6 - lx;
    bool left = align_left || icon != I_NONE;
    fl_draw(fit(label(), lw).c_str(), lx, y(), lw, h(), left ? FL_ALIGN_LEFT : FL_ALIGN_CENTER, nullptr, 0);
}

int FlatButton::handle(int e)
{
    if (e == FL_ENTER || e == FL_LEAVE) {
        hover = e == FL_ENTER;
        redraw();
    }
    return Fl_Button::handle(e);
}

void Label::set(const std::string &t)
{
    if (t == text)
        return;
    text = t;
    if (parent() && visible_r())
        parent()->damage(FL_DAMAGE_ALL, x(), y(), w(), h());
    else
        redraw();
}

void Label::draw()
{
    fl_push_clip(x(), y(), w(), h());
    fl_font(font, size);
    fl_color(fg);
    if (al & FL_ALIGN_WRAP)
        fl_draw(text.c_str(), x(), y(), w(), h(), al, nullptr, 0);
    else
        fl_draw(fit(text, w()).c_str(), x(), y(), w(), h(), al, nullptr, 0);
    fl_pop_clip();
}

DarkScroll::DarkScroll(int x, int y, int w, int h) : Fl_Scroll(x, y, w, h)
{
    box(FL_FLAT_BOX);
    color(C_BG);
    type(Fl_Scroll::VERTICAL);
    style_scrollbar(&scrollbar);
    scrollbar_size(12);
}

void style_scrollbar(Fl_Scrollbar *s)
{
    s->color(C_BAR, C_BTN_HI);
    s->slider(FL_FLAT_BOX);
}

DarkText::DarkText(int x, int y, int w, int h) : Fl_Text_Display(x, y, w, h)
{
    style_scrollbar(mVScrollBar);
    style_scrollbar(mHScrollBar);
    scrollbar_width(12);
}

void Seek::draw()
{
    fl_rectf(x(), y(), w(), h(), C_BAR);
    int ty = y() + h() / 2 - 2;
    fl_rectf(x() + 4, ty, w() - 8, 4, C_BTN);
    if (max_ > 0) {
        int fw = (int)((w() - 8) * std::min(1.0, value_ / max_));
        fl_rectf(x() + 4, ty, fw, 4, C_ACCENT);
        fl_rectf(x() + 4 + fw - 3, y() + 1, 6, h() - 2, C_TEXT);
    }
}

int Seek::handle(int e)
{
    if ((e == FL_PUSH || e == FL_DRAG) && max_ > 0) {
        double v = (Fl::event_x() - x() - 4) / (double)(w() - 8) * max_;
        value_ = std::max(0.0, std::min(max_, v));
        redraw();
        return 1;
    }
    if (e == FL_RELEASE && max_ > 0) {
        if (on_seek)
            on_seek(value_);
        return 1;
    }
    return Fl_Widget::handle(e);
}

FlatChoice::FlatChoice(int x, int y, int w, int h) : Fl_Choice(x, y, w, h)
{
    box(FL_FLAT_BOX);
    down_box(FL_FLAT_BOX);
    color(C_BTN);
    textcolor(C_TEXT);
    textfont(FL_HELVETICA_BOLD);
    textsize(12);
    labelcolor(C_TEXT);
    selection_color(C_SEL);
    clear_visible_focus();
}

void FlatChoice::draw()
{
    fl_rectf(x(), y(), w(), h(), C_BTN);
    int aw = h() - 8;
    fl_color(C_ACCENT);
    int ax = x() + w() - aw - 2, ay = y() + h() / 2;
    fl_polygon(ax + 3, ay - 3, ax + aw - 5, ay - 3, ax + aw / 2 - 1, ay + 3);
    if (mvalue()) {
        fl_font(FL_HELVETICA_BOLD, 12);
        fl_color(active_r() ? C_TEXT : C_DIM);
        fl_draw(fit(mvalue()->label(), w() - aw - 14).c_str(), x() + 8, y(), w() - aw - 14, h(), FL_ALIGN_LEFT,
                nullptr, 0);
    }
}

void style_input(Fl_Input *in)
{
    in->box(FL_FLAT_BOX);
    in->color(fl_rgb_color(52, 56, 64));
    in->textcolor(C_TEXT);
    in->textsize(12);
    in->cursor_color(C_ACCENT);
    in->selection_color(C_SEL);
}
