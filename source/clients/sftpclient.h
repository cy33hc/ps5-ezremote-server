#ifndef EZ_SFTPCLIENT_H
#define EZ_SFTPCLIENT_H

#include <libssh2.h>
#include <libssh2_sftp.h>
#include <string>
#include <vector>
#include "clients/remote_client.h"

class SFTPClient : public RemoteClient
{
public:
    SFTPClient();
    ~SFTPClient();
    int Connect(const std::string &url, const std::string &username, const std::string &password);
	int Get(const std::string &outputfile, const std::string &path, uint64_t offset=0);
    int GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset);
    int GetRange(void *fp, DataSink &sink, uint64_t size, uint64_t offset);
    const char *LastResponse();
    int Quit();

protected:
    LIBSSH2_SESSION *session;
    LIBSSH2_SFTP *sftp_session;
    int sock;
    char response[512];
    bool connected = false;
};

#endif
