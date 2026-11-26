#include <FL/fl_ask.H>
#include <ctype.h>
#include "app.h"

#include <FL/Fl_Box.H>
#include <FL/x.H>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "chatsend.h"
#include "player.h"
#include "mpkeys.h"
void hide_store_view();

App *app;

void quit_app()
{
    if (!downloads_quit())
        return;
    // exit() would run the destructors of the static condition variables the worker threads
    // are still waiting on, and glibc blocks there forever: the window never closed.
    player_stop();
    fflush(nullptr);
    _exit(0);
}

void App::set_title(const std::string &t)
{
    grid_title = t;
    header_title->set(t);
}

// --- layout --------------------------------------------------------------------------------------------

static const int TOP = 38, STAT = 20, HEAD = 32;

static int side_width(int W)
{
    return W >= 860 ? 118 : 46; // narrow window (800x600 screens): icons only
}

void App::layout_top(int W, int H, int SIDE)
{
    top->resize(0, 0, W, TOP);
    bool small = W < 700;
    bool back = back_btn->visible();
    // "Volver" takes the place of the name while there is somewhere to go back to.
    if (back)
        logo->hide();
    else
        logo->show();
    logo->resize(10, 0, small ? 0 : 76, TOP);
    back_btn->resize(6, 7, 74, 24);
    int tx = small && !back ? 6 : 84;
    int tw = small ? 60 : 68;
    for (int i = 0; i < P_COUNT; i++) {
        tabs[i]->resize(tx, 7, tw, 24);
        tx += tw + 3;
    }
    int bw = small ? 30 : 64;
    int sw = std::max(100, W - tx - 8 - bw - 4 - 28 - 4 - 8);
    search->resize(tx + 8, 7, sw, 24);
    search_btn->resize(tx + 8 + sw + 4, 7, bw, 24);
    reload_btn->resize(tx + 8 + sw + 4 + bw + 4, 7, 28, 24);
    search_btn->label(small ? "@search" : "Buscar");
    search_btn->symbol = small;
    side->resize(0, TOP, SIDE, H - TOP - STAT);
    bool compact = SIDE < 80;
    int y_pos = TOP + 8;
    for (int i = 0; i < NAV_COUNT; i++) {
        if (i == NAV_EXIT) {
            nav[i]->resize(4, H - STAT - 8 - 30, SIDE - 8, 30);
        } else if (i == NAV_RESTART) {
            nav[i]->resize(4, H - STAT - 8 - 30 - 34, SIDE - 8, 30);
        } else {
            nav[i]->resize(4, y_pos, SIDE - 8, 30);
            y_pos += 34;
        }
        nav[i]->compact = compact;
    }
    status_bar->resize(0, H - STAT, W, STAT);
    status->resize(10, H - STAT, W - 20, STAT);
}




// --- timers de vista -------------------------------------------------------------------------------
// Un solo tick de animación para la tienda, cancelable desde cualquier cambio de vista.
static void store_anim_tick(void *) { /* lo define app_store.cpp, aquí solo el forward usado por cancelar */ }
static void rebuild_settings(void *);
void cancel_view_timers()
{
    Fl::remove_timeout(rebuild_settings);
    // store_anim_tick se define en app_store.cpp; aquí solo evitamos el símbolo duplicado
}
static void rebuild_settings(void *)
{
    if (app && app->ready && app->view == V_SETTINGS)
        app->show_settings();
}

void App::layout()
{
    int W = win->w(), H = win->h();
    if (fullscreen) {
        layout_watch(0, 0, 0, 0);
        return;
    }
    int SIDE = side_width(W);
    layout_top(W, H, SIDE);
    int cx = SIDE, cw = W - SIDE, cy = TOP, ch = H - TOP - STAT;
    // The video screen has no header: its title is under the video, once.
    int hh = view == V_WATCH ? 0 : HEAD;
    if (hh)
        header->show();
    else
        header->hide();
    header->resize(cx, cy, cw, hh);
    int hx = cx + 12;
    sub_btn->resize(cx + cw - 110, cy + 4, 102, 24);
    int hr = cx + cw - 8 - (sub_btn->visible() ? 110 : 0); // right end of the title
    // Música: the song that plays while browsing, or the saved queue to pick up again.
    bool now = view != V_WATCH && plat == P_MUSIC && (music_bg || !queue.empty());
    if (now) {
        std::string l = music_bg ? "Sonando: " + clean(cur.item.title) : std::string("Retomar la cola");
        fl_font(FL_HELVETICA_BOLD, now_btn->font_size);
        int nw = std::min(std::max(120, (int)fl_width(l.c_str()) + 20), std::max(120, cw / 3));
        now_btn->copy_label(fit(l, nw - 14).c_str());
        now_btn->resize(hr - nw, cy + 4, nw, 24);
        now_btn->show();
        hr -= nw + 4;
    } else {
        now_btn->hide();
    }
    if (play_all_btn->visible()) {
        shuffle_all_btn->resize(hr - 80, cy + 4, 76, 24);
        play_all_btn->resize(hr - 80 - 124, cy + 4, 120, 24);
        hr -= 208;
    }
    header_title->resize(hx, cy, std::max(40, hr - hx), HEAD);
    int by = cy + hh, bh = ch - hh;
    scroll->resize(cx, by, cw, bh);
    grid->resize(cx, by - scroll->yposition(), cw - 12, grid->content_h(cw - 12));
    scroll->init_sizes();
    settings->resize(cx, by, cw, bh);
    layout_watch(cx, by, cw, bh);
    if (view == V_DOWNLOADS)
        downloads_layout();
    if (ready && view == V_SETTINGS && settings_w && settings_w != cw) {
        Fl::remove_timeout(rebuild_settings);
        Fl::add_timeout(0.2, rebuild_settings);
    }
    win->redraw();
}

