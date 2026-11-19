#include "app.h"
#include "store.h"
#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/Fl_RGB_Image.H>
#include <cctype>

static void store_anim_tick(void *v);

class StoreView : public Fl_Group {
public:
    int scroll_y = 0;
    int hover_row = -1;
    int hover_btn = -1;
    int anim_offset = 0;
    bool dragging_scroll = false;
    int drag_offset_y = 0;

    int content_total_h() const {
        return (app && !app->store_items.empty())
            ? ((int)app->store_items.size() * (110 + 15) + 15)
            : 0;
    }

    bool scrollbar_geom(int &bx, int &by, int &bw, int &bh, int &max_scroll) const {
        int total_h = content_total_h();
        if (total_h <= h() || h() <= 0) {
            max_scroll = 0;
            return false;
        }
        max_scroll = total_h - h();
        bw = 8;
        bx = x() + w() - 12;
        bh = std::max(30, h() * h() / total_h);
        if (bh > h()) bh = h();
        int track = std::max(1, h() - bh);
        int sy = std::max(0, std::min(scroll_y, max_scroll));
        by = y() + (int)((long long)track * sy / max_scroll);
        return true;
    }

    StoreView(int X, int Y, int W, int H) : Fl_Group(X, Y, W, H) {
        box(FL_FLAT_BOX);
        color(C_BG);
        end();
    }

    void draw() override {
        fl_rectf(x(), y(), w(), h(), C_BG);
        if (!app) { return; }
        fl_push_clip(x(), y(), w(), h());

        if (!app || app->store_gen == 1) {
            // Animación de carga
            std::string txt = "Buscando plugins";
            for (int i = 0; i < anim_offset % 4; i++) txt += ".";
            fl_font(FL_HELVETICA_BOLD, 14);
            fl_color(C_ACCENT);
            fl_draw(txt.c_str(), x() + 20, y() + 20, w() - 40, 30, FL_ALIGN_LEFT, nullptr, 0);
            
            // Barra de progreso simple
            int bar_y = y() + 55;
            int bar_w = w() - 40;
            fl_color(C_BTN);
            fl_rectf(x() + 20, bar_y, bar_w, 4);
            fl_color(C_ACCENT);
            int fill = (anim_offset % 20) * (bar_w / 20);
            fl_rectf(x() + 20, bar_y, fill, 4);
            
            fl_pop_clip();
            return;
        }

        int pad = 15;
        int row_h = 110;
        int y_pos = y() + pad - scroll_y;
        int row_w = w() - (pad * 2) - 20; 

        for (int i = 0; i < (int)app->store_items.size(); i++) {
            auto &p = app->store_items[i];
            if (y_pos + row_h < y()) { y_pos += row_h + pad; continue; }
            if (y_pos > y() + h()) break;

            fl_rectf(x() + pad, y_pos, row_w, row_h, C_PANEL);

            int icon_x = x() + pad + 15;
            int icon_y = y_pos + 15;
            
            Fl_RGB_Image *img = get_thumb(p.icon_url, 48, 48, true);
            if (img) {
                img->draw(icon_x, icon_y, 48, 48);
            } else {
                fl_color(C_BTN);
                fl_rectf(icon_x, icon_y, 48, 48);
                fl_font(FL_HELVETICA_BOLD, 28);
                fl_color(fl_rgb_color(255, 255, 255));
                std::string initial = p.name.empty() ? "?" : std::string(1, toupper(p.name[0]));
                fl_draw(initial.c_str(), icon_x, icon_y, 48, 48, FL_ALIGN_CENTER);
            }

            int text_x = icon_x + 48 + 15;
            int text_w = row_w - (text_x - x() - pad) - 250;

            fl_font(FL_HELVETICA_BOLD, 16);
            fl_color(C_TEXT);
            fl_draw(fit(p.name, text_w).c_str(), text_x, y_pos + 15, text_w, 20, FL_ALIGN_LEFT, nullptr, 0);

            fl_font(FL_HELVETICA, 12);
            fl_color(C_DIM);
            std::string meta = "v" + p.version + " · " + p.author;
            fl_draw(fit(meta, text_w).c_str(), text_x, y_pos + 37, text_w, 16, FL_ALIGN_LEFT, nullptr, 0);

            fl_font(FL_HELVETICA, 13);
            fl_color(C_DIM);
            fl_draw(fit(p.description, text_w).c_str(), text_x, y_pos + 60, text_w, 40, FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP, nullptr, 0);

            int btn_y = y_pos + (row_h - 36) / 2;
            int btn_w = 100;
            int del_w = 90;
            int gap = 10;
            int btn_x = x() + pad + row_w - 20 - btn_w;
            
            bool inst = p.installed;
            

            Fl_Color b_col = (hover_row == i && hover_btn == 0) ? C_BTN_HI : C_BTN;
            if (inst) b_col = C_GREEN;

            fl_rectf(btn_x, btn_y, btn_w, 36, b_col);
            fl_font(FL_HELVETICA_BOLD, 12);
            fl_color(C_TEXT);
            std::string lbl = inst ? "Instalado" : "Instalar";
            fl_draw(lbl.c_str(), btn_x, btn_y, btn_w, 36, FL_ALIGN_CENTER, nullptr, 0);

            if (inst) {
                int del_x = btn_x - del_w - gap;
                Fl_Color d_col = (hover_row == i && hover_btn == 1) ? C_RED : C_BTN;
                fl_rectf(del_x, btn_y, del_w, 36, d_col);
                fl_color(C_TEXT);
                fl_draw("Borrar", del_x, btn_y, del_w, 36, FL_ALIGN_CENTER, nullptr, 0);
            }

            y_pos += row_h + pad;
        }

        (void)row_h;
        (void)pad;
        int bx, by, bw, bh, max_s;
        if (scrollbar_geom(bx, by, bw, bh, max_s)) {
            fl_rectf(bx, by, bw, bh, dragging_scroll ? C_ACCENT : C_BTN_HI);
        }

        fl_pop_clip();
    }

