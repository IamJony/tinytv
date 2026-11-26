// The video screen: the picture and its controls, the title and description, and the panel
// (chat, comments, what comes next; lyrics and queue in Música).

#include "app.h"

#include <FL/Fl_Box.H>
#include <FL/x.H>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <unistd.h>

#include "chatsend.h"
#include "player.h"
#include "stream.h"

static int panel_for(const Item &it)
{
    if (it.plat == P_MUSIC)
        return setting("music_panel", "lyrics") == "queue" ? App::PANEL_QUEUE : App::PANEL_LYRICS;
    bool c_ovr = false, has_c = backend_comments_enabled(it.plat, c_ovr);
    bool ch_ovr = false, has_ch = backend_chat_enabled(it.plat, ch_ovr);
    bool comments_on = has_c ? c_ovr : (it.plat == P_YOUTUBE);
    bool chat_on = has_ch ? ch_ovr : true;
    if (it.live)
        return chat_on ? App::PANEL_LIVE : App::PANEL_REPLAY;
    if (comments_on)
        return App::PANEL_COMMENTS;
    return App::PANEL_REPLAY;
}

// Box for the picture inside bx,by,bw,bh keeping its aspect (mplayer -wid stretches the
// picture to the whole window). A picture smaller than the box is shown at its real size unless
// "grow" (full screen): stretching it would only cost more work to the X server.
static void fit_video(int bx, int by, int bw, int bh, bool grow, int &x, int &y, int &w, int &h)
{
    int nw, nh;
    player_video_size(nw, nh);
    double ar = player_video_aspect();
    w = bw;
    h = bh;
    if (ar > 0) {
        if (bw > bh * ar)
            w = (int)(bh * ar + 0.5);
        else
            h = (int)(bw / ar + 0.5);
        int dw = (int)(nh * ar + 0.5);
        if (!grow && nh > 0 && dw < w && nh < h) {
            w = dw;
            h = nh;
        }
    }
    x = bx + (bw - w) / 2;
    y = by + (bh - h) / 2;
}

// --- panel -----------------------------------------------------------------------------------------

bool App::panel_below()
{
    return watch->w() < 700;
}

bool App::panel_visible()
{
    if (view != V_WATCH || panel == PANEL_NONE || fullscreen || setting("chat", "1") != "1")
        return false;
    return !panel_below() || watch->h() >= 360; // under the video it needs some height
}

void App::toggle_panel()
{
    bool on = setting("chat", "1") != "1";
    set_setting("chat", on ? "1" : "0");
    if (on && cur.item.plat != P_MUSIC) {
        chat->clear_all();
        chat_fails = 0;
        start_chat();
    } else if (!on) {
        stop_chat();
    }
    layout();
}

// Tab 0: chat, comments or lyrics; tab 1: what comes next, or the Música queue.
void App::select_tab(int t)
{
    if (cur.item.plat == P_MUSIC) {
        panel = t ? PANEL_QUEUE : PANEL_LYRICS;
        set_setting("music_panel", t ? "queue" : "lyrics");
        if (t)
            queue_view->show_current();
    } else {
        related_tab = t == 1;
        set_setting("video_panel", t ? "related" : "main");
    }
    layout();
}

static void place_panel(App *a, int px, int py, int pw, int ph)
{
    bool music = a->cur.item.plat == P_MUSIC;
    a->panel_tabs->resize(px, py, pw, 26);
    a->panel_tabs->show();
    a->tab_btn[0]->resize(px, py, pw / 2, 26);
    a->tab_btn[1]->resize(px + pw / 2, py, pw - pw / 2, 26);
    bool ch_ovr_t = false, has_ch_t = backend_chat_enabled(a->cur.item.plat, ch_ovr_t);
    bool c_ovr_t = false, has_c_t = backend_comments_enabled(a->cur.item.plat, c_ovr_t);
    bool chat_disabled_t = has_ch_t && !ch_ovr_t;
    bool comments_disabled_t = has_c_t && !c_ovr_t;
    const char *t0 = music ? "Letra"
                     : a->panel == App::PANEL_COMMENTS ? "Comentarios"
                     : chat_disabled_t ? (comments_disabled_t ? "Sin chat" : "Comentarios")
                     : "Chat";
    a->tab_btn[0]->label(t0);
    if (!music && chat_disabled_t && a->panel != App::PANEL_COMMENTS) {
        a->tab_btn[0]->deactivate();
        a->related_tab = true;
    } else {
        a->tab_btn[0]->activate();
    }
    a->tab_btn[1]->label(music ? "Cola" : "Siguientes");
    bool second = music ? a->panel == App::PANEL_QUEUE : a->related_tab;
    a->tab_btn[0]->set_selected(!second);
    a->tab_btn[1]->set_selected(second);
    int cy = py + 28, ch = ph - 28;
    Fl_Widget *views[] = {a->chat, a->comments, a->related, a->lyrics, a->queue_view};
    Fl_Widget *shown;
    if (music)
        shown = second ? (Fl_Widget *)a->queue_view : (Fl_Widget *)a->lyrics;
    else if (second)
        shown = a->related;
    else
        shown = a->panel == App::PANEL_COMMENTS ? (Fl_Widget *)a->comments : (Fl_Widget *)a->chat;
    for (Fl_Widget *v : views)
        if (v != shown)
            v->hide();
    bool input = !music && !second;
    if (input) {
        int iy = cy + ch - 26;
        a->chat_in->resize(px, iy, pw - 78, 26);
        a->chat_send_btn->resize(px + pw - 74, iy, 74, 26);
        a->chat_send_btn->label(a->panel == App::PANEL_COMMENTS ? "Comentar" : "Enviar");
        a->chat_hint->resize(px + 4, iy, pw - 8, 26);
        ch -= 30;
        a->update_chat_input();
    } else {
        a->chat_in->hide();
        a->chat_send_btn->hide();
        a->chat_hint->hide();
    }
    shown->resize(px, cy, pw, ch);
    shown->show();
}

static void hide_panel(App *a)
{
    for (Fl_Widget *v : {(Fl_Widget *)a->panel_tabs, (Fl_Widget *)a->chat, (Fl_Widget *)a->comments,
                         (Fl_Widget *)a->related, (Fl_Widget *)a->lyrics, (Fl_Widget *)a->queue_view,
                         (Fl_Widget *)a->chat_in, (Fl_Widget *)a->chat_send_btn, (Fl_Widget *)a->chat_hint})
        v->hide();
}

static void show_if(Fl_Widget *w, bool on)
{
    if (on)
        w->show();
    else
        w->hide();
}

// The buttons over the video, laid out inside their own window (ow wide). What does not fit goes
// away in this order: the jumps of 10 s, then volume (the arrow keys remain).
void App::layout_overlay(int ow, int KH, bool music, bool live)
{
    show_if(seek, !live);
    seek->resize(8, 1, ow - 16, 12);
    int by2 = KH - 30;
    int x = ow - 8;
    auto place = [&](Fl_Widget *w, int width, bool on) {
        show_if(w, on);
        if (!on)
            return;
        x -= width;
        w->resize(x, by2, width, 26);
        x -= 4;
    };
    bool narrow = ow < 600;
    place(full_btn, 64, !music);
    panel_btn->set_selected(setting("chat", "1") == "1");
    place(panel_btn, 58, panel != PANEL_NONE);
    auto_btn->set_selected(autoplay());
    auto_btn->compact = narrow;
    auto_btn->label("Continua");
    place(auto_btn, narrow ? 32 : 94, !music && !live);
    place(quality, music ? 104 : 72, true);
    place(lang_btn, 62, cur.audios.size() > 1 || !cur.subtitles.empty());
    int kx = 8;
    play_btn->resize(kx, by2, 32, 26);
    kx += 36;
    show_if(prev_btn, music);
    if (music) {
        prev_btn->resize(kx, by2, 34, 26);
        kx += 38;
    }
    show_if(next_btn, music || !live);
    if (music || !live) {
        next_btn->resize(kx, by2, 34, 26);
        kx += 38;
    }
    int room = x - kx;
    bool vol = room >= 64 + 80;
    bool jumps = !live && !music && room >= 64 + 80 + 76;
    show_if(back10, jumps);
    show_if(fwd10, jumps);
    if (jumps) {
        back10->resize(kx, by2, 34, 26);
        fwd10->resize(kx + 38, by2, 34, 26);
        kx += 76;
    }
    show_if(vol_down, vol);
    show_if(vol_up, vol);
    if (vol) {
        vol_up->resize(x - 28, by2, 28, 26);
        vol_down->resize(x - 60, by2, 28, 26);
        x -= 64;
    }
    time_label->resize(kx, by2, std::max(0, std::min(120, x - kx)), 26);
}

