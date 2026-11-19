#ifndef SANSON_COOKIES_H
#define SANSON_COOKIES_H

#include <functional>
#include <string>
#include <vector>

struct Browser {
    std::string name;
    std::string exe;
    std::string sqlite;
};

std::vector<Browser> browsers_found();
// Opens the browser on the login page and returns as soon as the session is in its cookies
// (the browser is then closed), or when the person closes it.
void browser_login(int plat, const Browser &b, std::function<void(std::string auth, std::string error)> cb);
// The same for a verification ("not a robot"): returns the verification cookies, and the
// session too if the person signed in meanwhile.
void browser_verify(int plat, const Browser &b,
                    std::function<void(std::string auth, std::string clearance, std::string error)> cb);
std::string verify_user_agent(int plat);
std::string auth_from_text(int plat, const std::string &text, std::string &error);
std::string auth_summary(int plat, const std::string &auth);

#endif
