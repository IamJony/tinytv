#include "player.h"

#include <deque>
#include <algorithm>
#include <FL/Fl.H>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/prctl.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "backend.h"

static pid_t pid = -1;
static int in_fd = -1, out_fd = -1;
static std::string linebuf;
static bool paused, started, live, ended;
static bool muted, shooting; // shooting: S saves every frame until pressed again
// Questions sent to mplayer, in order: its answers (or ANS_ERROR) come back in the same order.
static std::deque<char> asked;
static double pos, length, base, start_time;
static int volume = 80;
static int vid_w, vid_h;
static double vid_aspect; // display aspect (ID_VIDEO_ASPECT), 0 = unknown
static std::string status;
static int subs_loaded = 0;
static std::function<void()> changed;

static void notify()
{
    if (changed)
        changed();
}

static void send_cmd(const char *cmd)
{
    if (in_fd < 0)
        return;
    std::string s = std::string(cmd) + "\n";
    ssize_t r = write(in_fd, s.data(), s.size());
    (void)r;
}

static void handle_line(const std::string &l)
{
    if (getenv("SANSON_DEBUG") && l.rfind("ANS_", 0))
        fprintf(stderr, "[mp] %ld %s\n", (long)time(nullptr), l.c_str());
    if (l.rfind("ANS_ERROR=", 0) == 0) {
        // With -idle mplayer stays open when the file ends: the position is then unavailable.
        char q = asked.empty() ? 0 : asked.front();
        if (!asked.empty())
            asked.pop_front();
        if (q == 'p' && started && !ended && !live) {
            ended = true;
            notify();
        }
        return;
    }
    if ((l.rfind("ANS_TIME_POSITION=", 0) == 0 || l.rfind("ANS_LENGTH=", 0) == 0) && !asked.empty())
        asked.pop_front();
    if (l.rfind("ANS_TIME_POSITION=", 0) == 0) {
        ended = false; // an answer out of step may have looked like the end
        double raw = atof(l.c_str() + 18);
        double off = (base > 0 && start_time > base) ? (raw - start_time + base) : (raw - start_time);
        pos = base > 0 && start_time > base ? off : (base + std::max(0.0, raw - start_time));
        if (!started && pos > base) {
            started = true;
            status.clear();
        }
    } else if (l.rfind("ID_VIDEO_WIDTH=", 0) == 0) {
        vid_w = atoi(l.c_str() + 15);
    } else if (l.rfind("ID_VIDEO_HEIGHT=", 0) == 0) {
        vid_h = atoi(l.c_str() + 16);
    } else if (l.rfind("ID_VIDEO_ASPECT=", 0) == 0) {
        vid_aspect = atof(l.c_str() + 16);
    } else if (l.rfind("ID_START_TIME=", 0) == 0) {
        start_time = std::max(0.0, atof(l.c_str() + 14));
    } else if (l.rfind("ANS_LENGTH=", 0) == 0) {
        double v = atof(l.c_str() + 11);
        if (v > 0 && !live)
            length = v + base;
    } else if (l.rfind("ID_LENGTH=", 0) == 0) {
        double v = atof(l.c_str() + 10);
        if (v > 0 && !live && length <= 0)
            length = v;
    } else if (l.rfind("Starting playback", 0) == 0) {
        started = true;
        status.clear();
    } else if (l.find("Cache fill") != std::string::npos) {
        status = "Cargando…";
    } else if (l.rfind("ANS_pause=", 0) == 0) {
        paused = l.find("yes") != std::string::npos;
    }
}

static void reap();

static void on_output(int fd, void *)
{
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof buf);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        reap();
        return;
    }
    for (ssize_t i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r') {
            if (!linebuf.empty())
                handle_line(linebuf);
            linebuf.clear();
        } else if (linebuf.size() < 4096) {
            linebuf += c;
        }
    }
    notify();
}

static void poll_cb(void *)
{
    if (pid <= 0)
        return;
    // While a stream opens mplayer drops questions without answering: if they pile up they are
    // forgotten, otherwise the position would stop being asked for good.
    if (asked.size() >= 8)
        asked.clear();
    if (!paused) {
        send_cmd("pausing_keep_force get_time_pos");
        asked.push_back('p');
        if (!live && length <= 0) {
            send_cmd("pausing_keep_force get_time_length");
            asked.push_back('l');
        }
    }
    Fl::repeat_timeout(0.5, poll_cb);
}

