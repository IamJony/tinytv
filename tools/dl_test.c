// dl-test: tests of the downloader without the window.
//   dl-test join mp4|webm OUT IN1 [IN2]
//   dl-test get mp4|webm THREADS OUT VIDEO_URL|- AUDIO_URL|-
#include "../src/download.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "join")) {
        char err[256] = "";
        const char *in[2] = {argv[4], argc > 5 ? argv[5] : NULL};
        double t = now();
        int r = !strcmp(argv[2], "webm") ? dl_join_webm(in, argc > 5 ? 2 : 1, argv[3], err, sizeof err)
                                         : dl_join_mp4(in, argc > 5 ? 2 : 1, argv[3], err, sizeof err);
        printf("join %s (%.2fs) %s\n", r ? "ERROR" : "ok", now() - t, err);
        return r ? 1 : 0;
    }
    if (argc == 7 && !strcmp(argv[1], "get")) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        dl_params p = {0};
        p.container = !strcmp(argv[2], "webm") ? DL_WEBM : DL_MP4;
        p.threads = atoi(argv[3]);
        p.out_path = argv[4];
        p.video_url = strcmp(argv[5], "-") ? argv[5] : NULL;
        p.audio_url = strcmp(argv[6], "-") ? argv[6] : NULL;
        double t0 = now();
        dl_job *j = dl_start(&p);
        dl_status s;
        for (;;) {
            dl_get(j, &s);
            fprintf(stderr, "\r%d %lld/%lld %.0f KB/s   ", s.state, s.done, s.total, s.done / 1024.0 / (now() - t0));
            if (s.state >= DL_DONE)
                break;
            usleep(500000);
        }
        printf("\nestado %d en %.1fs %s\n", s.state, now() - t0, s.error);
        dl_free(j);
        return s.state == DL_DONE ? 0 : 1;
    }
    fprintf(stderr, "uso: dl-test join mp4|webm OUT IN1 [IN2] | get mp4|webm HILOS OUT VIDEO|- AUDIO|-\n");
    return 2;
}
