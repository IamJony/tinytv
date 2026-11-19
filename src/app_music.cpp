// Música: the queue (saved between sessions), shuffle, repeat, and the radio that continues
// with similar songs when the queue ends.

#include "app.h"

#include <algorithm>
#include <cstdlib>

#include "player.h"

static bool same_song(const Item &a, const Item &b)
{
    return !a.id.empty() ? a.id == b.id : a.url == b.url;
}

void App::music_buttons()
{
    static const char *rl[3] = {"Repetir: no", "Repetir: todo", "Repetir: una"};
    shuffle_btn->selected = shuffle;
    repeat_btn->label(rl[repeat_mode]);
    repeat_btn->selected = repeat_mode != 0;
    radio_btn->selected = radio;
    std::string m;
    if (shuffle)
        m = "aleatorio";
    if (repeat_mode)
        m += (m.empty() ? "" : ", ") + std::string(repeat_mode == 1 ? "repetir todo" : "repetir una");
    queue_view->mode = m;
    for (Fl_Widget *wd : {(Fl_Widget *)shuffle_btn, (Fl_Widget *)repeat_btn, (Fl_Widget *)radio_btn,
                          (Fl_Widget *)queue_view})
        wd->redraw();
}

void App::queue_save()
{
    music_queue_save(queue);
    set_setting("music_pos", std::to_string(qpos));
    set_setting("music_repeat", std::to_string(repeat_mode));
    set_setting("music_shuffle", shuffle ? "1" : "0");
    set_setting("music_radio", radio ? "1" : "0");
}

void App::queue_load()
{
    queue = music_queue_list();
    queue_orig = queue;
    qpos = std::min(atoi(setting("music_pos", "0").c_str()), (int)queue.size() - 1);
    repeat_mode = std::max(0, std::min(2, atoi(setting("music_repeat", "0").c_str())));
    shuffle = setting("music_shuffle", "0") == "1";
    radio = setting("music_radio", "1") == "1";
    queue_view->current = qpos;
    music_buttons();
}

// Songs after `from` get a new random order (the ones already heard stay where they are).
static void shuffle_after(std::vector<Item> &q, int from)
{
    for (int i = (int)q.size() - 1; i > from + 1; i--) {
        int j = from + 1 + rand() % (i - from);
        std::swap(q[i], q[j]);
    }
}

void App::play_list(const std::vector<Item> &list, int idx, bool shuffled)
{
    if (list.empty())
        return;
    if (shuffled)
        shuffle = true;
    queue_orig = list;
    queue = list;
    idx = std::max(0, std::min(idx, (int)list.size() - 1));
    if (shuffle) {
        // The chosen song first, the rest in random order.
        std::swap(queue[0], queue[idx]);
        shuffle_after(queue, 0);
        idx = 0;
    }
    music_buttons();
    play_queue(idx);
}

void App::play_queue(int i)
{
    if (i < 0 || i >= (int)queue.size())
        return;
    qpos = i;
    queue_view->current = i;
    queue_save();
    open_item(queue[i]);
}

void App::set_shuffle(bool on)
{
    shuffle = on;
    if (on) {
        queue_orig = queue;
        shuffle_after(queue, qpos);
    } else if (!queue.empty()) {
        // Back to the original order: the songs still in the queue, then the ones added later.
        Item now = qpos >= 0 && qpos < (int)queue.size() ? queue[qpos] : Item();
        std::vector<Item> q;
        for (auto &o : queue_orig)
            for (auto &x : queue)
                if (same_song(o, x)) {
                    q.push_back(x);
                    break;
                }
        for (auto &x : queue)
            if (std::none_of(q.begin(), q.end(), [&](const Item &y) { return same_song(x, y); }))
                q.push_back(x);
        queue = q;
        for (int k = 0; k < (int)queue.size(); k++)
            if (same_song(queue[k], now))
                qpos = k;
    }
    queue_view->current = qpos;
    queue_view->show_current();
    music_buttons();
    queue_save();
}

void App::next_track(bool automatic)
{
    if (automatic && repeat_mode == 2 && cur_source >= 0) {
        play_source(cur_source, 0);
        return;
    }
    if (qpos + 1 < (int)queue.size()) {
        play_queue(qpos + 1);
        return;
    }
    if (repeat_mode == 1 && !queue.empty()) {
        if (shuffle)
            shuffle_after(queue, -1);
        play_queue(0);
        return;
    }
    if (radio && !radio_busy && cur.item.plat == P_MUSIC && !cur.item.url.empty()) {
        // End of the queue: songs like the last one (YouTube Music radio).
        radio_busy = true;
        set_status("Fin de la cola: buscando canciones parecidas…");
        int g = watch_gen;
        backend_radio(cur.item.url, [g](Page pg) {
            app->radio_busy = false;
            backend_drop(P_MUSIC, pg.pager);
            if (g != app->watch_gen)
                return;
            int added = 0;
            for (auto &it : pg.items) {
                if (it.kind != "video")
                    continue;
                if (std::none_of(app->queue.begin(), app->queue.end(), [&](const Item &q) { return same_song(q, it); })) {
                    app->queue.push_back(it);
                    app->queue_orig.push_back(it);
                    added++;
                }
            }
            if (added && app->qpos + 1 < (int)app->queue.size()) {
                app->play_queue(app->qpos + 1);
            } else {
                app->set_status(pg.error.empty() ? "Fin de la cola" : "Radio no disponible: " + pg.error);
                app->queue_view->redraw();
            }
        });
        return;
    }
    set_status("Fin de la cola");
}

