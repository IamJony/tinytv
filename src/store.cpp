// Almacén del proyecto (configuración, suscripciones, historial, cola de Música)
// + tienda de plugins remota.

#include "backend.h"
#include "store.h"

#include <FL/Fl.H>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <curl/curl.h>

#include "cJSON.h"

// ============================================================================
// Utilidades de archivo
// ============================================================================
std::string config_dir()
{
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = (x && *x) ? x : (std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.config");
    std::string dir = base + "/tinytv";
    mkdir(base.c_str(), 0700);
    mkdir(dir.c_str(), 0700);
    return dir;
}

std::string read_file(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return "";
    std::string s;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        s.append(buf, n);
    fclose(f);
    return s;
}

void write_file(const std::string &path, const std::string &text, int mode)
{
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f)
        return;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    chmod(tmp.c_str(), (mode_t)mode);
    rename(tmp.c_str(), path.c_str());
}

// ============================================================================
// Ajustes clave=valor
// ============================================================================
std::string setting(const std::string &key, const std::string &def)
{
    cJSON *root = cJSON_Parse(read_file(config_dir() + "/settings.json").c_str());
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key.c_str());
    std::string out = def;
    if (cJSON_IsString(v) && v->valuestring)
        out = v->valuestring;
    else if (cJSON_IsNumber(v))
        out = std::to_string((long long)v->valuedouble);
    else if (cJSON_IsBool(v))
        out = cJSON_IsTrue(v) ? "1" : "0";
    cJSON_Delete(root);
    return out;
}

void set_setting(const std::string &key, const std::string &value)
{
    std::string path = config_dir() + "/settings.json";
    cJSON *root = cJSON_Parse(read_file(path).c_str());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    cJSON_DeleteItemFromObjectCaseSensitive(root, key.c_str());
    cJSON_AddStringToObject(root, key.c_str(), value.c_str());
    char *s = cJSON_Print(root);
    write_file(path, s);
    free(s);
    cJSON_Delete(root);
}

// ============================================================================
// Listas: suscripciones, historial, cola de Música
// ============================================================================
static cJSON *load_array(const std::string &path, const char *key)
{
    cJSON *root = cJSON_Parse(read_file(path).c_str());
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return cJSON_CreateArray();
    }
    cJSON_DetachItemViaPointer(root, arr);
    cJSON_Delete(root);
    return arr;
}

static void save_array(const std::string &path, const char *key, cJSON *arr)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, key, arr);
    char *s = cJSON_PrintUnformatted(root);
    write_file(path, s ? s : "{}");
    free(s);
    cJSON_Delete(root);
}

static cJSON *item_to_json(const Item &it)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "plat", it.plat);
    cJSON_AddStringToObject(o, "kind", it.kind.c_str());
    cJSON_AddStringToObject(o, "id", it.id.c_str());
    cJSON_AddStringToObject(o, "title", it.title.c_str());
    cJSON_AddStringToObject(o, "author", it.author.c_str());
    cJSON_AddStringToObject(o, "author_url", it.author_url.c_str());
    cJSON_AddStringToObject(o, "author_thumb", it.author_thumb.c_str());
    cJSON_AddStringToObject(o, "thumb", it.thumb.c_str());
    cJSON_AddStringToObject(o, "url", it.url.c_str());
    cJSON_AddStringToObject(o, "album", it.album.c_str());
    cJSON_AddStringToObject(o, "album_url", it.album_url.c_str());
    cJSON_AddStringToObject(o, "note", it.note.c_str());
    cJSON_AddNumberToObject(o, "views", (double)it.views);
    cJSON_AddNumberToObject(o, "date", (double)it.date);
    cJSON_AddNumberToObject(o, "subs", (double)it.subs);
    cJSON_AddNumberToObject(o, "duration", it.duration);
    cJSON_AddBoolToObject(o, "live", it.live);
    return o;
}