void App::layout_watch(int cx, int by, int cw, int bh)
{
    if (fullscreen) {
        int x, y, w, h;
        fit_video(0, 0, fs_w, fs_h, true, x, y, w, h);
        video->resize(x, y, w, h);
        return;
    }
    watch->resize(cx, by, cw, bh);
    bool music = cur.item.plat == P_MUSIC, live = cur.item.live;
    bool show = panel_visible();
    bool below = show && panel_below();
    int PW = show && !below ? std::max(250, std::min(380, cw * 30 / 100)) : 0;
    int wcw = cw - (PW ? PW + 4 : 0);
    int KH = live ? 34 : 46;
    int vw = wcw - 16;
    int vh = std::min(vw * 9 / 16, below ? bh * 42 / 100 : bh - 100);
    vh = std::max(vh, 110);
    vw = std::min(vw, vh * 16 / 9);
    int vx = cx + (wcw - vw) / 2;
    video_bg->resize(vx, by + 6, vw, vh);
    {
        int x, y, w, h;
        fit_video(vx, by + 6, vw, vh, false, x, y, w, h);
        video->resize(x, y, w, h);
    }
    ovl->resize(vx, by + 6 + vh - KH, vw, KH);
    layout_overlay(vw, KH, music, live);

    // Title, channel (button), follow, and the views and likes next to them when they fit.
    int iy = by + 6 + vh + 6;
    watch_title->resize(cx + 12, iy, wcw - 24, 22);
    fl_font(FL_HELVETICA_BOLD, watch_author->font_size);
    const char *an = watch_author->label() ? watch_author->label() : "";
    int aw = std::max(80, std::min((int)fl_width(an) + 16, std::max(80, wcw / 3)));
    watch_author->resize(cx + 12, iy + 24, aw, 24);
    subscribe_watch->resize(cx + 12 + aw + 6, iy + 24, 96, 24);
    // The download button goes on this row, at the right end.
    bool can_dl = can_download(cur);
    show_if(dl_btn, can_dl);
    dl_btn->compact = wcw < 420;
    int dlw = wcw < 420 ? 32 : 92;
    if (can_dl)
        dl_btn->resize(cx + wcw - 12 - dlw, iy + 24, dlw, 24);
    int mx = cx + 12 + aw + 108, mw = cx + wcw - 12 - mx - (can_dl ? dlw + 6 : 0);
    int dy = iy + 54;
    fl_font(watch_meta->font, watch_meta->size);
    if ((int)fl_width(watch_meta->text.c_str()) <= mw) {
        watch_meta->resize(mx, iy + 24, mw, 24);
    } else {
        watch_meta->resize(cx + 12, iy + 50, wcw - 24, 18);
        dy = iy + 70;
    }
    for (Fl_Widget *wd : {(Fl_Widget *)shuffle_btn, (Fl_Widget *)repeat_btn, (Fl_Widget *)radio_btn})
        show_if(wd, music);
    if (music) {
        // Play modes on a row of their own, under the album.
        shuffle_btn->resize(cx + 12, dy, 74, 24);
        repeat_btn->resize(cx + 90, dy, 90, 24);
        radio_btn->resize(cx + 184, dy, 60, 24);
        dy += 30;
    }
    int rest = by + bh - dy - 6;
    if (below) {
        desc->hide();
        place_panel(this, cx + 8, dy, cw - 16, std::max(80, rest));
    } else {
        desc->show();
        desc->resize(cx + 12, dy, wcw - 24, std::max(30, rest));
        if (show)
            place_panel(this, cx + wcw, by + 6, PW - 6, bh - 12);
        else
            hide_panel(this);
    }
}

// --- chat and comments ---------------------------------------------------------------------------------

static void chat_tick(void *d);
static void vod_tick(void *d);

void App::stop_chat()
{
    chat_gen++;
    Fl::remove_timeout(chat_tick);
    Fl::remove_timeout(vod_tick);
    if (chat_pager > 0)
        backend_drop(cur.item.plat, chat_pager);
    chat_pager = 0;
    vod_pending.clear();
    vod_seen.clear();
    vod_last = -1;
    vod_busy = false;
    com_loading = false;
}

void App::start_chat()
{
    stop_chat();
    if (panel == PANEL_NONE || panel == PANEL_LYRICS || panel == PANEL_QUEUE || setting("chat", "1") != "1")
        return;
    bool c_ovr = false, has_c = backend_comments_enabled(cur.item.plat, c_ovr);
    bool ch_ovr = false, has_ch = backend_chat_enabled(cur.item.plat, ch_ovr);
    bool comments_on = has_c ? c_ovr : (cur.item.plat == P_YOUTUBE);
    bool chat_on = has_ch ? ch_ovr : true;
    int g = chat_gen;
    std::string url = cur.item.url;
    if (panel == PANEL_COMMENTS) {
        if (!comments_on) return;
        comments->clear_all();
        com_next.clear();
        com_create.clear();
        load_comments();
        return;
    }
    if (!chat_on) return;
    chat->title = panel == PANEL_LIVE ? "Chat" : "Chat de la repetición";
    if (panel == PANEL_REPLAY) {
        chat->status = "Cargando el chat de la repetición…";
        chat->redraw();
        backend_vod_chat_open(cur.item.plat, url, [g](ChatUpdate u) {
            if (g != app->chat_gen) {
                if (u.pager > 0)
                    backend_drop(app->cur.item.plat, u.pager);
                return;
            }
            if (!u.error.empty() || u.pager <= 0) {
                app->chat->status = "Chat de la repetición no disponible";
                app->chat->redraw();
                return;
            }
            app->chat_pager = u.pager;
            app->chat->status = "";
            app->chat->add(u);
            Fl::add_timeout(0.5, vod_tick, (void *)(long)g);
        });
        return;
    }
    chat->status = "Conectando al chat…";
    chat->redraw();
    backend_chat_open(cur.item.plat, url, [g](ChatUpdate u) { app->on_chat(u, g); });
}

// The replay is asked for the messages from the current position on; they wait in
// vod_pending and are shown when the video reaches their time. A jump clears the list.
void App::vod_step()
{
    if (!player_started())
        return;
    long long ms = (long long)(player_pos() * 1000);
    if (vod_last >= 0 && (ms < vod_last - 3000 || ms > vod_last + 60000)) {
        chat->clear_msgs();
        vod_pending.clear();
        vod_seen.clear();
    }
    vod_last = ms;
    ChatUpdate show;
    while (!vod_pending.empty() && vod_pending.front().time <= ms) {
        show.msgs.push_back(vod_pending.front());
        vod_pending.pop_front();
    }
    if (!show.msgs.empty())
        chat->add(show);
    if (!vod_busy && chat_pager > 0 && (vod_pending.empty() || vod_pending.back().time < ms + 4000)) {
        vod_busy = true;
        int pager = chat_pager;
        chat_pager = 0;
        int g = chat_gen;
        backend_vod_chat_poll(cur.item.plat, pager, ms, [g](ChatUpdate u) { app->on_vod_chat(u, g); });
    }
}