void App::prev_track()
{
    if (player_pos() > 5 || qpos <= 0) {
        if (cur_source >= 0) {
            if (cur.sources[cur_source].kind == S_HLS || player_ended())
                play_source(cur_source, 0);
            else
                player_seek_abs(0);
        }
        return;
    }
    play_queue(qpos - 1);
}

void App::queue_add(const std::vector<Item> &list, bool next)
{
    if (list.empty())
        return;
    if (queue.empty()) {
        play_list(list, 0, false);
        return;
    }
    int at = next ? std::min(qpos + 1, (int)queue.size()) : (int)queue.size();
    queue.insert(queue.begin() + std::max(0, at), list.begin(), list.end());
    queue_orig.insert(queue_orig.end(), list.begin(), list.end());
    queue_view->redraw();
    queue_save();
    std::string what = list.size() == 1 ? "«" + clean(list[0].title) + "»" : std::to_string(list.size()) + " canciones";
    set_status(what + (next ? " suena a continuación" : " agregada" + std::string(list.size() == 1 ? "" : "s") + " a la cola"));
    layout(); // "Retomar la cola"
}

void App::queue_menu(int row, int action)
{
    if (row < 0 || row >= (int)queue.size())
        return;
    switch (action) {
    case 0:
        play_queue(row);
        return;
    case 1:
    case 2: {
        int to = action == 1 ? row - 1 : row + 1;
        if (to < 0 || to >= (int)queue.size())
            return;
        std::swap(queue[row], queue[to]);
        if (qpos == row)
            qpos = to;
        else if (qpos == to)
            qpos = row;
        break;
    }
    case 3: {
        Item gone = queue[row];
        queue.erase(queue.begin() + row);
        for (size_t k = 0; k < queue_orig.size(); k++)
            if (same_song(queue_orig[k], gone)) {
                queue_orig.erase(queue_orig.begin() + k);
                break;
            }
        if (row <= qpos)
            qpos--; // the song keeps playing; "next" goes to the one that followed it
        break;
    }
    case 4:
        queue.clear();
        queue_orig.clear();
        qpos = -1;
        break;
    }
    queue_view->current = qpos;
    queue_view->redraw();
    queue_save();
    layout();
}

void App::with_songs(const Item &it, std::function<void(std::vector<Item>)> f)
{
    if (it.kind == "video") {
        f({it});
        return;
    }
    set_status("Cargando las canciones de «" + clean(it.title) + "»…");
    backend_channel_videos(P_MUSIC, it.url, [f](Page pg) {
        backend_drop(P_MUSIC, pg.pager);
        std::vector<Item> songs;
        for (auto &x : pg.items)
            if (x.kind == "video")
                songs.push_back(x);
        if (songs.empty()) {
            app->set_status(pg.error.empty() ? "No hay canciones" : pg.error);
            return;
        }
        f(songs);
    });
}

// Right click on a card of Música.
void App::music_menu(int i)
{
    if (i < 0 || i >= (int)grid->items.size() || grid->items[i].plat != P_MUSIC)
        return;
    Item it = grid->items[i];
    bool song = it.kind == "video";
    Fl_Menu_Item m[] = {{"Reproducir"},
                        {"Reproducir a continuación"},
                        {"Agregar a la cola", 0, 0, 0, FL_MENU_DIVIDER},
                        {"Ir al álbum", 0, 0, 0, song && !it.album_url.empty() ? 0 : FL_MENU_INACTIVE},
                        {"Ir al artista", 0, 0, 0, song && !it.author_url.empty() ? 0 : FL_MENU_INACTIVE},
                        {nullptr}};
    const Fl_Menu_Item *pick = m->popup(Fl::event_x(), Fl::event_y());
    if (!pick)
        return;
    int a = (int)(pick - m);
    if (a == 0)
        with_songs(it, [](std::vector<Item> v) { app->play_list(v, 0, false); });
    else if (a == 1 || a == 2)
        with_songs(it, [a](std::vector<Item> v) { app->queue_add(v, a == 1); });
    else if (a == 3)
        open_channel(P_MUSIC, it.album_url, it.album);
    else if (a == 4)
        open_channel(P_MUSIC, it.author_url, it.author);
}