static Item item_from_json(cJSON *o)
{
    Item it;
    it.plat = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "plat"));
    it.kind = cJSON_GetStringValue(cJSON_GetObjectItem(o, "kind"));
    it.id = cJSON_GetStringValue(cJSON_GetObjectItem(o, "id"));
    it.title = cJSON_GetStringValue(cJSON_GetObjectItem(o, "title"));
    it.author = cJSON_GetStringValue(cJSON_GetObjectItem(o, "author"));
    it.author_url = cJSON_GetStringValue(cJSON_GetObjectItem(o, "author_url"));
    it.author_thumb = cJSON_GetStringValue(cJSON_GetObjectItem(o, "author_thumb"));
    it.thumb = cJSON_GetStringValue(cJSON_GetObjectItem(o, "thumb"));
    it.url = cJSON_GetStringValue(cJSON_GetObjectItem(o, "url"));
    it.album = cJSON_GetStringValue(cJSON_GetObjectItem(o, "album"));
    it.album_url = cJSON_GetStringValue(cJSON_GetObjectItem(o, "album_url"));
    it.note = cJSON_GetStringValue(cJSON_GetObjectItem(o, "note"));
    it.views = (long long)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "views"));
    it.date = (long long)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "date"));
    it.subs = (long long)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "subs"));
    it.duration = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "duration"));
    it.live = cJSON_IsTrue(cJSON_GetObjectItem(o, "live"));
    return it;
}

static std::vector<Item> list_from(cJSON *arr)
{
    std::vector<Item> v;
    cJSON *it;
    cJSON_ArrayForEach(it, arr)
        v.push_back(item_from_json(it));
    return v;
}

static std::string subs_path() { return config_dir() + "/subs.json"; }
static std::string history_path() { return config_dir() + "/history.json"; }
static std::string music_queue_path() { return config_dir() + "/music_queue.json"; }

std::vector<Item> subs_list()
{
    cJSON *arr = load_array(subs_path(), "subs");
    std::vector<Item> v = list_from(arr);
    cJSON_Delete(arr);
    return v;
}

bool subs_has(const std::string &url)
{
    for (auto &it : subs_list())
        if (it.url == url)
            return true;
    return false;
}

void subs_toggle(const Item &channel)
{
    cJSON *arr = load_array(subs_path(), "subs");
    bool removed = false;
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        std::string u = cJSON_GetStringValue(cJSON_GetObjectItem(it, "url"));
        if (u == channel.url) {
            cJSON_DeleteItemFromArray(arr, i);
            removed = true;
            break;
        }
    }
    if (!removed)
        cJSON_AddItemToArray(arr, item_to_json(channel));
    save_array(subs_path(), "subs", arr);
}

std::vector<Item> history_list()
{
    cJSON *arr = load_array(history_path(), "history");
    std::vector<Item> v = list_from(arr);
    cJSON_Delete(arr);
    return v;
}

void history_add(const Item &it)
{
    cJSON *arr = load_array(history_path(), "history");
    cJSON *next = nullptr;
    int i = 0;
    cJSON_ArrayForEach(next, arr)
    {
        std::string u = cJSON_GetStringValue(cJSON_GetObjectItem(next, "url"));
        if (u == it.url) {
            cJSON_DeleteItemFromArray(arr, i);
            break;
        }
        i++;
    }
    cJSON_InsertItemInArray(arr, 0, item_to_json(it));
    while (cJSON_GetArraySize(arr) > 200)
        cJSON_DeleteItemFromArray(arr, cJSON_GetArraySize(arr) - 1);
    save_array(history_path(), "history", arr);
}

std::vector<Item> music_queue_list()
{
    cJSON *arr = load_array(music_queue_path(), "queue");
    std::vector<Item> v = list_from(arr);
    cJSON_Delete(arr);
    return v;
}

