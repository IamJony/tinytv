#include "cookies.h"

#include <FL/Fl.H>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "backend.h"
#include "cJSON.h"

struct CookieRow {
    std::string host;
    std::string name;
    std::string value;
};

static std::string plat_key(int plat)
{
    std::string id = backend_plat_id(plat);
    for (char &ch : id)
        ch = (char)std::tolower((unsigned char)ch);
    if (!id.empty())
        return id;

    std::string pn = plat_name(plat);
    for (char &ch : pn)
        ch = (char)std::tolower((unsigned char)ch);
    if (pn.find("youtube") != std::string::npos)
        return "youtube";
    if (pn.find("twitch") != std::string::npos)
        return "twitch";
    if (pn.find("kick") != std::string::npos)
        return "kick";
    return pn;
}

static std::string domain_for_plat(int plat)
{
    std::string k = plat_key(plat);
    if (k == "youtube")
        return "youtube.com";
    if (k == "twitch")
        return "twitch.tv";
    if (k == "kick")
        return "kick.com";
    return "";
}

static std::string trim_str(const std::string &s)
{
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
        a++;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
        b--;
    return s.substr(a, b - a);
}

static bool host_match(const std::string &host, const std::string &domain)
{
    if (domain.empty() || host.empty())
        return true;
    std::string h = host[0] == '.' ? host.substr(1) : host;
    size_t dl = domain.size();
    return h == domain ||
           (h.size() > dl && h.compare(h.size() - dl, dl, domain) == 0 && h[h.size() - dl - 1] == '.');
}

static std::string url_decode(const std::string &s)
{
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() &&
            std::isxdigit((unsigned char)s[i + 1]) &&
            std::isxdigit((unsigned char)s[i + 2])) {
            r += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            r += s[i];
        }
    }
    return r;
}

static std::string build_auth(int plat, const std::vector<CookieRow> &rows, std::string &err)
{
    err.clear();
    std::string k = plat_key(plat);
    std::string dom = domain_for_plat(plat);

    std::map<std::string, std::string> c;
    for (const auto &r : rows) {
        if (!r.name.empty() && (dom.empty() || host_match(r.host, dom)))
            c[r.name] = r.value;
    }

    if (c.empty()) {
        err = "No se encontraron cookies válidas para la plataforma.";
        return "";
    }

    cJSON *o = cJSON_CreateObject();
    cJSON *h = cJSON_CreateObject();
    std::string cookies;
    bool ok = false;

    if (k == "youtube") {
        for (const auto &kv : c)
            cookies += (cookies.empty() ? "" : "; ") + kv.first + "=" + kv.second;
        ok = c.count("SAPISID") || c.count("__Secure-3PAPISID") || c.count("__Secure-1PAPISID") || c.count("SID");
        if (!ok)
            err = "Faltan cookies de sesión de YouTube (SAPISID / SID).";
    } else if (k == "twitch") {
        for (const auto &kv : c)
            cookies += (cookies.empty() ? "" : "; ") + kv.first + "=" + kv.second;
        if (c.count("auth-token")) {
            cJSON_AddStringToObject(h, "Authorization", ("OAuth " + c["auth-token"]).c_str());
            ok = true;
        } else {
            err = "Falta la cookie auth-token de Twitch.";
        }
    } else if (k == "kick") {
        if (c.count("session_token")) {
            cookies = "session_token=" + c["session_token"];
            std::string dec = url_decode(c["session_token"]);
            if (dec.find('|') != std::string::npos)
                cJSON_AddStringToObject(h, "Authorization", ("Bearer " + dec).c_str());
            ok = true;
        } else {
            err = "Falta la cookie session_token de Kick.";
        }
    } else {
        for (const auto &kv : c)
            cookies += (cookies.empty() ? "" : "; ") + kv.first + "=" + kv.second;
        ok = !cookies.empty();
        if (!ok)
            err = "No se reconocieron cookies para esta plataforma.";
    }

    std::string out;
    if (ok) {
        if (!cookies.empty())
            cJSON_AddStringToObject(o, "cookies", cookies.c_str());
        if (h->child)
            cJSON_AddItemToObject(o, "headers", h);
        else
            cJSON_Delete(h);
        char *s = cJSON_PrintUnformatted(o);
        if (s) {
            out = s;
            std::free(s);
        }
        cJSON_Delete(o);
    } else {
        cJSON_Delete(h);
        cJSON_Delete(o);
    }
    return out;
}

