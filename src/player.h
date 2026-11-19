#ifndef SANSON_PLAYER_H
#define SANSON_PLAYER_H

#include <functional>
#include <string>

struct PlayRequest {
    std::string video;
    std::string audio;
    bool live = false;
    bool hls = false;
    bool fmp4 = false;
    std::string sub;    // SubRip file shown from the start
    double start = 0;
    bool audio_only = false; // no picture: mplayer runs with -novideo
    // Cabeceras HTTP para mplayer. Las rellena app_watch.cpp según la plataforma.
    std::string referer;
    std::string user_agent;
};

void player_init(std::function<void()> on_change);
void player_play(unsigned long wid, const PlayRequest &req);
void player_stop();
bool player_running();
// The file played to its end (mplayer stays open with -idle).
bool player_ended();
bool player_paused();
void player_pause();
void player_seek_abs(double secs);
void player_volume(int delta);
void player_sub_load(const std::string &path);
void player_sub_off();
int player_get_volume();
bool player_mute();                       // the new state: true = muted
void player_frame_step();
bool player_screenshot(bool each_frame); // true while every frame is saved
void player_cmd(const char *cmd, const char *show);
double player_pos();
double player_length();
bool player_started();
std::string player_status();
void player_video_size(int &w, int &h);
// Width/height of the picture as it should be shown (0 while unknown).
double player_video_aspect();

#endif