static void reap()
{
    if (out_fd >= 0) {
        Fl::remove_fd(out_fd);
        close(out_fd);
        out_fd = -1;
    }
    if (in_fd >= 0) {
        close(in_fd);
        in_fd = -1;
    }
    if (pid > 0) {
        int st;
        for (int i = 0; i < 20 && waitpid(pid, &st, WNOHANG) == 0; i++)
            usleep(50000);
        kill(-pid, SIGKILL);
        if (waitpid(pid, &st, WNOHANG) == 0)
            waitpid(pid, &st, 0);
    }
    pid = -1;
    Fl::remove_timeout(poll_cb);
    notify();
}

static void on_term(int sig)
{
    if (pid > 0)
        kill(-pid, SIGKILL);
    _exit(128 + sig);
}

void player_init(std::function<void()> on_change)
{
    changed = on_change;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    signal(SIGHUP, on_term);
}

void player_stop()
{
    if (pid > 0) {
        send_cmd("quit");
        reap();
    }
    started = false;
    paused = false;
    ended = false;
    asked.clear();
    vid_w = vid_h = 0;
    vid_aspect = 0;
    status.clear();
}

void player_play(unsigned long wid, const PlayRequest &req)
{
    player_stop();
    live = req.live;
    base = req.start;
    pos = req.start;
    length = 0;
    start_time = 0;
    paused = false;
    muted = false;
    shooting = false;
    started = false;
    status = "Conectando…";
    volume = atoi(setting("volume", "80").c_str());

    std::vector<std::string> a = {"mplayer", "-slave", "-quiet", "-identify", "-idle", "-nomouseinput",
                                  "-input", "nodefault-bindings:conf=/dev/null", "-noconsolecontrols",
                                  "-wid", std::to_string(wid), "-vo", setting("vo", "xv,x11"), "-ao",
                                  "pulse,alsa,null", "-zoom", "-framedrop", "-sws", "0", "-lavdopts",
                                  "skiploopfilter=all", "-volume", std::to_string(volume), "-softvol"};
    // No -autosync: with it the video stayed slow after a pause (0.72x with 30, measured).
    if (req.live) {
        // Without its audio (YouTube's separate track) the video is light: 10% of 8 MB is more
        // than the first segments bring, and mplayer would wait in real time to start.
        a.insert(a.end(), {"-cache", req.audio.empty() ? "8192" : "4096", "-cache-min", "10"});
    } else {
        a.insert(a.end(), {"-cache", "8192", "-cache-min", "2"});
        if (req.start > 0 && !req.hls) {
            a.insert(a.end(), {"-ss", std::to_string((int)req.start)});
            base = 0;
        }
    }
    if (req.audio_only)
        a.push_back("-novideo");
    else // screenshot: the s and S keys
        a.insert(a.end(), {"-vf", setting("light", "0") == "1" ? "framestep=2,screenshot" : "screenshot"});

    if (req.hls && req.live && !req.fmp4 && req.audio.empty()) {
        a.insert(a.end(), {"-demuxer", "mpegts", "-tsprobe", "400000"});
    } else if (req.hls && req.live && !req.fmp4) {
        a.insert(a.end(), {"-demuxer", "mpegts", "-tsprobe", "400000", "-correct-pts", "-mc", "0.2"});
    } else if (req.hls) {
        // HLS VOD: Auto-detect demuxer (mpegts para mp2t del proxy) permite seek.
        // NO añadir -forceidx: rompe HLS.
        a.push_back("-correct-pts"); // Auto-detect demuxer (mpegts)
    } else {
        a.push_back("-correct-pts");
    }
    if (!req.audio.empty())
        a.insert(a.end(), {"-audiofile", req.audio, "-audiofile-cache", "2048"});
    // YouTube's separate HLS audio arrives as MPEG-TS (see SP_AAC_TS); the video's frame rate is
    // misread, so -correct-pts (above) keeps its speed and -mc 0.2 lines it up with the audio.
    if (req.hls && !req.audio.empty())
        a.insert(a.end(), {"-audio-demuxer", "mpegts"});
    // Sansón draws its own controls: mplayer's messages (in English) would only cover the video.
    a.insert(a.end(), {"-osdlevel", "0", "-utf8", "-subfont-autoscale", "1", "-subfont-text-scale", "4.5", "-subfont-outline", "2"});
    subs_loaded = 0;
    if (!req.sub.empty()) {
        a.insert(a.end(), {"-sub", req.sub});
        subs_loaded = 1;
    }
    if (!req.user_agent.empty())
        a.insert(a.end(), {"-user-agent", req.user_agent});
    if (!req.referer.empty())
        a.insert(a.end(), {"-referrer", req.referer});
    a.push_back(req.video);

    int pin[2], pout[2];
    if (pipe(pin) || pipe(pout))
        return;
    pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        int r = nice(4);
        (void)r;
        dup2(pin[0], 0);
        dup2(pout[1], 1);
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0)
            dup2(dn, 2);
        for (int fd = 3; fd < 256; fd++)
            close(fd);
        const char *home = getenv("HOME"); // where the screenshots go
        if (home && chdir(home))
            (void)0;
        std::vector<char *> argv;
        for (auto &s : a)
            argv.push_back(const_cast<char *>(s.c_str()));
        argv.push_back(nullptr);
        execvp("mplayer", argv.data());
        _exit(127);
    }
    close(pin[0]);
    close(pout[1]);
    in_fd = pin[1];
    out_fd = pout[0];
    fcntl(out_fd, F_SETFL, O_NONBLOCK);
    fcntl(in_fd, F_SETFD, FD_CLOEXEC);
    fcntl(out_fd, F_SETFD, FD_CLOEXEC);
    Fl::add_fd(out_fd, FL_READ, on_output);
    Fl::add_timeout(0.5, poll_cb);
    notify();
}