void MainWindow::resize(int X, int Y, int W, int H)
{
    bool sized = W != w() || H != h();
    Fl_Double_Window::resize(X, Y, W, H);
    static bool in_layout = false;
    if (sized && app && app->ready && !in_layout) {
        in_layout = true;
        app->layout();
        in_layout = false;
    }
}

// --- navigation --------------------------------------------------------------------------------------

void App::select_nav(int i)
{
    for (int k = 0; k < NAV_COUNT; k++)
        nav[k]->set_selected(k == i);
}

void App::select_plat(int p)
{
    plat = p;
    set_setting("platform", std::to_string(p));
    for (int i = 0; i < P_COUNT; i++)
        tabs[i]->set_selected(i == p);
    if (view == V_WATCH)
        leave_watch();
    // A search belongs to the platform it was made on: the new one starts clean.
    clear_search();
    if (view == V_SUBS || view == V_HISTORY)
        show_grid(view);
    else if (view == V_FEED)
        load_page(V_FEED);
    else
        load_page(V_HOME);
}

void search_now()
{
    std::string q = app->search->value();
    Fl::focus(app->win); // the keys go back to the player
    if (app->view == V_WATCH)
        app->leave_watch();
    if (q.empty()) { // an emptied box goes back home instead of repeating the last search
        app->clear_search();
        app->load_page(V_HOME);
        return;
    }
    app->query = q;
    app->load_page(V_SEARCH);
}

void App::nav_to(int i)
{
    if (i == NAV_EXIT) {
        quit_app();
        return; // downloads running and the user stayed
    }
    if (view == V_WATCH)
        leave_watch();
    switch (i) {
    case NAV_HOME:
        clear_search();
        load_page(V_HOME);
        break;
    case NAV_FEED:
        load_page(V_FEED);
        break;
    case NAV_SUBS:
        show_grid(V_SUBS);
        break;
    case NAV_HISTORY:
        show_grid(V_HISTORY);
        break;
    case NAV_DOWNLOADS:
        show_downloads();
        break;
    case NAV_SETTINGS:
        show_settings();
        break;
    case NAV_STORE:
        show_store();
        break;
    case NAV_RESTART:
        store_restart_app();
        break;
    }
}

void App::go_back()
{
    if (view == V_WATCH) {
        leave_watch();
        show_grid(grid_view);
    } else if (view == V_CHANNEL) {
        if (!query.empty()) {
            search->value(query.c_str());
            load_page(V_SEARCH);
        } else {
            load_page(V_HOME);
        }
    } else if (view == V_SEARCH || view == V_SETTINGS || view == V_DOWNLOADS) {
        bool from_search = (view == V_SEARCH);
        bool page = view == V_SETTINGS || view == V_DOWNLOADS;
        clear_search();
        show_grid(V_HOME);
        if (from_search || grid->items.empty() || grid_view != V_HOME || page)
            load_page(V_HOME);
    }
}

void App::reload()
{
    if (view == V_WATCH) {
        Item it = cur.item;
        reload_pos = it.live ? 0 : player_pos();
        open_item(it);
    } else if (view == V_SETTINGS) {
        show_settings();
    } else if (view == V_DOWNLOADS) {
        show_downloads();
    } else if (view == V_SUBS || view == V_HISTORY) {
        show_grid(view);
    } else if (view == V_SEARCH) {
        // What the box says now, not the last search (it may have been erased or changed).
        query = search->value();
        load_page(query.empty() ? V_HOME : V_SEARCH);
    } else {
        load_page(view);
    }
}

void App::clear_search()
{
    query.clear();
    search->value("");
}

void App::open_channel(int p, const std::string &url, const std::string &name)
{
    if (view == V_WATCH)
        leave_watch();
    if (p != plat) {
        plat = p;
        for (int i = 0; i < P_COUNT; i++)
            tabs[i]->set_selected(i == p);
    }
    channel_url = url;
    channel_name = name;
    load_page(V_CHANNEL);
}

// --- card screens ----------------------------------------------------------------------------------

