#ifndef BASESERVER_H
#define BASESERVER_H

#include <string>
#include <vector>
#include <map>
#include "httpclient/HTTPClient.h"
#include "clients/remote_client.h"

class BaseClient : public RemoteClient
{
public:
    BaseClient();
    ~BaseClient();
    int Connect(const std::string &url, const std::string &username, const std::string &password, bool send_ping=false);
    int GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset);
    std::string GetPath(std::string path1, std::string path2);
    std::string GetFullPath(std::string path1);
    const char *LastResponse();
    int Quit();
    static std::string Escape(const std::string &url);
    static std::string UnEscape(const std::string &url);

protected:
    CHTTPClient *client;
    std::string base_path;
    std::string host_url;
    char response[512];
    bool connected = false;
};

#endif
