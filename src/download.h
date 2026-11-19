#ifndef SANSON_DOWNLOAD_H
#define SANSON_DOWNLOAD_H

// Downloads of YouTube and YouTube Music without outside programs: the googlevideo files
// (video and audio apart) are fetched by pieces with several connections at once, and then
// joined into one file by Sansón itself: MP4 (H.264/AV1 + AAC) or WebM (VP9 + Opus).

#ifdef __cplusplus
extern "C" {
#endif

enum { DL_MP4, DL_WEBM };
enum { DL_RUNNING, DL_JOINING, DL_DONE, DL_ERROR, DL_CANCELLED };

typedef struct dl_job dl_job;

typedef struct {
    const char *video_url; /* NULL: only the audio */
    long long video_len;   /* 0: taken from the URL (clen=) */
    const char *audio_url; /* NULL: only the video */
    long long audio_len;
    const char *out_path;  /* the final file */
    int container;         /* DL_MP4 or DL_WEBM: the container of both files */
    int threads;           /* connections at once (1-16) */
} dl_params;

typedef struct {
    int state;
    long long done, total; /* bytes */
    char error[256];
} dl_status;

dl_job *dl_start(const dl_params *p);
void dl_get(dl_job *j, dl_status *s);
void dl_cancel(dl_job *j);
/* Waits for the job to stop (cancel it first to not wait for the whole download). */
void dl_free(dl_job *j);

/* Joins files already downloaded (used by dl_start, and by the tests). 0 = fine. */
int dl_join_mp4(const char **in, int n, const char *out, char *err, int errsz);
int dl_join_webm(const char **in, int n, const char *out, char *err, int errsz);

#ifdef __cplusplus
}
#endif

#endif