static int nav_of(View v)
{
    switch (v) {
    case V_HOME:
    case V_SEARCH:
        return NAV_HOME;
    case V_FEED:
        return NAV_FEED;
    case V_SUBS:
        return NAV_SUBS;
    case V_HISTORY:
        return NAV_HISTORY;
    case V_SETTINGS:
        return NAV_SETTINGS;
    case V_DOWNLOADS:
        return NAV_DOWNLOADS;
    case V_STORE:
        return NAV_STORE;
    default:
        return -1;
    }
}

static std::string follow_word(int p)
{
    return p == P_MUSIC ? "artistas" : "canales";
}

// Siguiendo: the channels followed in Sansón, and then the ones of the account (if any).
static void add_account_channels(int g, int p)
{
    if (!backend_logged(p))
        return;
    app->set_status("Cargando los canales que sigue tu cuenta de " + std::string(plat_name(p)) + "…");
    backend_following(p, [g, p](std::vector<Item> items, std::string err) {
        if (g != app->gen || app->view != V_SUBS)
            return;
        std::set<std::string> have;
        for (auto &it : app->grid->items)
            have.insert(it.url);
        int added = 0;
        for (auto &it : items)
            if (have.insert(it.url).second) {
                app->grid->items.push_back(it);
                added++;
            }
        if (!app->grid->items.empty())
            app->grid->message = "";
        app->set_status(!err.empty() ? "No se pudieron leer los canales de tu cuenta: " + err
                                     : std::to_string(app->grid->items.size()) + " " + follow_word(p) + " · " +
                                           std::to_string(added) + " de tu cuenta");
        app->layout();
    });
}

void App::show_grid(View v)
{
    if (view == V_DOWNLOADS) clear_downloads_view();

    cancel_view_timers();
    hide_store_view();
    view = v;
    if (v != V_WATCH && v != V_SETTINGS && v != V_DOWNLOADS)
        grid_view = v;
    watch->hide();
    settings->hide();
    scroll->show();
    if (v == V_CHANNEL || v == V_SEARCH)
        back_btn->show();
    else
        back_btn->hide();
    sub_btn->hide();
    select_nav(nav_of(v));
    if (v == V_SUBS || v == V_HISTORY) {
        if (pager > 0)
            backend_drop(plat, pager);
        gen++;
        pager = 0;
        more = false;
        grid->clear_items();
        auto list = v == V_SUBS ? subs_list() : history_list();
        for (auto &it : list)
            if (it.plat == plat)
                grid->items.push_back(it);
        std::string pn = plat_name(plat);
        if (v == V_SUBS)
            grid->message = grid->items.empty() && !backend_logged(plat)
                                ? "Aún no sigues " + follow_word(plat) + " de " + pn +
                                      ". Abre uno y pulsa «Seguir», o inicia sesión en Ajustes."
                                : "";
        else
            grid->message = grid->items.empty() ? "Todavía no viste nada en " + pn + "." : "";
        set_title(std::string(v == V_SUBS ? "Siguiendo" : "Historial") + " · " + pn);
        scroll->scroll_to(0, 0);
        if (v == V_SUBS)
            add_account_channels(gen, plat);
    } else {
        header_title->set(grid_title);
    }
    play_all_btn->hide();
    shuffle_all_btn->hide();
    if (v == V_CHANNEL) {
        // Música: albums and playlists are not followed, artists are. Nor YouTube playlists.
        bool list = channel_url.find("/browse/") != std::string::npos || channel_url.find("list=") != std::string::npos;
        if (!list)
            sub_btn->show();
        bool on = subs_has(channel_url);
        sub_btn->label(on ? "Siguiendo" : "Seguir");
        sub_btn->selected = on;
        if (plat == P_MUSIC) {
            play_all_btn->show();
            shuffle_all_btn->show();
        }
    }
    layout();
}

void App::load_page(View v)
{
    if (view == V_DOWNLOADS) clear_downloads_view();

    cancel_view_timers();
    if (pager > 0)
        backend_drop(plat, pager);
    gen++;
    pager = 0;
    more = false;
    loading = true;
    grid->clear_items();
    grid->message = "Cargando…";
    show_grid(v);
    scroll->scroll_to(0, 0);
    int g = gen;
    auto done = [g](Page pg) { app->on_page(pg, g, false); };
    std::string pn = plat_name(plat);
    if (v == V_SEARCH) {
        set_title("Resultados: " + query + " · " + pn);
        backend_search(plat, query, done);
    } else if (v == V_FEED) {
        set_title(std::string(plat == P_YOUTUBE || plat == P_MUSIC ? "Novedades" : "En vivo ahora") +
                  " de lo que sigues · " + pn);
        backend_feed(plat, done);
    } else if (v == V_CHANNEL) {
        set_title(channel_name.empty() ? "Canal" : clean(channel_name));
        backend_channel_videos(plat, channel_url, done);
        backend_channel(plat, channel_url, [g](Channel c) {
            if (g != app->gen || !c.error.empty())
                return;
            app->channel_name = c.name;
            std::string t = clean(c.name);
            if (c.subs >= 0)
                t += "  ·  " + fmt_count(c.subs) + " seguidores";
            app->set_title(t);
        });
    } else {
        set_title(std::string(backend_logged(plat) && plat != P_MUSIC ? "Para ti" : "Inicio") + " · " + pn);
        backend_home(plat, done);
    }
    set_status("Cargando " + pn + "…");
}

