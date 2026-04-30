#ifndef WEBDAV_H
#define WEBDAV_H

#include <string>
#include <vector>
#include "clients/baseclient.h"
#include "clients/remote_client.h"

class WebDAVClient : public BaseClient
{
public:
    int Connect(const std::string &url, const std::string &user, const std::string &pass, bool send_ping=false);
    
private:
    static std::string GetHttpUrl(std::string url);
    bool PropFind(const std::string &path, int depth, CHTTPClient::HttpResponse &res);
};

#endif