void music_queue_save(const std::vector<Item> &v)
{
    cJSON *arr = cJSON_CreateArray();
    for (auto &it : v)
        cJSON_AddItemToArray(arr, item_to_json(it));
    save_array(music_queue_path(), "queue", arr);
}

// ============================================================================
// Formateo de números y fechas
// ============================================================================
std::string fmt_count(long long n)
{
    if (n < 0)
        return "";
    if (n < 1000)
        return std::to_string(n);
    char buf[32];
    if (n < 1000000)
        snprintf(buf, sizeof buf, "%.1f K", n / 1000.0);
    else if (n < 1000000000)
        snprintf(buf, sizeof buf, "%.1f M", n / 1000000.0);
    else
        snprintf(buf, sizeof buf, "%.1f B", n / 1000000000.0);
    std::string s = buf;
    if (s.size() > 2 && s[s.size() - 3] == '.' && s[s.size() - 2] == '0')
        s.erase(s.size() - 3, 2);
    return s;
}

std::string fmt_duration(int secs)
{
    if (secs <= 0)
        return "";
    char buf[32];
    int h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
    if (h > 0)
        snprintf(buf, sizeof buf, "%d:%02d:%02d", h, m, s);
    else
        snprintf(buf, sizeof buf, "%d:%02d", m, s);
    return buf;
}

std::string fmt_ago(long long epoch)
{
    if (epoch <= 0)
        return "";
    long long now = (long long)time(nullptr);
    long long d = now - epoch;
    if (d < 60)
        return "hace un momento";
    if (d < 3600)
        return "hace " + std::to_string(d / 60) + " min";
    if (d < 86400)
        return "hace " + std::to_string(d / 3600) + " h";
    if (d < 86400 * 7)
        return "hace " + std::to_string(d / 86400) + " días";
    if (d < 86400 * 30)
        return "hace " + std::to_string(d / (86400 * 7)) + " semanas";
    if (d < 86400 * 365)
        return "hace " + std::to_string(d / (86400 * 30)) + " meses";
    return "hace " + std::to_string(d / (86400 * 365)) + " años";
}

// ============================================================================
// Tienda de plugins remota
// ============================================================================
static const char *CATALOG_URL =
    "https://raw.githubusercontent.com/IamJony/plugins-tinytv/refs/heads/main/index.json";

static std::string user_plugins_dir() { return config_dir() + "/plugins"; }
static std::string sys_plugins_dir()
{
    const char *env = getenv("SANSON_SHARE");
    return std::string(env ? env : "/usr/local/share/tinytv") + "/plugins";
}
static size_t wr(char *p, size_t s, size_t n, void *ud)
{
    static_cast<std::string *>(ud)->append(p, s * n);
    return s * n;
}
static bool http_get(const std::string &url, std::string &out)
{
    CURL *c = curl_easy_init();
    if (!c)
        return false;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Sanson/1.8");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wr);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    CURLcode r = curl_easy_perform(c);
    long cc = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &cc);
    curl_easy_cleanup(c);
    return r == CURLE_OK && cc == 200;
}
static void awake(std::function<void()> f)
{
    auto *p = new std::function<void()>(std::move(f));
    Fl::awake([](void *d) {
        auto *q = static_cast<std::function<void()> *>(d);
        (*q)();
        delete q;
    }, p);
}
static void mkdir_p(const std::string &path)
{
    std::string cur;
    for (char c : path) {
        cur += c;
        if (c == '/' && cur.size() > 1)
            mkdir(cur.c_str(), 0755);
    }
    mkdir(path.c_str(), 0755);
}
static bool save_file(const std::string &path, const std::string &data)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
}

