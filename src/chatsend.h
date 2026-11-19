#ifndef SANSON_CHATSEND_H
#define SANSON_CHATSEND_H

#include <functional>
#include <string>

bool chat_can_send(int plat);
void chat_send(int plat, const std::string &url, const std::string &text, std::function<void(std::string)> done);
void chat_send_reset();

#endif
