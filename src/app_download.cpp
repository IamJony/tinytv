// Downloads of YouTube videos and Música songs, without outside programs (download.c fetches
// and joins the files). The «Descargar» button of the player opens the dialog (kind, quality,
// name, directory, connections); the Descargas page shows the progress of each one.

#include "app.h"
void hide_store_view();

#include <FL/Fl_File_Chooser.H>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include "download.h"
#include "stream.h"

struct Download {
    std::string name, path, info;
    dl_job *job = nullptr;
    dl_status st{};
    double last_t = 0, speed = 0;
    long long last_done = 0;
    bool notified = false;
};

static std::vector<Download *> downloads; // the newest first

static bool running(const Download *d)
{
    return d->st.state == DL_RUNNING || d->st.state == DL_JOINING;
}

static std::string fmt_size(long long b)
{
    char t[32];
    if (b >= 1024LL * 1024 * 1024)
        snprintf(t, sizeof t, "%.2f GB", b / 1073741824.0);
    else if (b >= 1024 * 1024)
        snprintf(t, sizeof t, "%.1f MB", b / 1048576.0);
    else
        snprintf(t, sizeof t, "%lld KB", (b + 1023) / 1024);
    std::string s = t;
    size_t dot = s.find('.');
    if (dot != std::string::npos)
        s[dot] = ',';
    return s;
}

static bool url_is_webm(const std::string &u)
{
    return u.find("mime=video%2Fwebm") != std::string::npos || u.find("mime=audio%2Fwebm") != std::string::npos ||
           u.find("mime=video/webm") != std::string::npos || u.find("mime=audio/webm") != std::string::npos;
}

static bool is_range(const std::string &u)
{
    return u.find("googlevideo.com") != std::string::npos;
}

// --- what can be downloaded -------------------------------------------------------------------------

struct DlOption {
    std::string label, info, ext, video, audio;
    long long vlen = 0, alen = 0;
    int container = DL_MP4;
};

static const AudioTrack *audio_of(const Details &d, bool webm)
{
    for (auto &a : d.dl_audio)
        if (url_is_webm(a.url) == webm)
            return &a;
    return nullptr;
}

static std::vector<DlOption> video_options(const Details &d, int &current)
{
    std::vector<DlOption> v;
    current = 0;
    if (d.item.plat != P_YOUTUBE)
        return v;
    for (size_t i = 0; i < d.sources.size(); i++) {
        const Source &s = d.sources[i];
        if (s.kind != S_RANGE || !is_range(s.url) || s.label == "Solo audio")
            continue;
        bool webm = url_is_webm(s.url);
        const AudioTrack *a = audio_of(d, webm);
        if (!a)
            continue;
        DlOption o;
        o.video = s.url;
        o.vlen = s.vlen > 0 ? s.vlen : sp_url_clen(s.url.c_str());
        o.audio = a->url;
        o.alen = a->len;
        o.container = webm ? DL_WEBM : DL_MP4;
        o.ext = webm ? ".webm" : ".mp4";
        std::string q = s.label;
        size_t sp = q.find(' '); // "1080p VP9" → "1080p"
        if (sp != std::string::npos)
            q = q.substr(0, sp);
        o.info = q + (webm ? " · WebM (VP9)" : s.label.find("AV1") != std::string::npos ? " · MP4 (AV1)" : " · MP4");
        o.label = o.info + (o.vlen > 0 && o.alen > 0 ? " · " + fmt_size(o.vlen + o.alen) : "");
        if ((int)i == app->cur_source)
            current = (int)v.size();
        v.push_back(o);
    }
    return v;
}