static std::string find_plugin_dir(const std::string &id)
{
    std::string base = user_plugins_dir();
    if (access((base + "/" + id + "/config.json").c_str(), R_OK) == 0)
        return base + "/" + id;
    DIR *d = opendir(base.c_str());
    if (!d) return "";
    struct dirent *e;
    std::string found;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        std::string cfg = base + "/" + e->d_name + "/config.json";
        if (access(cfg.c_str(), R_OK) != 0) continue;
        cJSON *r = cJSON_Parse(read_file(cfg).c_str());
        cJSON *jid = cJSON_GetObjectItemCaseSensitive(r, "id");
        if (cJSON_IsString(jid) && jid->valuestring && id == jid->valuestring)
            found = base + "/" + e->d_name;
        cJSON_Delete(r);
        if (!found.empty()) break;
    }
    closedir(d);
    return found;
}

bool store_installed(const std::string &id)
{
    return !find_plugin_dir(id).empty();
}
std::string store_local_version(const std::string &id)
{
    std::string dir = find_plugin_dir(id);
    if (dir.empty()) return "";
    std::string txt = read_file(dir + "/config.json");
    cJSON *r = cJSON_Parse(txt.c_str());
    cJSON *v = cJSON_GetObjectItemCaseSensitive(r, "version");
    std::string out;
    if (cJSON_IsString(v))
        out = v->valuestring;
    else if (cJSON_IsNumber(v))
        out = std::to_string((long long)v->valuedouble);
    cJSON_Delete(r);
    return out;
}
bool store_has_update(const StorePlugin &p)
{
    if (!p.installed)
        return false;
    std::string local = store_local_version(p.id);
    return !local.empty() && local != p.version;
}
static void ensure_includes(const std::string &id) {
    std::string cfg = read_file(user_plugins_dir() + "/" + id + "/config.json");
    cJSON *r = cJSON_Parse(cfg.c_str());
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(r, "sansonIncludes");
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (!cJSON_IsString(it)) continue;
        std::string inc = it->valuestring;
        if (inc.rfind("../", 0) != 0) continue;
        std::string name = inc.substr(3);
        std::string dst = user_plugins_dir() + "/" + name;
        
        if (access(dst.c_str(), R_OK) == 0) continue; // Ya existe

        // 1. Buscar en el sistema
        std::string src = sys_plugins_dir() + "/" + name;
        if (access(src.c_str(), R_OK) == 0) {
            save_file(dst, read_file(src));
            continue;
        }
        
        // 2. Buscar en la carpeta local del proyecto (desarrollo)
        src = "plugins/" + name;
        if (access(src.c_str(), R_OK) == 0) {
            save_file(dst, read_file(src));
            continue;
        }

        // 3. Descargar de internet (repositorio de TinyTV)
        std::string url = "https://raw.githubusercontent.com/IamJony/plugins-tinytv/main/" + name;
        std::string body;
        if (http_get(url, body) && !body.empty()) {
            save_file(dst, body);
            printf("[Tienda] Descargado %s de Internet.\n", name.c_str());
        } else {
            printf("[Tienda] ERROR: No se pudo obtener %s\n", name.c_str());
        }
    }
    cJSON_Delete(r);
}

void store_fetch(std::function<void(std::vector<StorePlugin>, std::string)> cb)
{
    std::thread([cb] {
        std::string body;
        if (!http_get(CATALOG_URL, body)) {
            awake([cb] { cb({}, "No se pudo descargar el catalogo."); });
            return;
        }
        std::vector<StorePlugin> out;
        cJSON *root = cJSON_Parse(body.c_str());
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "plugins");
        cJSON *it;
        cJSON_ArrayForEach(it, arr)
        {
            StorePlugin p;
            p.id = cJSON_GetStringValue(cJSON_GetObjectItem(it, "id"));
            p.name = cJSON_GetStringValue(cJSON_GetObjectItem(it, "name"));
            p.author = cJSON_GetStringValue(cJSON_GetObjectItem(it, "author"));
            p.version = cJSON_GetStringValue(cJSON_GetObjectItem(it, "version"));
            p.icon_url = cJSON_GetStringValue(cJSON_GetObjectItem(it, "icon_url"));
            p.zip_url = cJSON_GetStringValue(cJSON_GetObjectItem(it, "zip_url"));
            p.description = cJSON_GetStringValue(cJSON_GetObjectItem(it, "description"));
            p.installed = store_installed(p.id);
            if (p.installed)
                p.local_version = store_local_version(p.id);
            out.push_back(p);
        }
        cJSON_Delete(root);
        awake([cb, out] { cb(out, ""); });
    }).detach();
}