    int handle(int e) override {
        if (e == FL_MOUSEWHEEL) {
            int max_y = std::max(0, content_total_h() - h());
            scroll_y += Fl::event_dy() * 30;
            if (scroll_y < 0) scroll_y = 0;
            if (scroll_y > max_y) scroll_y = max_y;
            redraw();
            return 1;
        }

        if (e == FL_DRAG && dragging_scroll) {
            int bx, by, bw, bh, max_s;
            if (scrollbar_geom(bx, by, bw, bh, max_s) && max_s > 0) {
                int track = std::max(1, h() - bh);
                int new_by = Fl::event_y() - drag_offset_y - y();
                new_by = std::max(0, std::min(track, new_by));
                scroll_y = (int)((long long)new_by * max_s / track);
                redraw();
            }
            return 1;
        }

        if (e == FL_RELEASE && dragging_scroll) {
            dragging_scroll = false;
            redraw();
            return 1;
        }

        if (e == FL_MOVE) {
            int pad = 15, row_h = 110;
            int my = Fl::event_y() - y() + scroll_y - pad;
            int row = my / (row_h + pad);
            int mx = Fl::event_x() - x();

            int h_btn = -1;
            if (row >= 0 && row < (int)app->store_items.size()) {
                int row_w = w() - (pad * 2) - 20;
                int btn_x = pad + row_w - 20 - 100;
                int del_x = btn_x - 90 - 10;
                int btn_y = (row * (row_h + pad)) + pad + (row_h - 36) / 2;
                int real_my = Fl::event_y() - y() + scroll_y;

                if (mx >= btn_x && mx <= btn_x + 100 && real_my >= btn_y && real_my <= btn_y + 36)
                    h_btn = 0;
                else if (app->store_items[row].installed && mx >= del_x && mx <= del_x + 90 && real_my >= btn_y && real_my <= btn_y + 36)
                    h_btn = 1;
            }

            if (row != hover_row || h_btn != hover_btn) {
                hover_row = row;
                hover_btn = h_btn;
                redraw();
            }
            return 1;
        }

        if (e == FL_PUSH) {
            int bx, by, bw, bh, max_s;
            int mx = Fl::event_x(), my = Fl::event_y();
            if (scrollbar_geom(bx, by, bw, bh, max_s) && max_s > 0 &&
                mx >= bx - 4 && mx <= bx + bw + 4 && my >= y() && my <= y() + h()) {
                if (my >= by && my <= by + bh) {
                    dragging_scroll = true;
                    drag_offset_y = my - by;
                } else {
                    int track = std::max(1, h() - bh);
                    int target_by = std::max(0, std::min(track, my - y() - bh / 2));
                    scroll_y = (int)((long long)target_by * max_s / track);
                    dragging_scroll = true;
                    drag_offset_y = bh / 2;
                }
                redraw();
                return 1;
            }
            if (hover_row >= 0 && hover_row < (int)app->store_items.size()) {
                StorePlugin p = app->store_items[hover_row];
                if (hover_btn == 0) {
                    if (!p.installed) {
                        app->set_status("Descargando " + p.name + "...");
                        store_install(p, [p](bool ok, std::string err) {
                            for (auto &sp : app->store_items)
                                if (sp.id == p.id) { sp.installed = ok; sp.local_version = p.version; break; }
                            if (app->view != V_STORE) return;
                            app->show_store();
                            if (ok) app->set_status("Instalado: " + p.name + ". Reinicia TinyTV.");
                            else app->set_status("Error: " + err);
                        });
                    }
                } else if (hover_btn == 1) {
                    std::string id = p.id;
                    store_uninstall(id, [id](bool ok, std::string err) {
                        for (auto &sp : app->store_items)
                            if (sp.id == id) { sp.installed = false; break; }
                            if (app->view != V_STORE) return;
                        app->show_store();
                        if (!ok) app->set_status("Error: " + err);
                        else app->set_status("Desinstalado.");
                    });
                }
            }
            return 1;
        }
        return Fl_Group::handle(e);
    }
};

