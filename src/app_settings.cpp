// Ajustes: las cuentas de cada plataforma y las preferencias.

#include "app.h"
#include "store.h"
#include <FL/Fl_Native_File_Chooser.H>
void hide_store_view();

#include <FL/Fl_Box.H>
#include <FL/Fl_Multiline_Input.H>

#include "chatsend.h"
#include "cookies.h"

static void paste_dialog(int p)
{
    auto *w = new Fl_Double_Window(560, 330, "Pegar sesión");
    w->color(C_BG);
    auto *t = new Label(16, 10, 528, 46);
    t->size = 12;
    t->font = FL_HELVETICA;
    t->al = FL_ALIGN_LEFT | FL_ALIGN_WRAP | FL_ALIGN_TOP;
    std::string pid = backend_plat_id(p);
    t->set(pid == "youtube" ? "Pega las cookies de youtube.com."
         : pid == "twitch"  ? "Pega la cookie auth-token de twitch.tv."
         : pid == "kick"    ? "Pega la cookie session_token de kick.com."
                            : ("Pega las cookies de " + std::string(plat_name(p)) + "."));
    auto *in = new Fl_Multiline_Input(16, 60, 528, 210);
    style_input(in);
    in->textsize(11);
    in->wrap(1);
    auto *ok = new FlatButton(344, 284, 96, 32, "Guardar");
    ok->bg = C_GREEN;
    auto *cancel = new FlatButton(448, 284, 96, 32, "Cancelar");
    w->end();
    w->set_modal();
    w->show();
    int result = 0;
    ok->callback([](Fl_Widget *, void *r) { *(int *)r = 1; }, &result);
    cancel->callback([](Fl_Widget *, void *r) { *(int *)r = 2; }, &result);
    while (w->shown() && !result)
        Fl::wait();
    if (result == 1) {
        std::string err;
        std::string auth = auth_from_text(p, in->value(), err);
        if (auth.empty()) {
            app->set_status(err.empty() ? "No se reconoció la sesión" : err);
        } else {
            backend_set_auth(p, auth);
            chat_send_reset();
            app->set_status(std::string("Sesión guardada para ") + plat_name(p));
        }
    }
    w->hide();
    Fl::delete_widget(w);
    app->show_settings();
}

static void logged_in(int p, const std::string &auth, const std::string &err, const char *done)
{
    if (auth.empty()) {
        app->set_status(err.empty() ? "No se detectó la sesión" : err);
    } else {
        backend_set_auth(p, auth);
        chat_send_reset();
        app->set_status(std::string(done) + plat_name(p) + ": Inicio y Siguiendo muestran ahora lo de tu cuenta");
    }
    if (app->view == V_SETTINGS)
        app->show_settings();
}

void verify_captcha(int p)
{
    static bool busy;
    static double asked[P_COUNT_MAX];
    if (p == P_MUSIC && P_YOUTUBE >= 0)
        p = P_YOUTUBE;
    if (p < 0 || p >= P_COUNT_MAX)
        return;
    auto browsers = browsers_found();
    if (busy || (asked[p] && mono_now() - asked[p] < 120))
        return;
    asked[p] = mono_now();
    if (browsers.empty()) {
        app->set_status(std::string(plat_name(p)) + " pide una verificación: inicia sesión con «Pegar cookies…» en Ajustes");
        return;
    }
    Browser br = browsers[0];
    auto *w = new Fl_Double_Window(480, 170, "Verificación");
    w->color(C_BG);
    auto *t = new Label(16, 12, 448, 100);
    t->size = 13;
    t->font = FL_HELVETICA;
    t->al = FL_ALIGN_LEFT | FL_ALIGN_WRAP | FL_ALIGN_TOP;
    t->set(std::string(plat_name(p)) + " pide verificar que no eres un robot.\n\nSe abrirá " + br.name +
           " en " + plat_name(p) + ": completa la verificación (o inicia sesión) y el navegador se cerrará solo.");
    auto *ok = new FlatButton(264, 124, 96, 32, "Verificar");
    ok->bg = C_GREEN;
    auto *cancel = new FlatButton(368, 124, 96, 32, "Ahora no");
    w->end();
    w->set_modal();
    w->show();
    int result = 0;
    ok->callback([](Fl_Widget *, void *r) { *(int *)r = 1; }, &result);
    cancel->callback([](Fl_Widget *, void *r) { *(int *)r = 2; }, &result);
    while (w->shown() && !result)
        Fl::wait();
    w->hide();
    Fl::delete_widget(w);
    if (result != 1)
        return;
    busy = true;
    app->set_status("Completa la verificación en " + br.name + "; se cerrará solo al terminar…");
    browser_verify(p, br, [p](std::string auth, std::string clear, std::string err) {
        busy = false;
        if (!err.empty()) {
            app->set_status(err);
            return;
        }
        if (!clear.empty())
            backend_set_clearance(p, clear, verify_user_agent(p));
        if (!auth.empty()) {
            backend_set_auth(p, auth);
            chat_send_reset();
        }
        asked[p] = 0;
        app->set_status(std::string("Verificación lista en ") + plat_name(p));
        app->reload();
    });
}