std::vector<Browser> browsers_found()
{
    return {};
}

std::string verify_user_agent(int plat)
{
    (void)plat;
    return "";
}

std::string auth_from_text(int plat, const std::string &text, std::string &error)
{
    error.clear();
    if (trim_str(text).empty()) {
        error = "El texto está vacío.";
        return "";
    }

    std::string dom = domain_for_plat(plat);
    std::vector<CookieRow> rows;
    bool netscape = false;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos)
            e = text.size();
        std::string line = trim_str(text.substr(pos, e - pos));
        pos = (e == text.size()) ? text.size() : e + 1;

        if (line.empty())
            continue;

        std::vector<std::string> f;
        size_t p = 0;
        while (true) {
            size_t t = line.find('\t', p);
            f.push_back(trim_str(line.substr(p, t == std::string::npos ? std::string::npos : t - p)));
            if (t == std::string::npos)
                break;
            p = t + 1;
        }

        if (f.size() >= 7 && !line.empty() && line[0] != '#') {
            CookieRow r;
            r.host = f[0];
            r.name = f[5];
            r.value = f[6];
            rows.push_back(r);
            netscape = true;
        } else if (line.rfind("#HttpOnly_", 0) == 0 && f.size() >= 7) {
            CookieRow r;
            r.host = f[0].substr(10);
            r.name = f[5];
            r.value = f[6];
            rows.push_back(r);
            netscape = true;
        }
    }

    if (!netscape) {
        std::string t = trim_str(text);
        if (t.rfind("Cookie:", 0) == 0 || t.rfind("cookie:", 0) == 0)
            t = trim_str(t.substr(7));

        std::string k = plat_key(plat);
        if (t.find('=') == std::string::npos) {
            if (k == "twitch") {
                CookieRow r;
                r.host = dom;
                r.name = "auth-token";
                r.value = (t.rfind("OAuth ", 0) == 0) ? trim_str(t.substr(6)) : t;
                rows.push_back(r);
            } else if (k == "kick") {
                CookieRow r;
                r.host = dom;
                r.name = "session_token";
                r.value = t;
                rows.push_back(r);
            }
        } else {
            size_t p = 0;
            while (p < t.size()) {
                size_t sc = t.find(';', p);
                std::string kv = trim_str(t.substr(p, sc == std::string::npos ? std::string::npos : sc - p));
                p = (sc == std::string::npos) ? t.size() : sc + 1;
                if (kv.empty())
                    continue;
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    CookieRow r;
                    r.host = dom;
                    r.name = trim_str(kv.substr(0, eq));
                    r.value = trim_str(kv.substr(eq + 1));
                    rows.push_back(r);
                }
            }
        }
    }

    return build_auth(plat, rows, error);
}

std::string auth_summary(int plat, const std::string &auth)
{
    (void)plat;
    return auth.empty() ? "Sin sesión" : "Sesión guardada";
}

void browser_login(int plat, const Browser &b, std::function<void(std::string auth, std::string error)> cb)
{
    (void)plat;
    (void)b;
    if (cb)
        cb("", "Inicio de sesión en navegador no disponible en este modo.");
}

void browser_verify(int plat, const Browser &b,
                    std::function<void(std::string auth, std::string clearance, std::string error)> cb)
{
    (void)plat;
    (void)b;
    if (cb)
        cb("", "", "Verificación en navegador no disponible en este modo.");
}
