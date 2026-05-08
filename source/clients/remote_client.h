#ifndef REMOTECLIENT_H
#define REMOTECLIENT_H

#include <string>
#include <vector>
#include "http/httplib.h"

enum ClientType
{
    CLIENT_TYPE_FTP,
    CLIENT_TYPE_SFTP,
    CLIENT_TYPE_SMB,
    CLIENT_TYPE_WEBDAV,
    CLIENT_TYPE_HTTP_SERVER,
    CLIENT_TYPE_NFS,
    CLIENT_TYPE_FILEHOST,
    CLINET_TYPE_UNKNOWN
};

using namespace httplib;

class RemoteClient
{
public:
    RemoteClient(){};
    virtual ~RemoteClient(){};
    virtual int Connect(const std::string &url, const std::string &username, const std::string &password) = 0;
    virtual int Get(const std::string &outputfile, const std::string &path, uint64_t offset=0) = 0;
    virtual int GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset) = 0;
    virtual const char *LastResponse() = 0;
    virtual int Quit() = 0;
};

#endif