void App::on_page(const Page &pg, int g, bool append)
{
    if (g != gen) {
        backend_drop(pg.plat, pg.pager);
        return;
    }
    loading = false;
    if (!pg.error.empty()) {
        if (error_is_captcha(pg.error))
            Fl::add_timeout(0.0, [](void *d) { verify_captcha((int)(long)d); }, (void *)(long)pg.plat);
        if (append) {
            grid->message = "No se pudo cargar más: " + pg.error;
        } else {
            grid->message = "Error: " + pg.error;
            grid->clear_items();
        }
        if (view != V_WATCH)
            set_status("Error al cargar");
        more = false;
        layout();
        return;
    }
    if (!append)
        grid->clear_items();
    std::set<std::string> have;
    for (auto &it : grid->items)
        have.insert(it.url);
    for (auto &it : pg.items)
        if (have.insert(it.url).second)
            grid->items.push_back(it);
    pager = pg.pager;
    more = pg.more;
    if (grid->items.empty()) {
        if (view == V_FEED)
            grid->message = plat == P_YOUTUBE ? "Sigue canales (o inicia sesión en Ajustes) para ver aquí sus videos nuevos."
                                              : "Ninguno de los canales que sigues está en vivo ahora.";
        else
            grid->message = "No hay resultados.";
    } else {
        grid->message = "";
    }
    if (view != V_WATCH) // the page may arrive while a video is open
        set_status(std::to_string(grid->items.size()) + " elementos · " + plat_name(plat));
    layout();
}

static void scroll_check(void *)
{
    if (app->view != V_WATCH && app->view != V_SETTINGS && app->view != V_DOWNLOADS && app->more && !app->loading && app->pager > 0) {
        int bottom = app->scroll->yposition() + app->scroll->h();
        if (bottom > app->grid->h() - 300) {
            app->loading = true;
            app->grid->message = "Cargando más…";
            app->grid->redraw();
            int g = app->gen;
            backend_next(app->plat, app->pager, [g](Page pg) { app->on_page(pg, g, true); });
        }
    }
    Fl::repeat_timeout(0.3, scroll_check);
}

// A card: Música songs play with the songs of the page after them as the queue (album, search,
// artist); videos remember the page for continuous play.
void App::open_card(int i)
{
    if (!grid || i < 0 || i >= (int)grid->items.size() || grid->items.empty()) return;
    const Item it = grid->items[i];
    if (it.plat == P_MUSIC && it.kind == "video") {
        std::vector<Item> songs;
        int idx = 0;
        for (int k = 0; k < (int)grid->items.size(); k++) {
            const Item &x = grid->items[k];
            if (x.plat != P_MUSIC || x.kind != "video") continue;
            if (k == i) idx = (int)songs.size();
            songs.push_back(x);
        }
        play_list(songs, idx, false);
        return;
    }
    if (it.kind == "video") {
        source_list.clear();
        for (auto &x : grid->items)
            if (x.kind == "video" && category(x) == category(it))
                source_list.push_back(x);
    }
    open_item(it);
}

// --- keys ----------------------------------------------------------------------------------------------

static int global_keys(int e)
{
    if (e != FL_SHORTCUT || !app)
        return 0;
    int k = Fl::event_key();
    if (k == FL_F + 5 && !app->fullscreen) {
        app->reload();
        return 1;
    }
    bool typing = (Fl::focus() == app->search || Fl::focus() == app->chat_in) && !app->fullscreen;
    if (k == FL_Escape) {
        if (app->fullscreen)
            app->toggle_fullscreen();
        else if (app->countdown > 0)
            app->cancel_countdown();
        else if (typing)
            Fl::focus(app->win);
        else
            app->go_back();
        return 1;
    }
    if (typing)
        return 0;
    if (k == FL_BackSpace && !app->fullscreen) {
        app->go_back();
        return 1;
    }
    if (k == '/' && !app->fullscreen) {
        Fl::focus(app->search);
        app->search->position(0, app->search->size());
        return 1;
    }
    return 0;
}