static std::vector<DlOption> audio_options(const Details &d)
{
    std::vector<DlOption> v;
    std::vector<AudioTrack> tracks = d.dl_audio;
    if (d.item.plat == P_MUSIC) { // Música: its sources are the audio (AAC and Opus)
        tracks.clear();
        for (auto &s : d.sources)
            if (s.kind == S_RANGE && is_range(s.url)) {
                AudioTrack t;
                t.label = s.label;
                t.url = s.url;
                t.len = s.vlen > 0 ? s.vlen : sp_url_clen(s.url.c_str());
                tracks.push_back(t);
            }
    }
    for (auto &t : tracks) {
        DlOption o;
        bool webm = url_is_webm(t.url);
        o.audio = t.url;
        o.alen = t.len;
        o.container = webm ? DL_WEBM : DL_MP4;
        o.ext = webm ? ".webm" : ".m4a";
        o.info = t.label + (webm ? " · WebM" : " · M4A");
        o.label = o.info + (t.len > 0 ? " · " + fmt_size(t.len) : "");
        v.push_back(o);
    }
    return v;
}

bool can_download(const Details &d)
{
    if (d.item.live || (d.item.plat != P_YOUTUBE && d.item.plat != P_MUSIC))
        return false;
    int cur;
    return !video_options(d, cur).empty() || !audio_options(d).empty();
}

// --- names and places -------------------------------------------------------------------------------

// A file name from a title: no '/', no control characters, not too long.
static std::string safe_name(const std::string &title)
{
    std::string o;
    for (unsigned char c : clean(title)) {
        if (c < 32 || c == 127 || c == '/' || c == '\\')
            c = c == '/' || c == '\\' ? '-' : ' ';
        if (c == ' ' && (o.empty() || o.back() == ' '))
            continue;
        o += (char)c;
    }
    while (!o.empty() && (o.back() == ' ' || o.back() == '.'))
        o.pop_back();
    while (!o.empty() && o[0] == '.')
        o.erase(0, 1);
    if (o.size() > 180) { // cut on a character boundary
        size_t n = 180;
        while (n > 0 && ((unsigned char)o[n] & 0xC0) == 0x80)
            n--;
        o.resize(n);
    }
    return o;
}