static void vod_tick(void *d)
{
    if ((int)(long)d != app->chat_gen || app->panel != App::PANEL_REPLAY)
        return;
    app->vod_step();
    Fl::repeat_timeout(app->vod_busy ? 0.5 : 1.0, vod_tick, d);
}

void App::on_vod_chat(const ChatUpdate &u, int g)
{
    if (g != chat_gen) {
        if (u.pager > 0)
            backend_drop(cur.item.plat, u.pager);
        return;
    }
    vod_busy = false;
    if (!u.error.empty() || u.pager <= 0) {
        chat_pager = 0;
        chat->status = "Chat de la repetición no disponible";
        chat->redraw();
        return;
    }
    chat_pager = u.pager;
    if (!u.emojis.empty()) {
        ChatUpdate e;
        e.emojis = u.emojis;
        chat->add(e);
    }
    if (vod_seen.size() > 6000)
        vod_seen.clear();
    for (auto &m : u.msgs) {
        if (m.time < 0 || m.time < vod_last - 3000)
            continue;
        if (vod_seen.insert(std::to_string(m.time) + "\x1f" + m.name + "\x1f" + m.text).second)
            vod_pending.push_back(m);
    }
    std::stable_sort(vod_pending.begin(), vod_pending.end(),
                     [](const ChatMsg &a, const ChatMsg &b) { return a.time < b.time; });
}

void App::load_comments()
{
    if (com_loading || panel != PANEL_COMMENTS)
        return;
    com_loading = true;
    comments->can_more = false;
    comments->status = "Cargando comentarios…";
    comments->redraw();
    int g = chat_gen;
    std::string tok = com_next;
    backend_comments(cur.item.plat, cur.item.url, tok,
                     [g, tok](CommentPage cp) { app->on_comments(cp, g, tok.empty()); });
}

void App::on_comments(const CommentPage &cp, int g, bool first)
{
    if (g != chat_gen)
        return;
    com_loading = false;
    if (!cp.error.empty()) {
        comments->status = "No se pudieron cargar los comentarios: " + cp.error;
        comments->redraw();
        return;
    }
    if (cp.disabled) {
        comments->status = "Los comentarios están desactivados en este video.";
        comments->redraw();
        return;
    }
    comments->add(cp.items);
    if (first) {
        com_create = cp.create;
        comments->count = cp.count;
        update_chat_input();
    }
    com_next = cp.next;
    comments->can_more = !com_next.empty();
    comments->status = first && cp.items.empty() ? "Todavía no hay comentarios." : "";
    comments->redraw();
}

void App::post_comment()
{
    std::string text = chat_in->value();
    if (text.empty() || chat_sending || com_create.empty())
        return;
    chat_sending = true;
    chat_send_btn->deactivate();
    set_status("Publicando comentario…");
    int g = chat_gen;
    backend_post_comment(cur.item.plat, com_create, text, [g, text](std::string err) {
        app->chat_sending = false;
        app->chat_send_btn->activate();
        if (!err.empty()) {
            app->set_status(err);
            return;
        }
        app->set_status("Comentario publicado");
        if (g != app->chat_gen)
            return;
        if (text == app->chat_in->value())
            app->chat_in->value("");
        Comment c;
        c.author = "Tú";
        c.text = text;
        c.when = "ahora";
        app->comments->add({c}, true);
    });
}

static void chat_reopen(void *d)
{
    if ((int)(long)d == app->chat_gen && app->view == V_WATCH)
        app->start_chat();
}

void App::on_chat(const ChatUpdate &u, int g)
{
    if (g != chat_gen) {
        if (u.pager > 0)
            backend_drop(cur.item.plat, u.pager);
        return;
    }
    if (!u.error.empty() || u.pager <= 0 || !u.more) {
        chat_pager = 0;
        if (++chat_fails > 5) {
            chat->status = "Chat no disponible" + (u.error.empty() ? std::string() : ": " + u.error);
            chat->redraw();
            return;
        }
        chat->status = "Reconectando…";
        chat->redraw();
        Fl::add_timeout(5.0, chat_reopen, (void *)(long)g);
        return;
    }
    if (!u.msgs.empty())
        chat_fails = 0;
    if (!chat->status.empty()) {
        chat->status = "";
        chat->redraw();
    }
    chat->add(u);
    chat_pager = u.pager;
    Fl::add_timeout(u.interval_ms / 1000.0, chat_tick, (void *)(long)g);
}

static void chat_tick(void *d)
{
    int g = (int)(long)d;
    if (g != app->chat_gen || app->chat_pager <= 0)
        return;
    int pager = app->chat_pager;
    app->chat_pager = 0;
    backend_chat_poll(app->cur.item.plat, pager, [g](ChatUpdate u) { app->on_chat(u, g); });
}

void App::update_chat_input()
{
    if (!panel_visible() || related_tab || cur.item.plat == P_MUSIC) {
        // The box goes with the chat and comments tab: hidden with it, wherever it was left.
        for (Fl_Widget *w : {(Fl_Widget *)chat_hint, (Fl_Widget *)chat_in, (Fl_Widget *)chat_send_btn})
            w->hide();
        return;
    }
    bool can;
    if (panel == PANEL_REPLAY) {
        can = false;
        chat_hint->set("Repetición del chat (solo lectura)");
    } else if (panel == PANEL_COMMENTS) {
        bool logged = backend_logged(cur.item.plat);
        can = logged && !com_create.empty();
        chat_hint->set(!logged ? "Inicia sesión en Ajustes para comentar"
                               : com_loading ? "" : "Este video no permite comentar");
    } else {
        can = chat_can_send(cur.item.plat);
        chat_hint->set("Inicia sesión en Ajustes para escribir");
    }
    show_if(chat_hint, !can);
    show_if(chat_in, can);
    show_if(chat_send_btn, can);
    if (can) {
        if (chat_sending)
            chat_send_btn->deactivate();
        else
            chat_send_btn->activate();
    }
}

void App::send_chat()
{
    if (panel == PANEL_COMMENTS) {
        post_comment();
        return;
    }
    std::string text = chat_in->value();
    if (text.empty() || chat_sending)
        return;
    chat_sending = true;
    chat_send_btn->deactivate();
    set_status("Enviando mensaje…");
    int g = chat_gen;
    chat_send(cur.item.plat, cur.item.url, text, [g, text](std::string err) {
        app->chat_sending = false;
        app->chat_send_btn->activate();
        if (err.empty()) {
            if (g == app->chat_gen && text == app->chat_in->value())
                app->chat_in->value("");
            app->set_status("Mensaje enviado");
        } else {
            app->set_status(err);
        }
    });
}

// --- continuous play ----------------------------------------------------------------------------------

bool App::autoplay()
{
    return setting("autoplay", "1") == "1";
}

// What may come after the current video: its related videos and then the rest of the list it
// was opened from, only of its own kind (a normal video never goes on with a short or a live
// stream, a short goes on with shorts).
std::vector<Item> App::upcoming()
{
    std::vector<Item> out;
    std::set<std::string> have = {cur.item.url};
    int kind = category(cur.item);
    auto add = [&](const Item &it) {
        if (it.kind == "video" && category(it) == kind && have.insert(it.url).second)
            out.push_back(it);
    };
    for (auto &it : cur.related)
        add(it);
    int at = -1;
    for (int i = 0; i < (int)source_list.size(); i++)
        if (source_list[i].url == cur.item.url)
            at = i;
    for (int i = at + 1; at >= 0 && i < (int)source_list.size(); i++)
        add(source_list[i]);
    return out;
}

bool App::pick_next(Item &out)
{
    for (auto &it : upcoming())
        if (!it.live && !watched.count(it.url)) {
            out = it;
            return true;
        }
    return false;
}