// While watching, mplayer's own keys (mpkeys.h), also in full screen. They go before the
// widgets: otherwise the focused button takes Space and the arrows move the focus.
static int watch_keys()
{
    bool typing = (Fl::focus() == app->search || Fl::focus() == app->chat_in) && !app->fullscreen;
    if (app->view != V_WATCH || typing)
        return 0;
    MpKey m = mp_key_event();
    switch (m.act) {
    case MK_QUIT: // q and Esc: back to the list, as mplayer closes
        app->go_back();
        return 1;
    case MK_PAUSE:
        player_pause();
        return 1;
    case MK_SEEK:
        app->seek_rel(m.arg);
        return 1;
    case MK_VOLUME:
        player_volume(m.arg > 0 ? 3 : -3);
        app->set_status("Volumen: " + std::to_string(player_get_volume()) + " %");
        return 1;
    case MK_MUTE:
        app->set_status(player_mute() ? "Silencio" : "Volumen: " + std::to_string(player_get_volume()) + " %");
        return 1;
    case MK_FULLSCREEN:
        app->toggle_fullscreen();
        return 1;
    case MK_PREV: // Sansón has no previous video
        return 1;
    case MK_NEXT:
        app->play_next();
        return 1;
    case MK_FRAME_STEP:
        player_frame_step();
        return 1;
    case MK_SCREENSHOT: {
        bool each = m.arg > 0;
        bool on = player_screenshot(each);
        const char *h = getenv("HOME");
        std::string dir = h ? h : "~";
        if (!each)
            app->set_status("Captura guardada en " + dir + " (shot0001.png…)");
        else
            app->set_status(on ? "Guardando cada cuadro en " + dir + " (S para parar)" : "Capturas detenidas");
        return 1;
    }
    case MK_CMD:
        player_cmd(m.cmd, m.show_es);
        return 1;
    case MK_NONE:
        break;
    }
    // Sansón's own
    if (Fl::event_state() & (FL_CTRL | FL_ALT | FL_META))
        return 0;
    switch (Fl::event_key()) {
    case 'c':
        app->toggle_panel();
        return 1;
    case 'n':
        app->play_next();
        return 1;
    }
    return 0;
}

static int dispatch(int e, Fl_Window *w)
{
    // a click anywhere but on the box takes the keys out of it
    Fl_Widget *f = Fl::focus();
    if (e == FL_PUSH && app && f && (f == app->search || f == app->chat_in) &&
        !(f->window() == w && Fl::event_inside(f)))
        Fl::focus(app->win);
    // the video is a window of its own: with the pointer on it X gives it the keys
    if (e == FL_KEYBOARD && app && w && w->top_window() == app->win && watch_keys())
        return 1;
    return Fl::handle_(e, w);
}

// --- build -----------------------------------------------------------------------------------------

