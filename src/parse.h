#ifndef SANSON_PARSE_H
#define SANSON_PARSE_H

// The JSON the plugins answer (the object model of js/sanson_host.js) into Sansón's structures.

#include <string>

#include "backend.h"
#include "cJSON.h"

std::string jstr(cJSON *o, const char *k);
long long jnum(cJSON *o, const char *k, long long def = -1);
bool jbool(cJSON *o, const char *k);

// "" when the answer is not an error; otherwise the message for the screen.
std::string error_of(cJSON *root);

Item parse_item(cJSON *o, int plat);
Page parse_page(const char *json, int plat);
// getContentDetails value -> d (item, description, sources, audios, subtitles, related)
void parse_details(Details &d, cJSON *v);
// Música: the audio of a YouTube video, one source per codec (AAC first).
void parse_music_audio(Details &m, cJSON *v);
ChatUpdate parse_chat(const char *json);
CommentPage parse_comments(const char *json);
std::string vtt_to_srt(const std::string &vtt);

cJSON *item_to_json(const Item &it);
Item item_from_json(cJSON *o);

#endif