static StoreView *store_view = nullptr;


static void store_anim_tick(void *v)
{
    StoreView *sv = (StoreView *)v;
    if (!sv || !app || app->store_gen != 1) return;
    sv->anim_offset++;
    sv->redraw();
    Fl::repeat_timeout(0.1, store_anim_tick, sv);
}

void App::show_store() {
    if (!ready) return;
    view = V_STORE;

    if (watch) watch->hide();
    if (settings) settings->hide();
    if (scroll) scroll->hide();

    if (!store_view) {
        win->begin();
        store_view = new StoreView(118, 70, win->w() - 118, win->h() - 90);
        win->end();
    }

    store_view->resize(118, 70, win->w() - 118, win->h() - 90);
    store_view->show();
    store_view->redraw();

    if (back_btn) back_btn->hide();
    if (sub_btn) sub_btn->hide();

    select_nav(NAV_STORE);
    set_title("Tienda de plugins");
    layout();

    if (store_items.empty() && store_gen == 0) {
        store_gen = 1;
        if (store_view) {
            store_view->anim_offset = 0;
            Fl::remove_timeout(store_anim_tick);
            Fl::add_timeout(0.1, store_anim_tick, store_view);
        }
        store_fetch([](std::vector<StorePlugin> items, std::string err) {
            if (!app) return;
            app->store_items = items;
            app->store_gen = 0;
            Fl::remove_timeout(store_anim_tick);
            if (!err.empty()) app->set_status("Error: " + err);
            if (app->view != V_STORE) return;
            app->show_store();
        });
    }
}

void hide_store_view() {
    if (store_view) store_view->hide();
    Fl::remove_timeout(store_anim_tick);
}
