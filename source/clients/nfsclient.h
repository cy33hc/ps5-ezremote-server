#ifndef NFSCLIENT_H
#define NFSCLIENT_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <string>
#include <vector>
#include "nfsc/libnfs.h"
#include "nfsc/libnfs-raw.h"
#include "nfsc/libnfs-raw-mount.h"
#include "clients/remote_client.h"

class NfsClient : public RemoteClient
{
public:
	NfsClient();
	~NfsClient();
	int Connect(const std::string &url, const std::string &user, const std::string &pass);
	int Get(const std::string &outputfile, const std::string &path, uint64_t offset=0);
	int GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset);
	int GetRange(void *fp, DataSink &sink, uint64_t size, uint64_t offset);
	const char *LastResponse();
	int Quit();

private:
	int _Rmdir(const std::string &ppath);
	struct nfs_context *nfs;
	char response[1024];
	bool connected = false;
};

#endif