static Label *heading(int x, int y, int w, const char *text)
{
    auto *l = new Label(x, y, w, 26);
    l->size = 14;
    l->fg = C_ACCENT;
    l->set(text);
    return l;
}

static Label *hint(int x, int y, int w, int h, const std::string &text)
{
    auto *l = new Label(x, y, w, h);
    l->font = FL_HELVETICA;
    l->size = 11;
    l->fg = C_DIM;
    l->al = FL_ALIGN_LEFT | FL_ALIGN_WRAP | FL_ALIGN_TOP;
    l->set(text);
    return l;
}

static FlatChoice *option(int x0, int &y, int W, const char *name, const char *items, int value, const std::string &help)
{
    auto *l = new Label(x0, y + 4, 220, 28);
    l->set(name);
    bool narrow = W < 560;
    int cx = narrow ? x0 : x0 + 230;
    int cy = narrow ? y + 34 : y + 4;
    auto *c = new FlatChoice(cx, cy, 230, 28);
    c->add(items);
    c->value(value);
    if (narrow) {
        hint(x0, cy + 32, W, 30, help);
        y += 34 + 32 + 34;
    } else {
        hint(cx + 240, y + 4, W - 470, 32, help);
        y += 42;
    }
    return c;
}

void App::show_settings()
{
    if (!ready) return;
    if (view == V_DOWNLOADS) clear_downloads_view();

    if (!ready) return;
    Fl::remove_timeout([](void*){ if (app && app->ready && app->view == V_SETTINGS) app->show_settings(); });

    hide_store_view();
    view = V_SETTINGS;
    stop_watch();
    scroll->hide();
    watch->hide();
    back_btn->hide();
    sub_btn->hide();
    play_all_btn->hide();
    shuffle_all_btn->hide();
    select_nav(NAV_SETTINGS);
    header_title->set("Ajustes y cuentas");
    layout();
    settings_w = settings->w();
    settings->clear();
    settings->scroll_to(0, 0);
    settings->begin();
    
    int x0 = settings->x() + 20, y = settings->y() + 12, W = settings->w() - 52;

    heading(x0, y, W, "Cuentas");
    y += 26;
    hint(x0, y, W, 30, "Con la sesión iniciada, Inicio muestra lo que la plataforma te recomienda y Siguiendo "
                       "y Novedades suman los canales de tu cuenta.");
    y += 36;
    auto browsers = browsers_found();
    int nplats = backend_plat_count();
    for (int p = 0; p < nplats; p++) {
        if (p == P_MUSIC || backend_plat_id(p) == "music")
            continue;
        std::vector<std::pair<std::string, int>> btns;
        for (auto &b : browsers) {
            std::string l = "Iniciar con " + b.name;
            fl_font(FL_HELVETICA_BOLD, 12);
            btns.push_back({l, (int)fl_width(l.c_str()) + 24});
        }
        btns.push_back({"Pegar cookies…", 126});
        bool logged = backend_logged(p);
        if (logged)
            btns.push_back({"Cerrar sesión", 116});
        int total = 0;
        for (auto &b : btns)
            total += b.second + 4;
        bool two_rows = total > W - 200;
        int rh = two_rows ? 108 : 70;
        auto *row = new Bar(x0, y, W, rh, C_PANEL);
        row->end();
        auto *stripe = new Fl_Box(x0, y, 4, rh);
        stripe->box(FL_FLAT_BOX);
        stripe->color(plat_color[p]);
        auto *name = new Label(x0 + 16, y + 8, 180, 24);
        name->size = 15;
        name->set(plat_name(p));
        auto *st = new Label(x0 + 16, y + 36, 200, 22);
        st->font = FL_HELVETICA;
        st->size = 12;
        st->fg = logged ? C_GREEN : C_DIM;
        st->set(logged ? "Sesión iniciada" : "Sin sesión");
        int bx = two_rows ? x0 + 16 : x0 + W - 12 - total + 4;
        int byy = two_rows ? y + 66 : y + 20;
        for (size_t i = 0; i < btns.size(); i++) {
            auto *b = new FlatButton(bx, byy, btns[i].second, 30);
            b->copy_label(btns[i].first.c_str());
            bx += btns[i].second + 4;
            if (i < browsers.size()) {
                b->bg = C_SEL;
                b->callback([](Fl_Widget *, void *d) {
                    int code = (int)(long)d, pl = code / 16;
                    Browser br = browsers_found()[code % 16];
                    app->set_status("Inicia sesión en " + br.name + "...");
                    browser_login(pl, br, [pl](std::string auth, std::string err) {
                        logged_in(pl, auth, err, "Sesión iniciada en ");
                    });
                }, (void *)(long)(p * 16 + (int)i));
            } else if (btns[i].first == "Pegar cookies…") {
                b->callback([](Fl_Widget *, void *d) { paste_dialog((int)(long)d); }, (void *)(long)p);
            } else {
                b->bg = fl_rgb_color(120, 52, 52);
                b->callback([](Fl_Widget *, void *d) {
                    backend_set_auth((int)(long)d, "");
                    chat_send_reset();
                    app->set_status("Sesión cerrada");
                    app->show_settings();
                }, (void *)(long)p);
            }
        }
        y += rh + 8;
    }

    y += 10;
    heading(x0, y, W, "Reproducción");
    y += 32;
    std::string cq = setting("quality", "auto");
    auto *q = option(x0, y, W, "Calidad preferida", "Automática|360p|480p|720p|1080p|Máxima",
                     cq == "360" ? 1 : cq == "480" ? 2 : cq == "720" ? 3 : cq == "1080" ? 4 : cq == "0" ? 5 : 0,
                     "Automática usa 360p o 480p según el tamaño de la ventana.");
    q->callback([](Fl_Widget *w, void *) {
        static const char *v[] = {"auto", "360", "480", "720", "1080", "0"};
        set_setting("quality", v[((Fl_Choice *)w)->value()]);
    });
    auto *ap = option(x0, y, W, "Reproducción continua", "Sí|No", autoplay() ? 0 : 1,
                      "Al terminar un video sigue con el próximo.");
    ap->callback([](Fl_Widget *w, void *) { set_setting("autoplay", ((Fl_Choice *)w)->value() ? "0" : "1"); });
    auto *vo = option(x0, y, W, "Salida de video", "Automática (xv)|x11 (compatible)",
                      setting("vo", "xv,x11") == "x11" ? 1 : 0, "x11 si el video se ve negro.");
    vo->callback([](Fl_Widget *w, void *) { set_setting("vo", ((Fl_Choice *)w)->value() ? "x11" : "xv,x11"); });
    auto *light = option(x0, y, W, "Video liviano", "No|Sí (15 fps)", setting("light", "0") == "1" ? 1 : 0,
                         "La mitad de los cuadros: el equipo va más fluido.");
    light->callback([](Fl_Widget *w, void *) { set_setting("light", ((Fl_Choice *)w)->value() ? "1" : "0"); });

    y += 16;
    hint(x0, y, W, 64,
         "TinyTV es un fork basado en el código de Sansón de Andres Cortez . "
         "Este agrega soporte de plugins externos, incluye por defecto LatinoTV para ver televison en español-Latinoamérica. "
         "Teclas: / buscar · Esc volver · espacio pausa · F pantalla completa · C panel · N siguiente.");
    y += 80;

    // --- Instalar plugin local (.zip) ---
    {
        FlatButton *inst_btn = new FlatButton(x0, y, 280, 34, "Instalar plugin local (.zip)");
        inst_btn->bg = C_ACCENT;
        inst_btn->fg = C_BG;
        inst_btn->callback([](Fl_Widget *, void *) {
            Fl_Native_File_Chooser chooser;
            chooser.title("Selecciona el archivo .zip del plugin");
            chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
            chooser.filter("Plugins (*.zip)\t*.zip");
            
            if (chooser.show() == 0) {
                std::string path = chooser.filename();
                app->set_status("Instalando plugin local...");
                store_install_local(path, [](bool ok, std::string err) {
                    if (ok) app->set_status("Plugin instalado con éxito. Reinicia TinyTV.");
                    else app->set_status("Error: " + err);
                });
            }
        });
        settings->add(inst_btn);
        y += 46;
    }

    auto *end = new Fl_Box(x0, y, 10, 10);
    end->box(FL_NO_BOX);
    settings->end();
    settings->show();
    settings->redraw();
}
