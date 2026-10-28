#include <lexbor/html/parser.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/node.h>
#include <fstream>
#include <map>
#include "config.h"
#include "clients/remote_client.h"
#include "clients/archiveorg.h"
#include "util.h"

using httplib::Client;
using httplib::Headers;
using httplib::Result;

struct InsensitiveCompare
{
    bool operator()(const std::string &a, const std::string &b) const
    {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    }
};

static std::set<std::string, InsensitiveCompare> ignore_cookie_keys = {"path", "expires", "max-age", "domain", "secure"};

std::string ArchiveOrgClient::GenerateRandomId(const int len)
{
    static const char alphanum[] = "0123456789abcdef";
    std::string tmp_s;
    tmp_s.reserve(len);

    for (int i = 0; i < len; ++i) {
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    
    return tmp_s;
}

int ArchiveOrgClient::Connect(const std::string &url, const std::string &username, const std::string &password)
{
    std::lock_guard<std::mutex> lock(mtx);

    if (this->connected)
        return 1;

    this->host_url = url;
    size_t scheme_pos = url.find("://");
    size_t root_pos = url.find("/", scheme_pos + 3);
    if (root_pos != std::string::npos)
    {
        this->host_url = url.substr(0, root_pos);
        this->base_path = url.substr(root_pos);
    }

    client = new httplib::Client(this->host_url);
    client->set_keep_alive(true);
    client->set_follow_location(true);
    client->set_connection_timeout(30);
    client->set_read_timeout(30);
    client->enable_server_certificate_verification(false);

    this->cookies = {
        {"donation-identifier", GenerateRandomId(32)},
        {"test-cookie", "1"},
        {"abtest-identifier", GenerateRandomId(32)}
    };

    if (username.length() > 0)
        return Login(username, password);

    this->connected = true;
    return 1;
}

int ArchiveOrgClient::Login(const std::string &username, const std::string &password)
{
    std::string url = std::string("/account/login");
    Headers headers = {{ "User-Agent", "Mozilla/5.0 (X11; Linux x86_64; rv:133.0) Gecko/20100101 Firefox/133.0"}};
    SetCookies(headers);

    MultipartFormDataItems items = {
        {"username", username, "", ""},
        {"password", password, "", ""},
        {"remember", "true", "", ""},
        {"referer", "https://archive.org/", "", ""},
        {"login", "true", "", ""},
        {"submit_by_js", "true", "", ""}};

    if (auto res = client->Post(url, headers, items))
    {
        if (HTTP_SUCCESS(res->status))
        {
            if (res->has_header("Set-Cookie"))
            {
                auto range = res->headers.equal_range("Set-Cookie");

                size_t index = 0;
                for (auto range_it = range.first; range_it != range.second; ++range_it)
                {
                    std::vector<std::string> cookies = Util::Split(range_it->second, ";");
                    for (std::vector<std::string>::iterator it = cookies.begin(); it != cookies.end();)
                    {
                        std::vector<std::string> cookie = Util::Split(*it, "=");
                        std::string key = Util::Trim(cookie[0], " ");
                        if (ignore_cookie_keys.find(key) == ignore_cookie_keys.end())
                        {
                            if (cookie.size() > 1)
                            {
                                this->cookies[key] = Util::Trim(cookie[1], " ");
                            }
                            else
                            {
                                this->cookies[key] = "";
                            }
                        }
                        ++it;
                    }
                }

                this->connected = true;
                return 1;
            }
            else
            {
                return 0;
            }
        }
        else
        {
            return 0;
        }
    }
    else
    {
        return 0;
    }
}