void App::update_related()
{
    std::vector<Item> list = upcoming();
    int idx = -1;
    for (int i = 0; i < (int)list.size() && idx < 0; i++)
        if (!list[i].live && !watched.count(list[i].url))
            idx = i;
    related->status = !list.empty() ? "" : cur.item.is_short() ? "No hay más shorts en esta lista." : "Sin videos relacionados.";
    related->set(list, idx);
}

static void countdown_tick(void *)
{
    if (app->countdown <= 0)
        return;
    if (--app->countdown == 0) {
        Item n = app->next_item;
        app->open_item(n);
        return;
    }
    app->video->text = "A continuación en " + std::to_string(app->countdown) + " s:\n" + clean(app->next_item.title) +
                       "\n\nClic aquí para cancelar · N para verlo ya";
    app->video->redraw();
    Fl::repeat_timeout(1.0, countdown_tick);
}

void App::on_video_end()
{
    if (countdown > 0 || cur.item.plat == P_MUSIC || cur.item.live || view != V_WATCH)
        return;
    Item n;
    if (!autoplay() || !pick_next(n)) {
        video->text = "Reproducción terminada";
        video->redraw();
        return;
    }
    if (fullscreen)
        toggle_fullscreen();
    next_item = n;
    countdown = 6;
    Fl::remove_timeout(countdown_tick);
    countdown_tick(nullptr);
    set_status("Reproducción continua: sigue «" + clean(n.title) + "»");
}

void App::cancel_countdown()
{
    if (countdown <= 0)
        return;
    countdown = 0;
    Fl::remove_timeout(countdown_tick);
    video->text = "Reproducción terminada";
    video->redraw();
    set_status("Reproducción continua cancelada");
}

void App::play_next()
{
    if (cur.item.plat == P_MUSIC) {
        next_track(false);
        return;
    }
    Item n;
    if (!pick_next(n)) {
        set_status("No hay un video siguiente");
        return;
    }
    countdown = 0;
    Fl::remove_timeout(countdown_tick);
    open_item(n);
}

// --- player -----------------------------------------------------------------------------------------

void App::lang_menu()
{
    std::deque<std::string> labels; // the menu keeps pointers to these: they must not move
    std::vector<Fl_Menu_Item> m;
    auto item = [&](const std::string &l, long id, int flags) {
        labels.push_back(menu_label(l));
        Fl_Menu_Item it = {};
        it.text = labels.back().c_str();
        it.user_data_ = (void *)id;
        it.flags = flags;
        m.push_back(it);
    };
    auto end = [&] { m.push_back(Fl_Menu_Item{}); };
    if (cur.audios.size() > 1) {
        item("Audio", -1, FL_SUBMENU);
        for (size_t i = 0; i < cur.audios.size(); i++)
            item(cur.audios[i].label, 1000 + (long)i, FL_MENU_RADIO | ((int)i == audio_sel ? FL_MENU_VALUE : 0));
        end();
    }
    if (!cur.subtitles.empty()) {
        item("Subtítulos", -1, FL_SUBMENU);
        item("Sin subtítulos", 2000, FL_MENU_RADIO | (sub_sel == 0 ? FL_MENU_VALUE : 0));
        for (size_t i = 0; i < cur.subtitles.size(); i++)
            item(cur.subtitles[i], 2001 + (long)i, FL_MENU_RADIO | ((int)i + 1 == sub_sel ? FL_MENU_VALUE : 0));
        end();
    }
    end();
    const Fl_Menu_Item *r = m[0].popup(lang_btn->x(), lang_btn->y() + lang_btn->h(), nullptr, nullptr, quality);
    if (!r || (long)r->user_data_ < 0)
        return;
    long id = (long)r->user_data_;
    if (id >= 2000)
        select_subtitle((int)(id - 2000));
    else
        select_audio((int)(id - 1000));
}

// A video that stops moving (the stream stopped arriving, mplayer got stuck or the equipment
// cannot keep up) is reopened on its own; if it happens again soon, one quality lower. Stopping
// in the last seconds is the end (the stream may end a little before the length given, and
// mplayer then waits).
void App::watchdog()
{
    double now = mono_now();
    bool active = (view == V_WATCH || music_bg) && cur_source >= 0 && player_running() && !player_paused() &&
                  !player_ended() && !sp_in_ad();
    if (!active) {
        wd_pos = -1;
        wd_since = now;
        return;
    }
    bool live = cur.item.live, music = cur.item.plat == P_MUSIC;
    double p = player_pos();
    double len = cur.item.duration > 0 ? cur.item.duration : player_length();
    bool near_end = !live && len > 0 && p >= len - (music ? 20 : 4);
    if (!player_started()) {
        // Never started: only live streams are retried, a video may take long the first time.
        if (!live || now - wd_since < 30)
            return;
    } else if (p != wd_pos) {
        wd_pos = p;
        wd_since = now;
        return;
    } else if (now - wd_since < (live ? 6 : near_end ? 3 : 15)) {
        return;
    }
    if (near_end) {
        if (getenv("SANSON_DEBUG"))
            fprintf(stderr, "[watch] fin en %.1f de %.1f\n", p, len);
        wd_pos = -1;
        wd_since = now;
        if (!end_handled) {
            end_handled = true;
            if (music) {
                next_track(true);
            } else {
                player_stop();
                on_video_end();
            }
        }
        return;
    }
    while (!stalls.empty() && now - stalls.front() > 120)
        stalls.pop_front();
    stalls.push_back(now);
    int idx = cur_source;
    std::string msg = "El video se trabó; reconectando…";
    if (stalls.size() >= 2) {
        int h = cur.sources[cur_source].height, best = -1;
        for (int i = 0; i < (int)cur.sources.size(); i++) {
            const Source &s = cur.sources[i];
            if (s.height > 0 && s.height < h && s.label != "Solo audio" &&
                (best < 0 || s.height > cur.sources[best].height))
                best = i;
        }
        if (best >= 0) {
            idx = best;
            msg = "El video se trabó otra vez; bajando a " + cur.sources[best].label + "…";
            stalls.clear();
        }
    }
    if (getenv("SANSON_DEBUG"))
        fprintf(stderr, "[watch] %ld trabado en %.1f, fuente %d -> %d\n", (long)time(nullptr), p, cur_source, idx);
    wd_pos = -1;
    wd_since = now;
    play_source(idx, live ? 0 : p);
    set_status(msg);
}

void watchdog_tick(void *)
{
    app->watchdog();
    Fl::repeat_timeout(1.0, watchdog_tick);
}

void App::select_audio(int v)
{
    if (v < 0 || v >= (int)cur.audios.size() || cur_source < 0 || v == audio_sel)
        return;
    audio_sel = v;
    const AudioTrack &t = cur.audios[v];
    for (auto &s : cur.sources)
        if (!s.audio.empty()) {
            s.audio = t.url;
            s.alen = t.len;
        }
    play_source(cur_source, player_pos());
    set_status("Audio: " + t.label);
}

void App::select_subtitle(int v)
{
    if (v == sub_sel)
        return;
    if (v <= 0) {
        sub_sel = 0;
        sub_path.clear();
        player_sub_off();
        set_status("Subtítulos desactivados");
        return;
    }
    set_status("Cargando subtítulos…");
    int g = watch_gen;
    backend_subtitle(cur.item.plat, v - 1, [g, v](std::string srt, std::string err) {
        if (g != app->watch_gen || app->view != V_WATCH)
            return;
        if (!err.empty()) {
            app->set_status(err);
            return;
        }
        std::string path = config_dir() + "/subtitulo-" + std::to_string(++app->sub_count) + ".srt";
        for (int i = 1; i < app->sub_count; i++)
            unlink((config_dir() + "/subtitulo-" + std::to_string(i) + ".srt").c_str());
        write_file(path, srt, 0644);
        app->sub_sel = v;
        app->sub_path = path;
        player_sub_load(path);
        app->set_status("Subtítulos: " + app->cur.subtitles[v - 1]);
    });
}

