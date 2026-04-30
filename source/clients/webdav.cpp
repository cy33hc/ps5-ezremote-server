#include <fstream>
#include <regex>
#include <sys/time.h>
#include "clients/remote_client.h"
#include "clients/webdav.h"
#include "pugixml/pugiext.hpp"
#include "util.h"

#if defined(EZREMOTE_ENABLE_UI)
#include "windows.h"
#endif

std::string WebDAVClient::GetHttpUrl(std::string url)
{
    std::string http_url = std::regex_replace(url, std::regex("webdav://"), "http://");
    http_url = std::regex_replace(http_url, std::regex("webdavs://"), "https://");
    return http_url;
}

int WebDAVClient::Connect(const std::string &host, const std::string &user, const std::string &pass, bool send_ping)
{
    std::string url = GetHttpUrl(host);
    return BaseClient::Connect(url, user, pass, send_ping);
}