void App::build()
{
    // 600 high at most: the Acer Aspire One has an 800x600 screen (a little less for the title bar).
    int W = std::min(940, Fl::w() - 8), H = std::min(600, Fl::h() - 30);
    // The window manager reads the title as Latin-1, so the ó goes as that one byte.
    win = new MainWindow(W, H, "TinyTV");
    win->color(C_BG);
    win->xclass("tinytv");
    win->size_range(560, 400);

    top = new Bar(0, 0, W, TOP, C_BAR);
    logo = new Label(10, 0, 76, TOP);
    logo->size = 15;
    logo->fg = C_ACCENT;
    logo->set("TinyTV");
    back_btn = new FlatButton(6, 7, 74, 24, "< Volver");
    back_btn->tooltip("Volver (Esc)");
    back_btn->callback([](Fl_Widget *, void *) { app->go_back(); });
    back_btn->hide();
    for (int i = 0; i < P_COUNT; i++) {
        tabs[i] = new FlatButton(0, 0, 80, 28, plat_name(i));
        tabs[i]->sel_color = plat_color[i];
        tabs[i]->callback([](Fl_Widget *, void *d) { app->select_plat((int)(long)d); }, (void *)(long)i);
    }
    search = new ClickInput(0, 0, 100, 30);
    style_input(search);
    search->when(FL_WHEN_ENTER_KEY_ALWAYS);
    search->tooltip("Buscar (tecla /)");
    search->callback([](Fl_Widget *, void *) { search_now(); });
    search_btn = new FlatButton(0, 0, 84, 30, "Buscar");
    search_btn->bg = C_SEL;
    search_btn->callback([](Fl_Widget *, void *) { search_now(); });
    reload_btn = new FlatButton(0, 0, 28, 24, "@reload");
    reload_btn->symbol = true;
    reload_btn->tooltip("Recargar (F5)");
    reload_btn->callback([](Fl_Widget *, void *) { app->reload(); });
    top->end();

    side = new Bar(0, TOP, 118, H - TOP - STAT, C_SIDE);
    static const char *navl[] = {"Inicio", "Novedades", "Siguiendo", "Historial", "Descargas", "Ajustes", "Tienda", "Reiniciar", "Salir"};
    static const char *navt[] = {"Inicio", "Lo nuevo de lo que sigues", "Canales que sigues", "Historial",
                                 "Descargas de videos y canciones", "Ajustes y cuentas", "Tienda de plugins", "Reiniciar Sansón", "Salir"};
    static const Icon navi[] = {I_HOME, I_NEW, I_FOLLOW, I_HISTORY, I_DOWNLOAD, I_SETTINGS, I_STORE, I_LOOP, I_EXIT};
    for (int i = 0; i < NAV_COUNT; i++) {
        nav[i] = new FlatButton(0, 0, 110, 30, navl[i]);
        nav[i]->bg = C_SIDE;
        nav[i]->icon = navi[i];
        nav[i]->tooltip(navt[i]);
        nav[i]->callback([](Fl_Widget *, void *d) { app->nav_to((int)(long)d); }, (void *)(long)i);
    }
    side->end();

    header = new Bar(118, TOP, W - 118, HEAD, C_BG);
    header_title = new Label(0, 0, 100, HEAD);
    header_title->size = 13;
    sub_btn = new FlatButton(0, 0, 130, 28, "Seguir");
    sub_btn->sel_color = C_GREEN;
    sub_btn->callback([](Fl_Widget *, void *) {
        Item c;
        c.plat = app->plat;
        c.kind = "channel";
        c.url = app->channel_url;
        c.title = app->channel_name;
        c.author = app->channel_name;
        c.author_url = app->channel_url;
        for (auto &it : app->grid->items)
            if (!it.author_thumb.empty()) {
                c.thumb = it.author_thumb;
                break;
            }
        subs_toggle(c);
        app->show_grid(V_CHANNEL);
    });
    play_all_btn = new FlatButton(0, 0, 120, 24, "Reproducir todo");
    play_all_btn->bg = C_SEL;
    play_all_btn->callback([](Fl_Widget *, void *) {
        std::vector<Item> songs;
        for (auto &it : app->grid->items)
            if (it.kind == "video")
                songs.push_back(it);
        app->play_list(songs, 0, false);
    });
    shuffle_all_btn = new FlatButton(0, 0, 80, 24, "Aleatorio");
    shuffle_all_btn->callback([](Fl_Widget *, void *) {
        std::vector<Item> songs;
        for (auto &it : app->grid->items)
            if (it.kind == "video")
                songs.push_back(it);
        if (!songs.empty())
            app->play_list(songs, rand() % (int)songs.size(), true);
    });
    play_all_btn->hide();
    shuffle_all_btn->hide();
    now_btn = new FlatButton(0, 0, 200, 24);
    now_btn->bg = C_SEL;
    now_btn->align_left = 6;
    now_btn->callback([](Fl_Widget *, void *) {
        if (app->music_bg)
            app->show_watch();
        else if (!app->queue.empty())
            app->play_queue(std::max(0, std::min(app->qpos, (int)app->queue.size() - 1)));
    });
    now_btn->hide();
    header->end();

    scroll = new DarkScroll(118, TOP + HEAD, W - 118, H - TOP - HEAD - STAT);
    grid = new Grid(118, TOP + HEAD, W - 130, 100);
grid->on_open = [](int i) {
    if (app->view == V_STORE) {
        if (i < 0 || i >= (int)app->grid->items.size()) return;
        Item it = app->grid->items[i];
        StorePlugin target;
        bool found = false;
        for (auto &p : app->store_items) {
            if (p.id == it.id) { target = p; found = true; break; }
        }
        if (!found) return;
        if (target.installed) {
            std::string msg = "El plugin " + target.name + " ya está instalado.\n¿Deseas desinstalarlo?";
            if (fl_choice(msg.c_str(), "Cancelar", "Desinstalar", nullptr) == 1) {
                std::string tid = target.id;
                store_uninstall(tid, [tid](bool ok, std::string err) {
                    for (auto &p : app->store_items)
                        if (p.id == tid) { p.installed = false; break; }
                    if (app->view == V_STORE) app->show_store();
                    if (!ok) app->set_status("Error: " + err);
                    else app->set_status("Desinstalado. Reinicia Sansón.");
                });
            }
        } else {
            std::string msg = "¿Instalar " + target.name + "?";
            if (fl_choice(msg.c_str(), "Cancelar", "Instalar", nullptr) == 1) {
                app->set_status("Descargando " + target.name + "...");
                store_install(target, [target](bool ok, std::string err) {
                    for (auto &p : app->store_items)
                        if (p.id == target.id) { p.installed = ok; p.local_version = target.version; break; }
                    if (app->view == V_STORE) app->show_store();
                    if (ok) app->set_status("Instalado: " + target.name + ". Pulsa Reiniciar Sansón.");
                    else app->set_status("Error: " + err);
                });
            }
        }
    } else {
        if (i < 0 || i >= (int)app->grid->items.size() || app->grid->items.empty()) return;
        app->open_card(i);
    }
};
grid->on_filter = [] {
    app->scroll->scroll_to(0, 0);
    app->layout();
};
grid->on_menu = [](int i) { app->music_menu(i); };
grid->on_author = [](int i) {
    auto &it = app->grid->items[i];
    app->open_channel(it.plat, it.author_url, it.author);
};
scroll->end();
build_watch();
settings = new DarkScroll(118, TOP + HEAD, W - 118, H - TOP - HEAD - STAT);
settings->end();
settings->hide();
status_bar = new Bar(0, H - STAT, W, STAT, C_BAR);
status = new Label(10, H - STAT, W - 20, STAT);
status->font = FL_HELVETICA;
status->size = 11;
status->fg = C_DIM;
status_bar->end();
win->end();
win->resizable(win);
win->callback([](Fl_Widget *, void *) {
    if (Fl::event() == FL_SHORTCUT && Fl::event_key() == FL_Escape)
        return;
    quit_app();
});
thumbs_changed = [](const std::string &url) {
    app->grid->damage_thumb(url);
    app->related->damage_thumb(url);
    if (app->video->cover == url)
        app->video->redraw();
};
}