static int pick_quality(const std::vector<Source> &src, int box_h)
{
    std::string q = setting("quality", "auto");
    // Automatic stays at 360p/480p (old machines, slow connections); more only if chosen.
    int want = q == "auto" ? (box_h <= 360 ? 360 : 480) : atoi(q.c_str());
    int best = -1;
    for (int i = 0; i < (int)src.size(); i++) {
        const Source &s = src[i];
        if (s.label.find("VP9") != std::string::npos || s.label.find("AV1") != std::string::npos)
            continue;
        if (s.label == "Solo audio" || s.label == "Auto")
            continue;
        if (q == "auto" && s.label.find("p60") != std::string::npos)
            continue;
        // The number in the label is the quality (short side, also for vertical videos).
        int qs = atoi(s.label.c_str());
        if (qs > 0 && (want == 0 || qs <= want) && (best < 0 || atoi(src[best].label.c_str()) < qs))
            best = i;
    }
    if (best < 0)
        for (int i = (int)src.size() - 1; i >= 0; i--)
            if (src[i].label != "Solo audio") {
                best = i;
                break;
            }
    return best < 0 ? 0 : best;
}

static std::string watch_meta_text(const Details &d)
{
    std::string meta;
    if (d.item.plat == P_MUSIC) {
        meta = d.item.album.empty() ? "" : "Álbum: " + d.item.album;
        if (d.item.duration > 0)
            meta += (meta.empty() ? "" : " · ") + fmt_duration(d.item.duration);
        return meta;
    }
    if (d.item.views >= 0)
        meta = fmt_count(d.item.views) + (d.item.live ? " espectadores" : " vistas");
    if (!d.item.live && d.item.date > 0)
        meta += (meta.empty() ? "" : " · ") + fmt_ago(d.item.date);
    if (d.likes >= 0)
        meta += (meta.empty() ? "" : " · ") + fmt_count(d.likes) + " me gusta";
    if (d.item.live)
        meta = "EN VIVO" + (meta.empty() ? "" : " · " + meta);
    return meta;
}

static void on_details(Details d, int g)
{
    if (g != app->watch_gen || (app->view != V_WATCH && !app->music_bg))
        return;
    if (!d.error.empty()) {
        if (error_is_captcha(d.error))
            Fl::add_timeout(0.0, [](void *p) { verify_captcha((int)(long)p); }, (void *)(long)app->cur.item.plat);
        app->reload_pos = 0;
        app->video->text = d.error;
        app->video->redraw();
        app->set_status("No se pudo abrir");
        return;
    }
    const Item &old = app->cur.item;
    if (d.item.url.empty() || old.is_short())
        d.item.url = old.url; // a short stays a short (continuous play goes on with shorts)
    if (d.item.title.empty())
        d.item.title = old.title;
    if (d.item.author.empty())
        d.item.author = old.author;
    if (d.item.thumb.empty())
        d.item.thumb = old.thumb;
    if (d.item.author_url.empty())
        d.item.author_url = old.author_url;
    if (d.item.plat == P_MUSIC) {
        if (!d.item.thumb.empty())
            app->video->cover = d.item.thumb;
        std::string info = d.item.album.empty() ? "" : "Álbum: " + d.item.album + "\n";
        if (!d.fallback.empty())
            info += "YouTube no dio el audio de esta canción: suena desde " + d.fallback + ".\n";
        d.description = info;
        int lg = app->watch_gen;
        backend_lyrics(d, [lg](Lyrics l) {
            if (lg != app->watch_gen || (app->view != V_WATCH && !app->music_bg))
                return;
            if (!l.error.empty())
                app->lyrics->clear_all("No se pudo buscar la letra: " + l.error);
            else
                app->lyrics->set(l);
        });
    }
    app->cur = d;
    app->watch_title->set(clean(d.item.title));
    if (app->view == V_WATCH)
        app->header_title->set(clean(d.item.title));
    app->watch_author->copy_label(clean(d.item.author).c_str());
    app->watch_meta->set(watch_meta_text(d));
    app->desc_buf->text(clean(d.description).c_str());
    bool subbed = subs_has(d.item.author_url);
    app->subscribe_watch->label(subbed ? "Siguiendo" : "Seguir");
    app->subscribe_watch->selected = subbed;
    app->subscribe_watch->redraw();
    app->quality->clear();
    for (auto &s : d.sources) {
        std::string l = s.label;
        for (size_t p = 0; (p = l.find('/', p)) != std::string::npos; p += 2)
            l.replace(p, 1, "\\/");
        app->quality->add(l.c_str());
    }
    Item h = d.item;
    h.kind = "video";
    history_add(h);
    app->watched.insert(h.url);
    app->panel = panel_for(d.item);
    app->update_related();
    if (d.related.empty() && d.item.plat != P_MUSIC) {
        // The fallback plugin does not give them with the video: asked apart.
        int rg = app->watch_gen;
        app->related->status = "Buscando videos relacionados…";
        backend_related(d.item.plat, d.item.url, [rg](Page pg) {
            backend_drop(pg.plat, pg.pager);
            if (rg != app->watch_gen)
                return;
            for (auto &it : pg.items)
                if (it.kind == "video")
                    app->cur.related.push_back(it);
            app->update_related();
        });
    }
    // Música: AAC first (mplayer handles it everywhere); Opus stays in the menu.
    app->play_source(d.item.plat == P_MUSIC ? 0 : pick_quality(d.sources, app->video_bg->h()), app->reload_pos);
    app->reload_pos = 0;
    app->chat->clear_all();
    app->chat_fails = 0;
    app->layout();
    app->start_chat();
    static bool dl_test = getenv("SANSON_DL") != nullptr; // tests: the download dialog without clicks
    if (dl_test && can_download(d)) {
        dl_test = false;
        Fl::add_timeout(1.5, [](void *) { download_dialog(); });
    }
}

void hide_store_view();
void App::open_item(const Item &it)
{
    hide_store_view();
    if (it.kind == "channel" || it.kind == "playlist") {
        open_channel(it.plat, it.url, it.title);
        return;
    }
    bool bg = music_bg && it.plat == P_MUSIC && view != V_WATCH;
    stop_watch();
    watch_gen++;
    if (bg) {
        music_bg = true;
    } else {
        view = V_WATCH;
        if (Fl::focus() == search || Fl::focus() == chat_in) // the player keys go to the player
            Fl::focus(win);
        scroll->hide();
        settings->hide();
        watch->show();
        back_btn->show();
        sub_btn->hide();
        play_all_btn->hide();
        shuffle_all_btn->hide();
        select_nav(-1);
    }
    cur = Details();
    cur.item = it;
    cur_source = -1;
    panel = panel_for(it);
    related_tab = setting("video_panel", "main") == "related";
    bool c_ovr = false, has_c = backend_comments_enabled(it.plat, c_ovr);
    bool ch_ovr = false, has_ch = backend_chat_enabled(it.plat, ch_ovr);
    bool comments_on = has_c ? c_ovr : (it.plat == P_YOUTUBE && !it.live);
    bool chat_on = has_ch ? ch_ovr : true;
    if (!comments_on && (!chat_on || !it.live)) related_tab = true;
    vid_w = vid_h = 0;
    chat->clear_all();
    comments->clear_all();
    related->status = "Cargando…";
    related->set({}, -1);
    if (!bg)
        header_title->set(clean(it.title));
    watch_title->set(clean(it.title));
    watch_author->label(nullptr);
    watch_author->copy_label(clean(it.author).c_str());
    watch_meta->set("");
    desc_buf->text("");
    quality->clear();
    audio_sel = 0;
    sub_sel = 0;
    sub_path.clear();
    video->text = it.plat == P_MUSIC ? "Preparando la canción…" : "Preparando…";
    video->cover = it.plat == P_MUSIC ? it.thumb : "";
    if (it.plat == P_MUSIC) {
        lyrics->clear_all("Buscando la letra…");
        queue_view->current = qpos;
        queue_view->show_current();
    }
    video->redraw();
    layout();
    update_controls(); // otherwise the time label keeps the previous video's (e.g. "EN VIVO")
    set_status("Obteniendo fuentes de " + std::string(plat_name(it.plat)) + "…");
    int g = watch_gen;
    backend_details(it.plat, it.url, [g](Details d) { on_details(d, g); });
}

