#ifndef MPKEYS_H
#define MPKEYS_H

// mplayer's own keys (MPlayer 1.5, input/input.c def_cmd_binds), so a player with mplayer
// inside a window (-wid) answers like mplayer in a terminal. mplayer runs with its keys off: X
// gives them to the app's window anyway. The app does the ones that change what it shows
// (pause, seek, volume...) and sends the rest to mplayer as they are.
// Left out: DVD menus, TV, teletext, EDL, Tab (moves the focus), Ins/Del/End, 'c' (dump the
// stream) and 'T' (on top: the window manager's job with -wid).
// The same file goes in Lumière, Sansón and mplayer-gui.

#include <FL/Fl.H>
#include <FL/Enumerations.H>

enum MpKeyAct {
    MK_NONE,
    MK_QUIT,       // q, Esc: mplayer-gui closes, Lumière and Sansón go back
    MK_PAUSE,      // p, Space
    MK_SEEK,       // arg: seconds
    MK_VOLUME,     // arg: +1 or -1
    MK_MUTE,       // m
    MK_FULLSCREEN, // f
    MK_PREV,       // <
    MK_NEXT,       // >
    MK_FRAME_STEP, // . (leaves the video paused)
    MK_SCREENSHOT, // arg: 0 this frame (s), 1 every frame until pressed again (S)
    MK_CMD         // cmd goes to mplayer as it is; show says on the video what changed
};

struct MpKey {
    MpKeyAct act;
    double arg;
    const char *cmd;
    const char *show_es, *show_en; // osd_show_property_text after cmd (null: mplayer shows it)
};