// --- main ----------------------------------------------------------------------------------------------
static std::string exe_dir()
{
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0)
        return ".";
    buf[n] = 0;
    std::string s = buf;
    return s.substr(0, s.rfind('/'));
}
static std::string find_share()
{
    const char *env = getenv("SANSON_SHARE");
    if (env)
        return env;
    std::string d = exe_dir();
    for (std::string c : {d + "/../share/sanson", d + "/..", d, std::string("/usr/local/share/tinytv")})
        if (access((c + "/js/sanson_host.js").c_str(), R_OK) == 0)
            return c;
    return "/usr/local/share/tinytv";
}
// SANSON_OPEN (for tests): "ajustes", "<plat>:?query", "<plat>:@channel-url", "<plat>:video-url",
// "<plat>:novedades", "<plat>:siguiendo", "descargas". SANSON_DL=1 with a video: opens the download dialog (auto: also starts it).
static void open_from_env()
{
    const char *open = getenv("SANSON_OPEN");
    if (!open)
        return;
    if (!strcmp(open, "ajustes")) {
        app->show_settings();
        return;
    }
    if (!strcmp(open, "descargas")) {
        app->show_downloads();
        return;
    }
    const char *colon = strchr(open, ':');
    if (!colon)
        return;
    int op = atoi(open);
    std::string arg = colon + 1;
    if (op != app->plat)
        app->select_plat(op);
    if (arg == "novedades") {
        app->nav_to(NAV_FEED);
    } else if (arg == "siguiendo") {
        app->nav_to(NAV_SUBS);
    } else if (arg[0] == '?') {
        app->search->value(arg.c_str() + 1);
        search_now();
    } else if (arg[0] == '@') {
        app->open_channel(op, arg.substr(1), "");
    } else {
        Item it;
        it.plat = op;
        it.kind = "video";
        it.url = arg;
        it.title = arg;
        app->open_item(it);
    }
}
// Abrir un enlace pasado por la linea de comandos: `sanson ENLACE`. Reconoce YouTube, YouTube Music,
// Twitch y Kick, y las paginas espejo de YouTube (Invidious, S60Tube) se traducen a YouTube.
static std::string url_decode(const std::string &s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') {
            o += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            o += s[i];
        }
    }
    return o;
}
static std::string query_param(const std::string &q, const std::string &key)
{
    size_t i = 0;
    while (i < q.size()) {
        size_t e = q.find('&', i);
        if (e == std::string::npos)
            e = q.size();
        std::string kv = q.substr(i, e - i);
        if (kv.compare(0, key.size() + 1, key + "=") == 0)
            return url_decode(kv.substr(key.size() + 1));
        i = e + 1;
    }
    return "";
}
static void open_from_arg(const char *arg)
{
    std::string u = arg;
    size_t sch = u.find("://");
    if (sch == std::string::npos || (u.compare(0, 4, "http") != 0))
        return;
    size_t hs = sch + 3, he = u.find('/', hs);
    std::string host = u.substr(hs, he == std::string::npos ? std::string::npos : he - hs);
    std::string rest = he == std::string::npos ? "/" : u.substr(he);
    if (host.compare(0, 4, "www.") == 0)
        host = host.substr(4);
    if (host.compare(0, 2, "m.") == 0)
        host = host.substr(2);
    size_t qm = rest.find('?');
    std::string path = rest.substr(0, qm), query = qm == std::string::npos ? "" : rest.substr(qm + 1);
    int plat;
    if (host == "inv.nadeko.net" || host == "s60tube.io.vn" || host == "youtube.com" || host == "youtu.be") {
        plat = P_YOUTUBE;
        if (host == "s60tube.io.vn" && path.compare(0, 7, "/video/") == 0) {
            std::string id = path.substr(7);
            size_t e = id.find('/');
            if (e != std::string::npos)
                id.resize(e);
            query = "v=" + id;
            path = "/watch";
        } else if (host == "youtu.be") {
            query = "v=" + path.substr(1);
            path = "/watch";
        }
        if (path == "/results" || path == "/search") {
            std::string q = query_param(query, path == "/results" ? "search_query" : "q");
            if (q.empty())
                return;
            if (app->plat != plat)
                app->select_plat(plat);
            app->search->value(q.c_str());
            search_now();
            return;
        }
        if (path.compare(0, 2, "/@") == 0 || path.compare(0, 9, "/channel/") == 0 || path.compare(0, 3, "/c/") == 0 ||
            path.compare(0, 6, "/user/") == 0) {
            if (app->plat != plat)
                app->select_plat(plat);
            app->open_channel(plat, "https://www.youtube.com" + path, "");
            return;
        }
        u = "https://www.youtube.com" + path + (query.empty() ? "" : "?" + query);
    } else if (host == "music.youtube.com") {
        plat = P_MUSIC;
    } else if (host == "twitch.tv") {
        plat = P_TWITCH;
    } else if (host == "kick.com") {
        plat = P_KICK;
    } else {
        return;
    }
    if (app->plat != plat)
        app->select_plat(plat);
    Item it;
    it.plat = plat;
    it.kind = "video";
    it.url = u;
    it.title = u;
    app->open_item(it);
}