void App::play_source(int idx, double start)
{
    if (idx < 0 || idx >= (int)cur.sources.size())
        return;
    cur_source = idx;
    quality->value(idx);
    const Source &s = cur.sources[idx];
    if (getenv("SANSON_DEBUG"))
        fprintf(stderr, "[play] kind=%d url='%s' label='%s'\n",
                (int)s.kind, s.url.c_str(), s.label.c_str());
    if (s.kind == S_HACKSTREAM) {
        sp_stop_hackstream();
    }
    sp_clear();
    PlayRequest r;
    r.sub = sub_path;
    r.live = cur.item.live;
    r.start = start;
    r.audio_only = cur.item.plat == P_MUSIC;
    end_handled = false;
    auto own = [](char *u) {
        std::string s = u ? u : "";
        free(u);
        return s;
    };
    if (s.kind == S_HLS) {
        int fl = cur.item.live ? SP_LIVE | (cur.item.plat == P_TWITCH ? SP_SKIP_ADS : 0) : 0;
        r.video = own(sp_add_hls(s.url.c_str(), fl, start));
        r.hls = true;
        if (!s.audio.empty())
            r.audio = own(sp_add_hls(s.audio.c_str(), fl | SP_AAC_TS, start));
    } else if (s.kind == S_RANGE) {
        r.video = own(sp_add_range(s.url.c_str(), s.vlen, "https://www.youtube.com"));
        if (!s.audio.empty())
            r.audio = own(sp_add_range(s.audio.c_str(), s.alen, "https://www.youtube.com"));
    } else if (s.kind == S_HACKSTREAM ||
               s.url.find("hackstream.io/watch/") != std::string::npos ||
               s.url.find("odysee.com/") != std::string::npos ||
               s.url.rfind("lbry://", 0) == 0) {
        r.video = own(sp_add_hackstream(s.url.c_str()));
        if (getenv("SANSON_DEBUG"))
            fprintf(stderr, "[play] hackstream_proxy -> %s\n", r.video.c_str());
        if (r.video.empty()) {
            video->text = "No se pudo iniciar hackstream_proxy";
            video->redraw();
            set_status("Error al resolver stream con hackstream_proxy");
            return;
        }
        if (!s.audio.empty())
            r.audio = own(sp_add_direct(s.audio.c_str()));
    } else {
        r.video = own(sp_add_direct(s.url.c_str()));
        if (!s.audio.empty())
            r.audio = own(sp_add_direct(s.audio.c_str()));
    }
    video->text = r.audio_only ? "" : "Conectando…";
    if (r.audio_only)
        set_status("Conectando…");
    video->redraw();
    Fl::flush();
    if (r.hls) {
        r.fmp4 = sp_hls_fmp4(s.url.c_str());
        if (!cur.item.live && cur.item.duration <= 0) {
            double d = sp_hls_duration(s.url.c_str());
            if (d > 0)
                cur.item.duration = d;
        }
    }
    player_play(fl_xid(video), r);
    set_status(std::string(plat_name(cur.item.plat)) + " · " + s.label);
    update_controls();
}

