#include "../src/engine.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static long rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    long v = 0;
    while (f && fgets(line, sizeof line, f))
        if (!strncmp(line, "VmRSS:", 6))
            v = atol(line + 6);
    if (f)
        fclose(f);
    return v;
}

static void on_log(const char *msg, void *u)
{
    if (getenv("JSLOG"))
        fprintf(stderr, "[js] %.300s\n", msg);
}

static void show(const char *label, char *r, double t)
{
    size_t max = getenv("FULL") ? (size_t)-1 : 1500;
    fprintf(stderr, "== %s: %.2fs rss=%ldKB len=%zu\n", label, t, rss_kb(), r ? strlen(r) : 0);
    if (r)
        printf("%.*s\n", (int)(strlen(r) < max ? strlen(r) : max), r);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "uso: %s <dir-plugin> <host.js> [metodo args-json]... [eval codigo]\n", argv[0]);
        return 1;
    }
    struct rusage ru;
    double t0 = now();
    char *err = NULL;
    sj_plugin *p = sj_open(argv[1], argv[2], getenv("SETTINGS"), getenv("AUTH"), NULL, on_log, NULL, &err);
    fprintf(stderr, "== open+enable: %.2fs rss=%ldKB\n", now() - t0, rss_kb());
    if (!p) {
        fprintf(stderr, "error: %s\n", err ? err : "?");
        return 2;
    }
    for (int i = 3; i + 1 < argc; i += 2) {
        double t = now();
        char *r;
        if (!strcmp(argv[i], "sleep")) {
            sleep(atoi(argv[i + 1]));
            continue;
        }
        if (!strcmp(argv[i], "eval"))
            r = sj_eval(p, argv[i + 1]);
        else if (!strcmp(argv[i], "next"))
            r = sj_next(p, atoi(argv[i + 1]), NULL);
        else
            r = sj_call(p, argv[i], argv[i + 1]);
        show(argv[i], r, now() - t);
        free(r);
    }
    getrusage(RUSAGE_SELF, &ru);
    fprintf(stderr, "== total %.2fs maxrss=%ldKB\n", now() - t0, ru.ru_maxrss);
    sj_close(p);
    return 0;
}