// The key of the event in progress. With Ctrl or Alt: none (mplayer ignores them too).
inline MpKey mp_key_event()
{
    MpKey k = {MK_NONE, 0, nullptr, nullptr, nullptr};
    if (Fl::event_state() & (FL_CTRL | FL_ALT | FL_META))
        return k;
    switch (Fl::event_key()) {
    case FL_Right: k.act = MK_SEEK; k.arg = 10; return k;
    case FL_Left: k.act = MK_SEEK; k.arg = -10; return k;
    case FL_Up: k.act = MK_SEEK; k.arg = 60; return k;
    case FL_Down: k.act = MK_SEEK; k.arg = -60; return k;
    case FL_Page_Up: k.act = MK_SEEK; k.arg = 600; return k;
    case FL_Page_Down: k.act = MK_SEEK; k.arg = -600; return k;
    case FL_Escape: k.act = MK_QUIT; return k;
    case FL_BackSpace:
        k = {MK_CMD, 0, "speed_set 1.0", "Velocidad: ${speed}", "Speed: ${speed}"};
        return k;
    }
    // the character, not the key: '*', '{' or '#' are on different keys in each layout
    const char *t = Fl::event_text();
    if (!t || !t[0] || t[1])
        return k;
    switch (t[0]) {
    case 'q': k.act = MK_QUIT; break;
    case 'p':
    case ' ': k.act = MK_PAUSE; break;
    case '9':
    case '/': k.act = MK_VOLUME; k.arg = -1; break;
    case '0':
    case '*': k.act = MK_VOLUME; k.arg = 1; break;
    case 'm': k.act = MK_MUTE; break;
    case 'f': k.act = MK_FULLSCREEN; break;
    case '<': k.act = MK_PREV; break;
    case '>': k.act = MK_NEXT; break;
    case '.': k.act = MK_FRAME_STEP; break;
    case 's': k.act = MK_SCREENSHOT; k.arg = 0; break;
    case 'S': k.act = MK_SCREENSHOT; k.arg = 1; break;
#define MK(c, command, es, en) \
    case c: k = {MK_CMD, 0, command, es, en}; break;
    MK('+', "audio_delay 0.100", "Retraso del audio: ${audio_delay}", "Audio delay: ${audio_delay}")
    MK('-', "audio_delay -0.100", "Retraso del audio: ${audio_delay}", "Audio delay: ${audio_delay}")
    MK('[', "speed_mult 0.9091", "Velocidad: ${speed}", "Speed: ${speed}")
    MK(']', "speed_mult 1.1", "Velocidad: ${speed}", "Speed: ${speed}")
    MK('{', "speed_mult 0.5", "Velocidad: ${speed}", "Speed: ${speed}")
    MK('}', "speed_mult 2.0", "Velocidad: ${speed}", "Speed: ${speed}")
    MK('o', "osd", "OSD: ${osdlevel}", "OSD: ${osdlevel}")
    MK('I', "osd_show_property_text \"${filename}\"", nullptr, nullptr)
    MK('P', "osd_show_progression", nullptr, nullptr)
    MK('z', "sub_delay -0.1", "Retraso de subtítulos: ${sub_delay}", "Subtitle delay: ${sub_delay}")
    MK('x', "sub_delay +0.1", "Retraso de subtítulos: ${sub_delay}", "Subtitle delay: ${sub_delay}")
    MK('g', "sub_step -1", nullptr, nullptr)
    MK('y', "sub_step +1", nullptr, nullptr)
    MK('(', "balance -0.1", "Balance: ${balance}", "Balance: ${balance}")
    MK(')', "balance 0.1", "Balance: ${balance}", "Balance: ${balance}")
    MK('1', "contrast -1", "Contraste: ${contrast}", "Contrast: ${contrast}")
    MK('2', "contrast 1", "Contraste: ${contrast}", "Contrast: ${contrast}")
    MK('3', "brightness -1", "Brillo: ${brightness}", "Brightness: ${brightness}")
    MK('4', "brightness 1", "Brillo: ${brightness}", "Brightness: ${brightness}")
    MK('5', "hue -1", "Tono: ${hue}", "Hue: ${hue}")
    MK('6', "hue 1", "Tono: ${hue}", "Hue: ${hue}")
    MK('7', "saturation -1", "Saturación: ${saturation}", "Saturation: ${saturation}")
    MK('8', "saturation 1", "Saturación: ${saturation}", "Saturation: ${saturation}")
    MK('d', "frame_drop", "Saltar cuadros: ${framedropping}", "Frame dropping: ${framedropping}")
    MK('D', "step_property deinterlace", "Desentrelazado: ${deinterlace}", "Deinterlace: ${deinterlace}")
    MK('r', "sub_pos -1", "Posición de subtítulos: ${sub_pos}", "Subtitle position: ${sub_pos}")
    MK('t', "sub_pos +1", "Posición de subtítulos: ${sub_pos}", "Subtitle position: ${sub_pos}")
    MK('a', "sub_alignment", "Alineación de subtítulos: ${sub_alignment}", "Subtitle alignment: ${sub_alignment}")
    MK('v', "sub_visibility", "Subtítulos visibles: ${sub_visibility}", "Subtitles visible: ${sub_visibility}")
    MK('j', "sub_select", "Subtítulos: ${sub}", "Subtitles: ${sub}")
    MK('J', "sub_select -3", "Subtítulos: ${sub}", "Subtitles: ${sub}")
    MK('F', "forced_subs_only", "Solo subtítulos forzados: ${sub_forced_only}", "Forced subtitles only: ${sub_forced_only}")
    MK('#', "switch_audio", "Audio: ${switch_audio}", "Audio: ${switch_audio}")
    MK('_', "step_property switch_video", "Video: ${switch_video}", "Video: ${switch_video}")
    MK('w', "panscan -0.1", "Panscan: ${panscan}", "Panscan: ${panscan}")
    MK('e', "panscan +0.1", "Panscan: ${panscan}", "Panscan: ${panscan}")
    MK('!', "seek_chapter -1", "Capítulo: ${chapter}", "Chapter: ${chapter}")
    MK('@', "seek_chapter 1", "Capítulo: ${chapter}", "Chapter: ${chapter}")
    MK('A', "switch_angle 1", "Ángulo: ${angle}", "Angle: ${angle}")
#undef MK
    }
    return k;
}

#endif
