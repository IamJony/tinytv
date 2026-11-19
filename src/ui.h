#ifndef SANSON_UI_H
#define SANSON_UI_H

// Colors, text helpers, images and the small flat widgets every screen uses.

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Text_Display.H>
#include <FL/fl_draw.H>
#include <functional>
#include <string>
#include <vector>

#include "backend.h"

extern Fl_Color C_BG, C_BAR, C_SIDE, C_PANEL, C_BTN, C_BTN_HI, C_SEL, C_TEXT, C_DIM, C_ACCENT, C_RED, C_GREEN, C_LINE;
extern Fl_Color plat_color[P_COUNT_MAX];
void init_colors();

// Text without what the core fonts cannot draw (emoji, joiners), and tabs as spaces.
std::string clean(const std::string &s);
// `s` cut with "…" to `w` pixels in the current font.
std::string fit(const std::string &s, int w);
std::vector<std::string> wrap(const std::string &s, int w, int maxlines);
double mono_now();

// Images: decoded once from the downloaded bytes, scaled copies kept per size, so resizing
// the window never downloads them again.
Fl_RGB_Image *decode_raw(const std::string &data);
Fl_RGB_Image *get_thumb(const std::string &url, int w, int h, bool square);
extern std::function<void(const std::string &)> thumbs_changed;

// Vector icons for the side bar and the player.
enum Icon { I_NONE, I_HOME, I_NEW, I_FOLLOW, I_HISTORY, I_SETTINGS, I_EXIT, I_NEXT, I_PREV, I_LOOP, I_DOWNLOAD, I_STORE };
void draw_icon(Icon icon, int x, int y, int size, Fl_Color c);

class FlatButton : public Fl_Button {
public:
    Fl_Color bg, hi, fg, sel_color;
    bool selected = false, hover = false, symbol = false;
    int font_size = 12;
    int align_left = 0;
    Icon icon = I_NONE;
    bool compact = false; // only the icon (narrow side bar)

    FlatButton(int x, int y, int w, int h, const char *l = nullptr);
    void draw() override;
    int handle(int e) override;
    void set_selected(bool s)
    {
        if (s != selected) {
            selected = s;
            redraw();
        }
    }
};

class Bar : public Fl_Group {
public:
    Bar(int x, int y, int w, int h, Fl_Color c) : Fl_Group(x, y, w, h) { color(c); }
    void draw() override
    {
        if (damage() & ~FL_DAMAGE_CHILD)
            fl_rectf(x(), y(), w(), h(), color());
        draw_children();
    }
};

class Label : public Fl_Widget {
public:
    std::string text;
    int size = 13;
    Fl_Font font = FL_HELVETICA_BOLD;
    Fl_Color fg;
    Fl_Align al = FL_ALIGN_LEFT;
    Label(int x, int y, int w, int h) : Fl_Widget(x, y, w, h), fg(C_TEXT) {}
    void set(const std::string &t);
    void draw() override;
};

class DarkScroll : public Fl_Scroll {
public:
    DarkScroll(int x, int y, int w, int h);
};

// The scrollbars of a list (DarkScroll) on a text box: FLTK's default ones are grey with arrows.
void style_scrollbar(Fl_Scrollbar *s);
class DarkText : public Fl_Text_Display {
public:
    DarkText(int x, int y, int w, int h);
};

class Seek : public Fl_Widget {
public:
    double value_ = 0, max_ = 0;
    std::function<void(double)> on_seek;
    Seek(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void draw() override;
    int handle(int e) override;
};

class FlatChoice : public Fl_Choice {
public:
    FlatChoice(int x, int y, int w, int h);
    void draw() override;
};

void style_input(Fl_Input *in);
// Menu labels show '&' as an underline and '/' opens a submenu: both are escaped.
std::string menu_label(std::string l);

// The cursor comes only with a click (or Fl::focus, the / key): FLTK gives the focus back to the
// last widget that had it when the window gets it again, and the player keys ended up here.
// Fl::event() is no help: fl_fix_focus leaves there the last event, often a click elsewhere.
class ClickInput : public Fl_Input {
    bool clicked = false;
public:
    ClickInput(int X, int Y, int W, int H) : Fl_Input(X, Y, W, H) {}
    int handle(int e) override
    {
        if (e == FL_FOCUS && !clicked)
            return 0;
        if (e != FL_PUSH)
            return Fl_Input::handle(e);
        clicked = true;
        int r = Fl_Input::handle(e);
        clicked = false;
        return r;
    }
};

#endif