// ---------------------------------------------------------------------------
// Argumentos de línea de comandos.
//   --debug / -d  : equivale a SANSON_DEBUG=1 SP_DEBUG=1
//   --help  / -h  : ayuda breve y sale
//   --version     : versión y sale
// Los logs se activan ANTES de backend_start() (log_cb y ensure_open leen
// getenv), por eso parse_args() se llama al principio de main().
// ---------------------------------------------------------------------------
static const char *TINYTV_DOCS = "https://raw.githubusercontent.com/IamJony/tinytv/refs/heads/main/docs/docs.md";
static const char *TINYTV_VERSION = "1.0.1";

static void print_help()
{
    const char *home = getenv("HOME");
    std::string cfg = home ? home : "~";
    cfg += "/.config/tinytv/plugins";

    printf(
        "TinyTV - reproduce contenido streaming de diversas plataformas de manera ligera y rapida.\n"
        "\n"
        "TinyTV es un fork de Sanson que incorpora un sistema de plugins externos\n"
        "y una tienda integrada para instalar, actualizar y gestionar plataformas\n"
        "de streaming adicionales.\n"
        "\n"
        "\n"
        "Uso:\n"
        "  tinytv [OPCIONES] [ENLACE]\n"
        "\n"
        "Opciones:\n"
        "  -d, --debug       Muestra los logs de depuración (equivale a SANSON_DEBUG=1 SP_DEBUG=1).\n"
        "  -h, --help        Muestra esta ayuda.\n"
        "      --version     Muestra la versión actual instalada.\n"
        "\n"
        "Entorno:\n"
        "  SANSON_SHARE      Directorio de datos (por defecto /usr/local/share/tinytv).\n"
        "  SANSON_DEBUG=1    Logs del host JS.\n"
        "  SP_DEBUG=1        Logs del stream/proxy.\n"
        "  SANSON_OPEN       \"ajustes\", \"descargas\", \"<plat>:?query\", \"<plat>:@canal\", \"<plat>:video\".\n"
        "\n"
        "Plugins:\n"
        "\n"
        "Directorio de plugins %s\n"
        "\n"
        "Ejemplos:\n"
        "  tinytv --debug\n"
        "  tinytv https://www.youtube.com/watch?v=dQw4w9WgXcQ (debe tener el plugin youtube instalado)\n"
        "  SANSON_OPEN=0:?kimetsu tinytv\n"
        "\n"
        "Documentación completa:\n"
        "  %s\n",
        cfg.c_str(), TINYTV_DOCS);
}

static void print_version()
{
    printf("TinyTV %s\n", TINYTV_VERSION);
    printf("Documentación: %s\n", TINYTV_DOCS);
}

// Devuelve true si ya se atendió --help/--version y main debe salir sin abrir la ventana.
static bool parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (!strcmp(argv[i], "--debug") || !strcmp(argv[i], "-d")) {
            setenv("SANSON_DEBUG", "1", 1);
            setenv("SP_DEBUG", "1", 1);
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_help();
            return true;
        } else if (!strcmp(argv[i], "--version")) {
            print_version();
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    if (parse_args(argc, argv))
        return 0;
    Fl::lock();
    Fl::visual(FL_RGB);
    Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
    init_colors();
    Fl::set_color(FL_BACKGROUND_COLOR, 38, 40, 46);
    Fl::set_color(FL_BACKGROUND2_COLOR, 52, 56, 64);
    Fl::set_color(FL_FOREGROUND_COLOR, 225, 228, 234);
    Fl::set_color(FL_SELECTION_COLOR, 70, 110, 160);
    Fl::scheme("none");
    std::string share = find_share();
    backend_start(share + "/plugins", share + "/js/sanson_host.js");
    app = new App();
    app->build();
    app->queue_load();
    player_init(player_changed);
    Fl::add_handler(global_keys);
    Fl::event_dispatch(dispatch);
    Fl::add_system_handler(fs_events, nullptr);
    app->win->show(1, argv);
    app->ready = true;
    int p = atoi(setting("platform", "0").c_str());
    if (p < 0 || p >= P_COUNT)
        p = 0;
    app->layout();
    app->select_plat(p);
    open_from_env();
    if (argc > 1 && !getenv("SANSON_OPEN"))
        open_from_arg(argv[1]);
    Fl::add_timeout(0.3, scroll_check);
    Fl::add_timeout(1.0, watchdog_tick);
    return Fl::run();
}