bool player_running()
{
    return pid > 0;
}

bool player_ended()
{
    return ended;
}

bool player_paused()
{
    return paused;
}

bool player_started()
{
    return started;
}

void player_pause()
{
    if (pid <= 0)
        return;
    send_cmd("pause");
    paused = !paused;
    notify();
}

void player_seek_abs(double secs)
{
    if (pid <= 0 || live)
        return;
    if (secs < 0)
        secs = 0;
    double target = secs - base;
    if (target < 0)
        target = 0;
    char b[64];
    snprintf(b, sizeof b, "seek %.1f 2", target);
    send_cmd(b);
    paused = false;
    pos = secs;
    notify();
}

// Subtitles are added to the running mplayer: the video does not start again.
void player_sub_load(const std::string &path)
{
    if (pid <= 0)
        return;
    send_cmd(("pausing_keep_force sub_load \"" + path + "\"").c_str());
    send_cmd(("pausing_keep_force sub_file " + std::to_string(subs_loaded++)).c_str());
}

void player_sub_off()
{
    if (pid > 0)
        send_cmd("pausing_keep_force sub_file -1");
}

void player_volume(int delta)
{
    muted = false; // mplayer turns the mute off with any volume
    volume += delta;
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    char b[48];
    snprintf(b, sizeof b, "pausing_keep_force volume %d 1", volume);
    send_cmd(b);
    set_setting("volume", std::to_string(volume));
    notify();
}

int player_get_volume()
{
    return volume;
}

bool player_mute()
{
    if (pid <= 0)
        return muted;
    send_cmd("pausing_keep_force mute");
    muted = !muted;
    return muted;
}

// mplayer stays paused on the next frame
void player_frame_step()
{
    if (pid <= 0 || live)
        return;
    send_cmd("frame_step");
    paused = true;
    notify();
}

bool player_screenshot(bool each_frame)
{
    if (pid <= 0)
        return false;
    if (each_frame)
        shooting = !shooting;
    send_cmd(each_frame ? "pausing_keep_force screenshot 1" : "pausing_keep_force screenshot 0");
    return shooting;
}

// A key of mplayer's own (mpkeys.h) that changes nothing Sansón shows; show says on the video
// what changed (mplayer's own messages are off: -osdlevel 0).
void player_cmd(const char *cmd, const char *show)
{
    if (pid <= 0)
        return;
    send_cmd((std::string("pausing_keep_force ") + cmd).c_str());
    if (show)
        send_cmd((std::string("pausing_keep_force osd_show_property_text \"") + show + "\" 2000 0").c_str());
}

double player_pos()
{
    return pos;
}

double player_length()
{
    return length;
}

std::string player_status()
{
    return status;
}

void player_video_size(int &w, int &h)
{
    w = vid_w;
    h = vid_h;
}

double player_video_aspect()
{
    if (vid_aspect > 0.1)
        return vid_aspect;
    return vid_w > 0 && vid_h > 0 ? (double)vid_w / vid_h : 0;
}