static std::string default_dir()
{
    std::string d = setting("dl_dir");
    struct stat st;
    if (!d.empty() && stat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        return d;
    const char *h = getenv("HOME");
    std::string home = h && *h ? h : "/tmp";
    for (const char *sub : {"/Downloads", "/Descargas"})
        if (stat((home + sub).c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            return home + sub;
    return home;
}

static bool taken(const std::string &p)
{
    for (auto *d : downloads)
        if (d->path == p && running(d))
            return true;
    return access(p.c_str(), F_OK) == 0 || access((p + ".part").c_str(), F_OK) == 0;
}

// dir/name.ext, or dir/name (2).ext if that one exists...
static std::string free_path(const std::string &dir, const std::string &name, const std::string &ext)
{
    std::string base = dir + (dir.empty() || dir.back() != '/' ? "/" : "") + name;
    std::string p = base + ext;
    for (int i = 2; taken(p) && i < 1000; i++)
        p = base + " (" + std::to_string(i) + ")" + ext;
    return p;
}

// --- the Descargas page -----------------------------------------------------------------------------

class DownloadList : public Fl_Widget {
public:
    static const int RH = 78, GAP = 8, BW = 92;
    DownloadList(int x, int y, int w) : Fl_Widget(x, y, w, content_h()) {}
    static int content_h() { return std::max(1, (int)downloads.size() * (RH + GAP)); }
    void draw() override;
    int handle(int e) override;
};

static DownloadList *dl_list;

static std::string fmt_eta(double s)
{
    int t = (int)(s + 0.5);
    if (t >= 3600)
        return std::to_string(t / 3600) + " h " + std::to_string(t % 3600 / 60) + " min";
    if (t >= 60)
        return std::to_string(t / 60) + " min " + std::to_string(t % 60) + " s";
    return std::to_string(t) + " s";
}

void DownloadList::draw()
{
    fl_push_clip(x(), y(), w(), h());
    fl_rectf(x(), y(), w(), h(), C_BG);
    for (size_t i = 0; i < downloads.size(); i++) {
        const Download *d = downloads[i];
        int ry = y() + (int)i * (RH + GAP);
        int st = d->st.state;
        Fl_Color c = st == DL_DONE ? C_GREEN : st == DL_ERROR ? C_RED : st == DL_CANCELLED ? C_DIM : C_ACCENT;
        fl_rectf(x(), ry, w(), RH, C_PANEL);
        fl_rectf(x(), ry, 4, RH, c);
        int tw = w() - 16 - BW - 24;
        fl_font(FL_HELVETICA_BOLD, 13);
        fl_color(C_TEXT);
        fl_draw(fit(d->name, tw).c_str(), x() + 16, ry + 8, tw, 20, FL_ALIGN_LEFT, nullptr, 0);
        std::string line;
        long long done = d->st.done, total = d->st.total;
        if (st == DL_RUNNING) {
            line = d->info + " · " + (total > 0 ? fmt_size(done) + " de " + fmt_size(total) : "conectando…");
            if (d->speed > 1024 && total > 0)
                line += " · " + fmt_size((long long)d->speed) + "/s · quedan " + fmt_eta((total - done) / d->speed);
        } else if (st == DL_JOINING) {
            line = d->info + " · uniendo video y audio…";
        } else if (st == DL_DONE) {
            line = "Guardado en " + d->path;
        } else if (st == DL_ERROR) {
            line = "Error: " + std::string(d->st.error);
        } else {
            line = "Cancelada";
        }
        fl_font(FL_HELVETICA, 11);
        fl_color(st == DL_ERROR ? C_RED : C_DIM);
        fl_draw(fit(line, tw).c_str(), x() + 16, ry + 30, tw, 18, FL_ALIGN_LEFT, nullptr, 0);
        int bx = x() + 16, bw = tw, by = ry + 56;
        fl_rectf(bx, by, bw, 6, C_BAR);
        double f = st == DL_DONE || st == DL_JOINING ? 1.0 : total > 0 ? (double)done / total : 0;
        if (st != DL_CANCELLED)
            fl_rectf(bx, by, (int)(bw * std::min(1.0, std::max(0.0, f))), 6, c);
        if (st != DL_JOINING) { // the button: Cancelar while it downloads, Quitar after
            int kx = x() + w() - 12 - BW, ky = ry + (RH - 28) / 2;
            fl_rectf(kx, ky, BW, 28, st == DL_RUNNING ? fl_rgb_color(120, 52, 52) : C_BTN);
            fl_font(FL_HELVETICA_BOLD, 12);
            fl_color(C_TEXT);
            fl_draw(st == DL_RUNNING ? "Cancelar" : "Quitar", kx, ky, BW, 28, FL_ALIGN_CENTER, nullptr, 0);
        }
    }
    fl_pop_clip();
}

int DownloadList::handle(int e)
{
    if (e != FL_PUSH || Fl::event_button() != FL_LEFT_MOUSE)
        return Fl_Widget::handle(e);
    int i = (Fl::event_y() - y()) / (RH + GAP);
    if (i < 0 || i >= (int)downloads.size())
        return 1;
    Download *d = downloads[i];
    int ry = y() + i * (RH + GAP);
    int kx = x() + w() - 12 - BW, ky = ry + (RH - 28) / 2;
    if (Fl::event_x() < kx || Fl::event_x() >= kx + BW || Fl::event_y() < ky || Fl::event_y() >= ky + 28)
        return 1;
    if (d->st.state == DL_RUNNING) {
        dl_cancel(d->job);
        app->set_status("Cancelando «" + d->name + "»…");
    } else if (d->st.state != DL_JOINING) {
        dl_free(d->job);
        delete d;
        downloads.erase(downloads.begin() + i);
        downloads_layout();
        app->settings->redraw();
    }
    return 1;
}

static void nav_label()
{
    int n = 0;
    for (auto *d : downloads)
        n += running(d);
    std::string l = n ? "Descargas (" + std::to_string(n) + ")" : "Descargas";
    app->nav[NAV_DOWNLOADS]->copy_label(l.c_str());
    app->nav[NAV_DOWNLOADS]->redraw();
}

static void dl_tick(void *)
{
    double now = mono_now();
    bool any = false;
    for (auto *d : downloads) {
        if (!running(d))
            continue;
        dl_get(d->job, &d->st);
        double dt = now - d->last_t;
        if (dt > 0.2) {
            double inst = (d->st.done - d->last_done) / dt;
            d->speed = d->speed > 0 ? d->speed * 0.7 + inst * 0.3 : inst;
            d->last_t = now;
            d->last_done = d->st.done;
        }
        if (running(d)) {
            any = true;
        } else if (!d->notified) {
            d->notified = true;
            if (d->st.state == DL_DONE)
                app->set_status("Descarga lista: " + d->path);
            else if (d->st.state == DL_ERROR)
                app->set_status("No se pudo descargar «" + d->name + "»: " + d->st.error);
            else
                app->set_status("Descarga cancelada: «" + d->name + "»");
        }
    }
    nav_label();
    if (dl_list && app->view == V_DOWNLOADS)
        dl_list->redraw();
    if (any)
        Fl::repeat_timeout(0.5, dl_tick);
}

// Llamada al salir de la página de Descargas (antes de settings->clear() de otra vista).
// El DownloadList va a ser destruido por settings->clear(), así que dl_list queda colgante:
// lo ponemos a nullptr para que downloads_layout() no lo toque.
void clear_downloads_view()
{
    dl_list = nullptr;
}

void downloads_layout()
{
    // dl_list es static y sobrevive entre vistas, pero settings->clear() lo destruye.
    // Solo es válido si estamos en la página de Descargas y el widget sigue vivo.
    if (!app || !app->ready || app->view != V_DOWNLOADS || !dl_list)
        return;
    // Comprobar que dl_list sigue siendo hijo de settings (no fue destruido por clear()).
    if (dl_list->parent() != app->settings)
        return;
    dl_list->resize(dl_list->x(), dl_list->y(), app->settings->w() - 52, DownloadList::content_h());
}

void App::show_downloads()
{
    hide_store_view();
    // 1) Marcar que ya no tenemos un dl_list válido ANTES de tocar settings.
    dl_list = nullptr;
    // 2) Salir de la vista actual para que cualquier layout() intermedio no toque descargas.
    view = V_DOWNLOADS;
    scroll->hide();
    watch->hide();
    back_btn->hide();
    sub_btn->hide();
    play_all_btn->hide();
    shuffle_all_btn->hide();
    select_nav(NAV_DOWNLOADS);
    header_title->set("Descargas");
    // 3) Reconstruir la página de descargas ANTES de llamar a layout(),
    //    así layout() → downloads_layout() encontrará un dl_list válido y recién creado.
    settings->clear();
    settings->scroll_to(0, 0);
    settings->begin();
    int x0 = settings->x() + 20, y = settings->y() + 12, W = settings->w() - 52;
    auto *t = new Label(x0, y, W, 34);
    t->font = FL_HELVETICA;
    t->size = 11;
    t->fg = C_DIM;
    t->al = FL_ALIGN_LEFT | FL_ALIGN_WRAP | FL_ALIGN_TOP;
    t->set(downloads.empty()
               ? "Todavía no hay descargas. Abre un video de YouTube o una canción de Música y usa el botón "
                 "«Descargar» del reproductor."
               : "Las descargas siguen mientras navegas. Si sales de Sansón, las que no terminaron se cancelan.");
    y += 40;
    dl_list = new DownloadList(x0, y, W);
    auto *end = new Fl_Box(x0, y + DownloadList::content_h(), 10, 10); // the scroll reaches down to here
    end->box(FL_NO_BOX);
    settings->end();
    settings->show();
    settings->redraw();
    // 4) Ahora sí: layout() con settings reconstruido y dl_list válido.
    layout();
}

// --- the dialog -------------------------------------------------------------------------------------

namespace {
struct Dialog {
    Fl_Double_Window *w;
    FlatChoice *kind, *quality, *threads;
    Fl_Input *name, *dir;
    Label *ext, *msg;
    std::vector<DlOption> vopts, aopts;
    int vcur = 0;
    const std::vector<DlOption> &opts() const { return kind->value() == 0 && !vopts.empty() ? vopts : aopts; }
};
} // namespace

static Label *row_label(int x, int y, const char *t)
{
    auto *l = new Label(x, y, 96, 28);
    l->size = 12;
    l->set(t);
    return l;
}

static void fill_quality(Dialog *d)
{
    d->quality->clear();
    for (auto &o : d->opts())
        d->quality->add(menu_label(o.label).c_str());
    bool video = &d->opts() == &d->vopts;
    int sel = 0;
    if (video) {
        sel = d->vcur;
    } else {
        std::string pref = setting("dl_audio", "aac");
        for (size_t i = 0; i < d->aopts.size(); i++)
            if ((d->aopts[i].container == DL_WEBM) == (pref == "opus")) {
                sel = (int)i;
                break;
            }
    }
    d->quality->value(sel);
    d->ext->set(d->opts().empty() ? "" : d->opts()[sel].ext);
    d->quality->redraw();
}

static int place_at[2];
static void place_dialog(void *w)
{
    ((Fl_Window *)w)->position(place_at[0], place_at[1]);
}

void download_dialog()
{
    const Details &cur = app->cur;
    if (!can_download(cur)) {
        app->set_status("Este contenido no se puede descargar");
        return;
    }
    Dialog D;
    D.vopts = video_options(cur, D.vcur);
    D.aopts = audio_options(cur);
    const int W = 560, H = 330, LX = 16, CX = 116, CW = W - CX - 16;
    D.w = new Fl_Double_Window(W, H, "Descargar");
    D.w->color(C_BG);
    auto *head = new Label(LX, 10, W - 32, 22);
    head->size = 14;
    head->fg = C_ACCENT;
    head->set(cur.item.plat == P_MUSIC ? "Descargar canción" : "Descargar video");
    auto *title = new Label(LX, 32, W - 32, 22);
    title->font = FL_HELVETICA;
    title->size = 12;
    title->set(clean(cur.item.title));

    int y = 64;
    row_label(LX, y, "Tipo");
    D.kind = new FlatChoice(CX, y, 200, 28);
    if (!D.vopts.empty())
        D.kind->add("Video con audio");
    D.kind->add("Solo audio");
    bool want_audio = D.vopts.empty() || setting("dl_kind", "video") == "audio";
    D.kind->value(!D.vopts.empty() && want_audio ? 1 : 0);
    if (D.vopts.empty())
        D.kind->deactivate();
    y += 36;
    row_label(LX, y, "Calidad");
    D.quality = new FlatChoice(CX, y, CW, 28);
    y += 36;
    row_label(LX, y, "Nombre");
    D.name = new Fl_Input(CX, y, CW - 60, 28);
    style_input(D.name);
    std::string nm = cur.item.plat == P_MUSIC && !cur.item.author.empty() ? cur.item.author + " - " + cur.item.title
                                                                          : cur.item.title;
    D.name->value(safe_name(nm).c_str());
    D.name->position(0); // the start of the name in view, not its end
    D.ext = new Label(CX + CW - 54, y, 54, 28);
    D.ext->font = FL_HELVETICA;
    D.ext->size = 12;
    D.ext->fg = C_DIM;
    y += 36;
    row_label(LX, y, "Directorio");
    D.dir = new Fl_Input(CX, y, CW - 96, 28);
    style_input(D.dir);
    D.dir->value(default_dir().c_str());
    auto *pick = new FlatButton(CX + CW - 90, y, 90, 28, "Elegir…");
    y += 36;
    row_label(LX, y, "Hilos");
    D.threads = new FlatChoice(CX, y, 90, 28);
    D.threads->add("1|2|3|4|6|8");
    static const int nthreads[] = {1, 2, 3, 4, 6, 8};
    int tv = atoi(setting("dl_threads", "4").c_str());
    D.threads->value(3);
    for (int i = 0; i < 6; i++)
        if (nthreads[i] == tv)
            D.threads->value(i);
    auto *th = new Label(CX + 100, y, CW - 100, 28);
    th->font = FL_HELVETICA;
    th->size = 11;
    th->fg = C_DIM;
    th->set("Conexiones a la vez: con más, descarga más rápido.");
    y += 36;
    D.msg = new Label(LX, y, W - 32, 24);
    D.msg->font = FL_HELVETICA;
    D.msg->size = 12;
    D.msg->fg = C_RED;
    auto *ok = new FlatButton(W - 16 - 96 - 8 - 110, H - 46, 110, 32, "Descargar");
    ok->bg = C_GREEN;
    ok->icon = I_DOWNLOAD;
    ok->shortcut(FL_Enter);
    auto *cancel = new FlatButton(W - 16 - 96, H - 46, 96, 32, "Cancelar");
    D.w->end();
    fill_quality(&D);

    D.kind->callback([](Fl_Widget *, void *p) { fill_quality((Dialog *)p); }, &D);
    D.quality->callback([](Fl_Widget *, void *p) {
        Dialog *d = (Dialog *)p;
        int v = d->quality->value();
        if (v >= 0 && v < (int)d->opts().size())
            d->ext->set(d->opts()[v].ext);
    }, &D);
    pick->callback([](Fl_Widget *, void *p) {
        Dialog *d = (Dialog *)p;
        const char *r = fl_dir_chooser("Elegir directorio de descargas", d->dir->value(), 0);
        if (r && *r) {
            std::string s = r;
            while (s.size() > 1 && s.back() == '/')
                s.pop_back();
            d->dir->value(s.c_str());
        }
    }, &D);
    int result = 0;
    ok->callback([](Fl_Widget *, void *r) { *(int *)r = 1; }, &result);
    cancel->callback([](Fl_Widget *, void *r) { *(int *)r = 2; }, &result);
    // Centered on Sansón (flwm puts new windows under the pointer: moved again once shown).
    int cx = app->win->x() + (app->win->w() - W) / 2, cy = app->win->y() + (app->win->h() - H) / 2;
    D.w->position(cx, cy);
    D.w->set_modal();
    D.w->show();
    place_at[0] = cx;
    place_at[1] = cy;
    Fl::add_timeout(0.15, place_dialog, D.w);
    // Tests: SANSON_DL=auto starts the download as offered and shows the Descargas page.
    const char *test = getenv("SANSON_DL");
    bool auto_test = test && !strcmp(test, "auto");
    if (auto_test)
        Fl::add_timeout(1.0, [](void *r) { *(int *)r = 1; }, &result);
    for (;;) {
        while (D.w->shown() && !result)
            Fl::wait();
        if (result != 1)
            break;
        result = 0;
        // Check what was chosen; on a problem the dialog stays open with the reason.
        int qv = D.quality->value();
        std::string name = safe_name(D.name->value()), dir = D.dir->value();
        while (dir.size() > 1 && dir.back() == '/')
            dir.pop_back();
        struct stat st;
        if (qv < 0 || qv >= (int)D.opts().size()) {
            D.msg->set("Elige una calidad.");
            continue;
        }
        if (name.empty()) {
            D.msg->set("Escribe un nombre para el archivo.");
            continue;
        }
        if (dir.empty() || (stat(dir.c_str(), &st) != 0 && mkdir(dir.c_str(), 0755) != 0)) {
            D.msg->set("No existe el directorio y no se pudo crear: " + std::string(strerror(errno)));
            continue;
        }
        if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || access(dir.c_str(), W_OK) != 0) {
            D.msg->set("No se puede escribir en ese directorio.");
            continue;
        }
        const DlOption &o = D.opts()[qv];
        bool video = &D.opts() == &D.vopts;
        set_setting("dl_dir", dir);
        set_setting("dl_kind", video ? "video" : "audio");
        if (!video)
            set_setting("dl_audio", o.container == DL_WEBM ? "opus" : "aac");
        int nt = nthreads[std::max(0, D.threads->value())];
        set_setting("dl_threads", std::to_string(nt));

        auto *d = new Download;
        d->name = name + o.ext;
        d->path = free_path(dir, name, o.ext);
        d->name = d->path.substr(d->path.rfind('/') + 1);
        d->info = o.info;
        dl_params p = {};
        p.video_url = o.video.empty() ? nullptr : o.video.c_str();
        p.video_len = o.vlen;
        p.audio_url = o.audio.empty() ? nullptr : o.audio.c_str();
        p.audio_len = o.alen;
        p.out_path = d->path.c_str();
        p.container = o.container;
        p.threads = nt;
        d->job = dl_start(&p);
        dl_get(d->job, &d->st);
        d->last_t = mono_now();
        downloads.insert(downloads.begin(), d);
        Fl::remove_timeout(dl_tick);
        Fl::add_timeout(0.5, dl_tick);
        nav_label();
        app->set_status("Descargando «" + d->name + "» (" + o.info + ") · el progreso está en Descargas");
        if (app->view == V_DOWNLOADS)
            app->show_downloads();
        if (auto_test)
            Fl::add_timeout(1.0, [](void *) { app->nav_to(NAV_DOWNLOADS); });
        break;
    }
    Fl::remove_timeout(place_dialog, D.w);
    D.w->hide();
    Fl::delete_widget(D.w);
}

// --- leaving ----------------------------------------------------------------------------------------

bool downloads_quit()
{
    int n = 0;
    for (auto *d : downloads) {
        dl_get(d->job, &d->st);
        n += running(d);
    }
    if (n) {
        auto *w = new Fl_Double_Window(440, 140, "Descargas en curso");
        w->color(C_BG);
        auto *t = new Label(16, 12, 408, 70);
        t->size = 13;
        t->font = FL_HELVETICA;
        t->al = FL_ALIGN_LEFT | FL_ALIGN_WRAP | FL_ALIGN_TOP;
        t->set(n == 1 ? "Hay una descarga en curso. Si sales ahora se cancela y se borra lo descargado."
                      : "Hay " + std::to_string(n) +
                            " descargas en curso. Si sales ahora se cancelan y se borra lo descargado.");
        auto *quit = new FlatButton(440 - 16 - 110 - 8 - 110, 94, 110, 32, "Salir igual");
        quit->bg = fl_rgb_color(120, 52, 52);
        auto *stay = new FlatButton(440 - 16 - 110, 94, 110, 32, "Seguir");
        stay->bg = C_GREEN;
        w->end();
        w->set_modal();
        w->show();
        int result = 0;
        quit->callback([](Fl_Widget *, void *r) { *(int *)r = 1; }, &result);
        stay->callback([](Fl_Widget *, void *r) { *(int *)r = 2; }, &result);
        while (w->shown() && !result)
            Fl::wait();
        w->hide();
        Fl::delete_widget(w);
        if (result != 1)
            return false;
    }
    dl_list = nullptr;                 // el widget ya no existe
    Fl::remove_timeout(dl_tick);       // cancelar el tick pendiente
    for (auto *d : downloads)
        dl_cancel(d->job);
    for (auto *d : downloads)
        dl_free(d->job); // waits for each one to stop and delete its pieces
    downloads.clear();
    return true;
}
