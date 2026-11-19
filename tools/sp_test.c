#include "../src/stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "uso: %s hls <master> [indice] [ads|live] | yt <video> <audio> -- [args mplayer]\n", argv[0]);
        return 1;
    }
    sp_start();
    char cmd[8192];
    const char *extra = getenv("MP") ? getenv("MP") : "-benchmark -vo null -ao null -frames 300";
    if (!strcmp(argv[1], "hls")) {
        sp_variant *v;
        int n = sp_hls_variants(argv[2], &v);
        for (int i = 0; i < n; i++)
            fprintf(stderr, "%d: %s %dx%d %.0ffps %dbps\n", i, v[i].name, v[i].width, v[i].height, v[i].fps,
                    v[i].bandwidth);
        int idx = argc > 3 ? atoi(argv[3]) : 0;
        if (n <= 0)
            return 2;
        int fmp4 = sp_hls_fmp4(v[idx].url);
        fprintf(stderr, "fmp4=%d\n", fmp4);
        char *u = sp_add_hls(v[idx].url, argc > 4 ? (!strcmp(argv[4], "live") ? SP_LIVE : SP_SKIP_ADS | SP_LIVE) : 0, getenv("T") ? atof(getenv("T")) : 0);
        snprintf(cmd, sizeof cmd, "mplayer %s %s -cache 4096 -cache-min 20 '%s'",
                 fmp4 ? "-demuxer lavf" : "-demuxer mpegts -tsprobe 400000", extra, u);
    } else {
        char *vu = sp_add_range(argv[2], sp_url_clen(argv[2]), "https://www.youtube.com");
        char *au = sp_add_range(argv[3], sp_url_clen(argv[3]), "https://www.youtube.com");
        snprintf(cmd, sizeof cmd, "mplayer %s -cache 4096 '%s' -audiofile '%s'", extra, vu, au);
    }
    fprintf(stderr, "%s\n", cmd);
    return system(cmd);
}
