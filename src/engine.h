#ifndef SANSON_ENGINE_H
#define SANSON_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sj_plugin sj_plugin;
typedef void (*sj_log_fn)(const char *msg, void *user);

sj_plugin *sj_open(const char *plugin_dir, const char *host_js, const char *settings_json,
                   const char *auth_json, const char *save_state, sj_log_fn log, void *log_user,
                   char **error);
char *sj_call(sj_plugin *p, const char *name, const char *args_json);
char *sj_next(sj_plugin *p, int pager, const char *arg_json);
char *sj_pager_call(sj_plugin *p, int pager, const char *name, const char *args_json);
char *sj_drop(sj_plugin *p, int pager);
char *sj_eval(sj_plugin *p, const char *code);
void sj_set_auth(sj_plugin *p, const char *auth_json);
void sj_set_clearance(sj_plugin *p, const char *cookies, const char *ua);
void sj_close(sj_plugin *p);
char *sj_read_file(const char *path, long *len);
char *sj_sapisid_header(const char *cookies, const char *origin);

#ifdef __cplusplus
}
#endif

#endif
