#ifndef BASESERVER_H
#define BASESERVER_H

#include <string>
#include <vector>
#include <map>
#include "httpclient/HTTPClient.h"
#include "clients/remote_client.h"

#define HTTP_SUCCESS(x) (x >= 200 && x < 300)

class BaseClient : public RemoteClient
{
public:
    BaseClient();
    ~BaseClient();
    int Connect(const std::string &url, const std::string &username, const std::string &password);
	int Get(const std::string &outputfile, const std::string &path, uint64_t offset=0);
    int GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset);
    std::string GetPath(std::string path1, std::string path2);
    std::string GetFullPath(std::string path1);
    const char *LastResponse();
    int Quit();
    static int DownloadProgressCallback(void* ptr, double dTotalToDownload, double dNowDownloaded, double dTotalToUpload, double dNowUploaded);
    static int SocketOptCallback(void* ptr, int fd, uint32_t socktype);
    static size_t WriteCallback(void *pCurlData, size_t usBlockCount, size_t usBlockSize, void *pUserData);

protected:
    CHTTPClient *client;
    std::string base_path;
    std::string host_url;
    char response[512];
    bool connected = false;
};

#endif