void store_install(const StorePlugin &p, std::function<void(bool, std::string)> cb)
{
    std::thread([p, cb] {
        std::string zip;
        if (!http_get(p.zip_url, zip)) {
            awake([cb] { cb(false, "No se pudo descargar el ZIP."); });
            return;
        }
        std::string tmp = "/tmp/tinytv-plugin-" + p.id + ".zip";
        save_file(tmp, zip);
        std::string dest = user_plugins_dir();
        mkdir_p(dest);
        std::string cmd = "unzip -o -q \"" + tmp + "\" -d \"" + dest + "\"";
        int rc = system(cmd.c_str());
        unlink(tmp.c_str());
        if (rc != 0) {
            awake([cb] { cb(false, "Fallo la extraccion (instala unzip)."); });
            return;
        }
        ensure_includes(p.id);
        awake([cb] { cb(true, ""); });
    }).detach();
}

void store_uninstall(const std::string &id, std::function<void(bool, std::string)> cb)
{
    std::thread([id, cb] {
        std::string dir = find_plugin_dir(id);
        if (dir.empty()) dir = user_plugins_dir() + "/" + id;
        std::string cmd = "rm -rf \"" + dir + "\"";
        int rc = system(cmd.c_str());
        awake([cb, rc] { cb(rc == 0, rc == 0 ? "" : "No se pudo borrar."); });
    }).detach();
}

void store_restart_app()
{
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0)
        return;
    exe[n] = 0;
    // Cierra el reproductor sin arrastrar destructores de statics con hilos
    fflush(nullptr);
    execl(exe, exe, (char *)nullptr);
}


void store_install_local(const std::string &zip_path, std::function<void(bool, std::string)> cb) {
    std::thread([zip_path, cb] {
        std::string dest = user_plugins_dir();
        mkdir_p(dest);
        // Copiamos el zip a /tmp para evitar problemas con espacios o permisos
        std::string tmp = "/tmp/tinytv-local-" + std::to_string(getpid()) + ".zip";
        std::string cmd_cp = "cp \"" + zip_path + "\" \"" + tmp + "\"";
        if (system(cmd_cp.c_str()) != 0) {
            awake([cb] { cb(false, "No se pudo leer el archivo local."); });
            return;
        }
        std::string cmd_unzip = "unzip -o -q \"" + tmp + "\" -d \"" + dest + "\"";
        int rc = system(cmd_unzip.c_str());
        unlink(tmp.c_str());
        
        if (rc != 0) {
            awake([cb] { cb(false, "Fallo la extraccion del ZIP."); });
            return;
        }
        
        // Detectar qué carpeta se acaba de crear para copiar los includes (comun.js)
        // Buscamos la carpeta más reciente en el destino
        DIR *d = opendir(dest.c_str());
        if (d) {
            struct dirent *e;
            time_t latest = 0;
            std::string latest_id = "";
            while ((e = readdir(d))) {
                if (e->d_name[0] == '.') continue;
                std::string cfg = dest + "/" + e->d_name + "/config.json";
                struct stat st;
                if (stat(cfg.c_str(), &st) == 0 && st.st_mtime > latest) {
                    latest = st.st_mtime;
                    latest_id = e->d_name;
                }
            }
            closedir(d);
            if (!latest_id.empty()) {
                ensure_includes(latest_id);
            }
        }
        
        awake([cb] { cb(true, ""); });
    }).detach();
}