static std::string fmt_time(double t)
{
    int s = (int)t;
    char b[32];
    if (s >= 3600)
        snprintf(b, sizeof b, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else
        snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

void App::update_controls()
{
    if (player_ended() && !end_handled && cur_source >= 0) {
        end_handled = true;
        if (cur.item.plat == P_MUSIC)
            Fl::add_timeout(0.2, [](void *) { app->next_track(true); });
        else
            on_video_end();
    }
    if (cur.item.plat == P_MUSIC)
        lyrics->set_time((long long)(player_pos() * 1000), mono_now());
    if (!watch->visible())
        return;
    double len = cur.item.duration > 0 ? cur.item.duration : player_length();
    const char *pl = player_paused() || !player_running() ? "@>" : "@||";
    if (!play_btn->label() || strcmp(play_btn->label(), pl)) {
        play_btn->label(pl);
        play_btn->redraw();
    }
    double old_max = seek->max_, old_value = seek->value_;
    if (cur.item.live) {
        time_label->fg = C_RED;
        time_label->set("EN VIVO");
        seek->max_ = 0;
    } else {
        time_label->fg = C_TEXT;
        time_label->set(fmt_time(player_pos()) + " / " + (len > 0 ? fmt_time(len) : "--:--"));
        seek->max_ = len;
        if (Fl::pushed() != seek)
            seek->value_ = player_pos();
    }
    if (seek->max_ != old_max || (int)(seek->value_ * seek->w() / std::max(1.0, seek->max_)) !=
                                     (int)(old_value * seek->w() / std::max(1.0, seek->max_)))
        seek->redraw();
    if (countdown > 0)
        return; // the box shows what comes next
    if (player_running() && player_started() && !video->text.empty()) {
        video->text.clear();
        video->redraw();
    } else if (player_running() && !player_started()) {
        std::string st = sp_in_ad() ? "Saltando el anuncio de Twitch…" : player_status();
        if (!st.empty() && video->text != st) {
            video->text = st;
            video->redraw();
        }
    }
    if (!player_running() && cur_source >= 0 && video->text.empty()) {
        video->text = "Reproducción terminada";
        video->redraw();
    }
    int nw, nh;
    player_video_size(nw, nh);
    if (nw != vid_w || nh != vid_h) {
        vid_w = nw;
        vid_h = nh;
        if (nw > 0 && nh > 0 && cur_source >= 0 && cur_source < (int)cur.sources.size())
            set_status(std::string(plat_name(cur.item.plat)) + " · " + cur.sources[cur_source].label +
                       " · imagen recibida " + std::to_string(nw) + "×" + std::to_string(nh));
        layout(); // the picture box follows the aspect of the video, also in full screen
    }
    static bool ad_msg = false;
    bool ad = player_running() && player_started() && sp_in_ad();
    if (ad != ad_msg && cur_source >= 0 && cur_source < (int)cur.sources.size()) {
        ad_msg = ad;
        set_status(ad ? std::string("Twitch · saltando un anuncio, el directo vuelve en unos segundos…")
                      : std::string(plat_name(cur.item.plat)) + " · " + cur.sources[cur_source].label);
    }
}

void player_changed()
{
    app->update_controls();
}

void App::seek_rel(double d)
{
    if (cur.item.live || cur_source < 0)
        return;
    double len = cur.item.duration > 0 ? cur.item.duration : player_length();
    double t = std::max(0.0, player_pos() + d);
    if (len > 0 && t > len - 1)
        t = std::max(0.0, len - 1);
    if (cur.sources[cur_source].kind == S_HLS && (d > 0 || (player_pos() - t) > 20.0)) {
        wd_pos = -1;
        wd_since = mono_now();
        play_source(cur_source, t);
    } else {
        player_seek_abs(t);
    }
}

void App::stop_watch()
{
    if (music_bg) {
        music_bg = false;
        layout();
    }
    countdown = 0;
    stop_chat();
    player_stop();
    sp_clear();
    if (fullscreen)
        toggle_fullscreen();
}

void App::leave_watch()
{
    if (cur.item.plat == P_MUSIC && cur_source >= 0 && player_running()) {
        music_bg = true;
        if (fullscreen)
            toggle_fullscreen();
        return;
    }
    stop_watch();
}

void App::show_watch()
{
    music_bg = false;
    view = V_WATCH;
    if (Fl::focus() == search || Fl::focus() == chat_in) // the player keys go to the player
        Fl::focus(win);
    scroll->hide();
    settings->hide();
    watch->show();
    back_btn->show();
    sub_btn->hide();
    play_all_btn->hide();
    shuffle_all_btn->hide();
    header_title->set(clean(cur.item.title));
    select_nav(-1);
    queue_view->current = qpos;
    queue_view->show_current();
    layout();
}

// --- full screen -------------------------------------------------------------------------------------

static Window top_ancestor(Window w)
{
    Window root, parent, *kids;
    unsigned n;
    while (XQueryTree(fl_display, w, &root, &parent, &kids, &n)) {
        if (kids)
            XFree(kids);
        if (parent == root || !parent)
            break;
        w = parent;
    }
    return w;
}

// flwm can raise the Sansón window over the full screen one (focus, a click): the video then
// stayed full size behind it and it looked as if leaving full screen had failed.
static void fs_keep_on_top(void *)
{
    if (!app->fullscreen || !app->fs_win)
        return;
    Window frame = top_ancestor(fl_xid(app->win)), root, parent, *kids;
    unsigned n;
    if (XQueryTree(fl_display, RootWindow(fl_display, fl_screen), &root, &parent, &kids, &n)) {
        int ifs = -1, ifr = -1;
        for (unsigned i = 0; i < n; i++) {
            if (kids[i] == app->fs_win)
                ifs = (int)i;
            if (kids[i] == frame)
                ifr = (int)i;
        }
        if (kids)
            XFree(kids);
        if (ifs >= 0 && ifr > ifs) {
            XRaiseWindow(fl_display, app->fs_win);
            XFlush(fl_display);
        }
    }
    Fl::repeat_timeout(0.25, fs_keep_on_top);
}

// The video is an FLTK subwindow, which only selects Expose: its clicks go up to the parent.
// In full screen that parent is our own X window, so it selects the clicks and they are handled
// here; before, they reached the desktop (flwm opened its menu and the second click of a double
// click picked "Sansón" from it, raising the window over the video).
int fs_events(void *event, void *)
{
    XEvent *e = (XEvent *)event;
    if (!app->fullscreen || !app->fs_win || e->xany.window != app->fs_win)
        return 0;
    if (e->type == ButtonPress && e->xbutton.button == Button1) {
        static Time last = 0;
        bool dbl = last && e->xbutton.time - last < 400;
        last = dbl ? 0 : e->xbutton.time;
        if (dbl) {
            if (app->video->on_double)
                app->video->on_double();
        } else if (app->video->on_click) {
            app->video->on_click();
        }
    }
    return 1;
}

void App::toggle_fullscreen()
{
    Window vid = fl_xid(video);
    if (!fullscreen) {
        int sx, sy;
        Fl::screen_xywh(sx, sy, fs_w, fs_h, win->x() + win->w() / 2, win->y() + win->h() / 2);
        XSetWindowAttributes at;
        at.override_redirect = True;
        at.background_pixel = BlackPixel(fl_display, fl_screen);
        fs_win = XCreateWindow(fl_display, RootWindow(fl_display, fl_screen), sx, sy, fs_w, fs_h, 0, CopyFromParent,
                               InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel, &at);
        XSelectInput(fl_display, fs_win, ButtonPressMask | ButtonReleaseMask);
        XMapRaised(fl_display, fs_win);
        XReparentWindow(fl_display, vid, fs_win, 0, 0);
        fullscreen = true;
        layout();
        XSetInputFocus(fl_display, fl_xid(win), RevertToParent, CurrentTime);
        Fl::add_timeout(0.25, fs_keep_on_top);
    } else {
        Fl::remove_timeout(fs_keep_on_top);
        XReparentWindow(fl_display, vid, fl_xid(win), video->x(), video->y());
        XDestroyWindow(fl_display, fs_win);
        fs_win = 0;
        fullscreen = false;
        layout();
    }
    XFlush(fl_display);
    video->redraw();
}

// --- build -----------------------------------------------------------------------------------------

static FlatButton *small_button(int w, const char *l, const char *tip = nullptr)
{
    auto *b = new FlatButton(0, 0, w, 26, l);
    b->font_size = 11;
    b->sel_color = C_SEL;
    if (tip)
        b->tooltip(tip);
    return b;
}

static void volume(int d)
{
    player_volume(d);
    app->set_status("Volumen: " + std::to_string(player_get_volume()) + " %");
}

// The controls show while the pointer is over the video (and for a while after it moves), and
// stay when nothing is playing (paused, loading), like YouTube's. Hiding a child window destroys
// its X window, so it is only done when the state changes.
static void ovl_tick(void *)
{
    Fl::repeat_timeout(0.1, ovl_tick);
    if (!app || !app->ready)
        return;
    bool want = false;
    if (app->view == V_WATCH && !app->fullscreen && app->watch->visible()) {
        static int lx = -1, ly = -1;
        static int quiet = 0; // ticks without the pointer moving
        int mx, my;
        Fl::get_mouse(mx, my);
        quiet = mx != lx || my != ly ? 0 : quiet + 1;
        lx = mx;
        ly = my;
        int rx = app->win->x_root() + app->video_bg->x(), ry = app->win->y_root() + app->video_bg->y();
        bool inside = mx >= rx && my >= ry && mx < rx + app->video_bg->w() && my < ry + app->video_bg->h();
        bool playing = player_running() && player_started() && !player_paused() && !player_ended();
        want = (inside && (!playing || quiet < 25)) || Fl::grab() || Fl::pushed() == app->seek;
        if (!inside && !playing && player_running())
            want = true; // paused: the controls stay
    }
    if (want && !app->ovl->visible())
        app->ovl->show();
    else if (!want && app->ovl->visible())
        app->ovl->hide();
}

void App::build_watch()
{
    int W = win->w(), H = win->h();
    watch = new Fl_Group(118, 70, W - 118, H - 90);
    watch->box(FL_FLAT_BOX);
    watch->color(C_BG);
    video_bg = new Fl_Box(0, 0, 320, 180);
    video_bg->box(FL_FLAT_BOX);
    video_bg->color(FL_BLACK);
    video = new VideoBox(0, 0, 320, 180);
    // In full screen one click pauses, but only once it is clear it was not the first half of
    // a double click (which leaves full screen): that used to pause the video on the way out.
    static auto pause_cb = [](void *) { player_pause(); };
    video->on_double = [] {
        Fl::remove_timeout(pause_cb);
        if (app->cur.item.plat != P_MUSIC || app->fullscreen)
            app->toggle_fullscreen();
    };
    video->on_click = [] {
        if (Fl::focus() == app->search || Fl::focus() == app->chat_in)
            Fl::focus(app->win);
        if (app->countdown > 0)
            app->cancel_countdown();
        else if (app->fullscreen)
            Fl::add_timeout(0.4, pause_cb);
    };
    // The controls live in a window of their own over the bottom of the video (a child window:
    // the picture is one too, and nothing else can be drawn over it).
    ovl = new Fl_Window(0, 0, 100, 46);
    ovl->box(FL_FLAT_BOX);
    ovl->color(C_BAR);
    play_btn = new FlatButton(0, 0, 40, 26, "@>");
    play_btn->symbol = true;
    play_btn->tooltip("Pausa (espacio)");
    play_btn->callback([](Fl_Widget *, void *) {
        if (player_running())
            player_pause();
        else if (app->cur_source >= 0)
            app->play_source(app->cur_source, 0);
    });
    back10 = small_button(34, "-10", "Atrás 10 s (←)");
    back10->callback([](Fl_Widget *, void *) { app->seek_rel(-10); });
    fwd10 = small_button(34, "+10", "Adelante 10 s (→)");
    fwd10->callback([](Fl_Widget *, void *) { app->seek_rel(10); });
    time_label = new Label(0, 0, 118, 26);
    time_label->size = 11;
    time_label->al = FL_ALIGN_CENTER;
    seek = new Seek(0, 0, 100, 12);
    seek->on_seek = [](double t) {
        if (app->cur_source < 0)
            return;
        if (app->cur.sources[app->cur_source].kind == S_HLS &&
            (t > player_pos() || (player_pos() - t) > 20.0))
            app->play_source(app->cur_source, t);
        else
            player_seek_abs(t);
    };
    vol_down = small_button(28, "V-", "Bajar volumen (↓)");
    vol_down->callback([](Fl_Widget *, void *) { volume(-10); });
    vol_up = small_button(28, "V+", "Subir volumen (↑)");
    vol_up->callback([](Fl_Widget *, void *) { volume(10); });
    quality = new FlatChoice(0, 0, 104, 26);
    quality->tooltip("Calidad");
    quality->callback([](Fl_Widget *w, void *) {
        int v = ((Fl_Choice *)w)->value();
        if (v != app->cur_source)
            app->play_source(v, app->cur.item.live ? 0 : player_pos());
    });
    lang_btn = small_button(62, "Idioma", "Idioma del audio y subtítulos");
    lang_btn->hide();
    lang_btn->callback([](Fl_Widget *, void *) { app->lang_menu(); });
    auto_btn = small_button(94, "Continua", "Reproducción continua: al terminar sigue con el próximo video");
    auto_btn->icon = I_LOOP;
    auto_btn->callback([](Fl_Widget *, void *) {
        bool on = !app->autoplay();
        set_setting("autoplay", on ? "1" : "0");
        if (!on)
            app->cancel_countdown();
        app->set_status(on ? "Reproducción continua activada" : "Reproducción continua desactivada");
        app->layout();
    });
    panel_btn = small_button(58, "Panel", "Mostrar u ocultar el panel (C)");
    panel_btn->callback([](Fl_Widget *, void *) { app->toggle_panel(); });
    prev_btn = small_button(34, nullptr, "Anterior");
    prev_btn->icon = I_PREV;
    prev_btn->callback([](Fl_Widget *, void *) { app->prev_track(); });
    next_btn = small_button(34, nullptr, "Siguiente (N)");
    next_btn->icon = I_NEXT;
    next_btn->callback([](Fl_Widget *, void *) { app->play_next(); });
    full_btn = small_button(64, "Pantalla", "Pantalla completa (F, doble clic)");
    full_btn->callback([](Fl_Widget *, void *) { app->toggle_fullscreen(); });
    ovl->end();
    ovl->hide();

    dl_btn = small_button(92, "Descargar", "Descargar el video o el audio");
    dl_btn->icon = I_DOWNLOAD;
    dl_btn->hide();
    dl_btn->callback([](Fl_Widget *, void *) { download_dialog(); });

    watch_title = new Label(0, 0, 100, 22);
    watch_title->size = 14;
    watch_author = new FlatButton(0, 0, 200, 24);
    watch_author->align_left = 4;
    watch_author->fg = C_ACCENT;
    watch_author->tooltip("Ir al canal");
    watch_author->callback([](Fl_Widget *, void *) {
        if (!app->cur.item.author_url.empty())
            app->open_channel(app->cur.item.plat, app->cur.item.author_url, app->cur.item.author);
    });
    subscribe_watch = new FlatButton(0, 0, 96, 24, "Seguir");
    subscribe_watch->sel_color = C_GREEN;
    subscribe_watch->callback([](Fl_Widget *, void *) {
        const Item &it = app->cur.item;
        if (it.author_url.empty())
            return;
        Item c;
        c.plat = it.plat;
        c.kind = "channel";
        c.url = it.author_url;
        c.title = it.author;
        c.author = c.title;
        c.author_url = c.url;
        c.thumb = it.author_thumb;
        subs_toggle(c);
        bool s = subs_has(c.url);
        app->subscribe_watch->label(s ? "Siguiendo" : "Seguir");
        app->subscribe_watch->set_selected(s);
    });
    watch_meta = new Label(0, 0, 100, 24);
    watch_meta->font = FL_HELVETICA;
    watch_meta->size = 12;
    watch_meta->fg = C_DIM;
    shuffle_btn = small_button(74, "Aleatorio");
    shuffle_btn->callback([](Fl_Widget *, void *) { app->set_shuffle(!app->shuffle); });
    repeat_btn = small_button(90, "Repetir: no");
    repeat_btn->callback([](Fl_Widget *, void *) {
        app->repeat_mode = (app->repeat_mode + 1) % 3;
        app->music_buttons();
        app->queue_save();
    });
    radio_btn = small_button(60, "Radio", "Al terminar la cola sigue con canciones parecidas");
    radio_btn->callback([](Fl_Widget *, void *) {
        app->radio = !app->radio;
        app->music_buttons();
        app->queue_save();
    });
    desc_buf = new Fl_Text_Buffer();
    desc = new DarkText(0, 0, 100, 100);
    desc->buffer(desc_buf);
    desc->box(FL_FLAT_BOX);
    desc->color(C_PANEL);
    desc->textcolor(C_TEXT);
    desc->textfont(FL_HELVETICA);
    desc->textsize(12);
    desc->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    desc->scrollbar_align(FL_ALIGN_RIGHT);

    panel_tabs = new Bar(0, 0, 100, 26, C_BAR);
    for (int t = 0; t < 2; t++) {
        tab_btn[t] = new FlatButton(0, 0, 50, 26);
        tab_btn[t]->bg = C_BAR;
        tab_btn[t]->font_size = 11;
        tab_btn[t]->callback([](Fl_Widget *, void *d) { app->select_tab((int)(long)d); }, (void *)(long)t);
    }
    panel_tabs->end();
    chat = new ChatView(0, 0, 280, 300);
    comments = new CommentView(0, 0, 280, 300);
    comments->on_more = [] { app->load_comments(); };
    related = new RelatedView(0, 0, 280, 300);
    related->on_open = [](int i) {
        Item it = app->related->items[i];
        app->open_item(it);
    };
    lyrics = new LyricsView(0, 0, 280, 300);
    lyrics->on_seek = [](double t) {
        if (app->cur_source >= 0 && (app->cur.sources[app->cur_source].kind != S_HLS || t <= player_pos()))
            player_seek_abs(t);
        else if (app->cur_source >= 0)
            app->play_source(app->cur_source, t);
    };
    queue_view = new QueueView(0, 0, 280, 300);
    queue_view->items = &queue;
    queue_view->on_play = [](int r) { app->play_queue(r); };
    queue_view->on_menu = [](int r, int a) { app->queue_menu(r, a); };
    chat_in = new ClickInput(0, 0, 200, 26); // only a click: the player keys must not go to the chat
    style_input(chat_in);
    chat_in->maximum_size(480);
    chat_in->when(FL_WHEN_ENTER_KEY_ALWAYS);
    chat_in->callback([](Fl_Widget *, void *) { app->send_chat(); });
    chat_send_btn = new FlatButton(0, 0, 62, 26, "Enviar");
    chat_send_btn->bg = C_SEL;
    chat_send_btn->callback([](Fl_Widget *, void *) { app->send_chat(); });
    chat_hint = new Label(0, 0, 200, 26);
    chat_hint->font = FL_HELVETICA;
    chat_hint->size = 11;
    chat_hint->fg = C_DIM;
    hide_panel(this);
    watch->end();
    watch->hide();
    Fl::add_timeout(0.1, ovl_tick);
}
