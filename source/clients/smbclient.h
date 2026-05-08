#ifndef SMBCLIENT_H
#define SMBCLIENT_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <string>
#include <vector>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include "clients/remote_client.h"

#define SMB_CLIENT_MAX_FILENAME_LEN 256

class SmbClient : public RemoteClient
{
public:
	SmbClient();
	~SmbClient();
	int Connect(const std::string &url, const std::string &user, const std::string &pass);
	int Get(const std::string &outputfile, const std::string &path, uint64_t offset=0);
	int GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset);
	int GetRange(void *fp, DataSink &sink, uint64_t size, uint64_t offset);
	const char *LastResponse();
	int Quit();

private:
	struct smb2_context *smb2;
	char response[1024];
	bool connected = false;
	uint32_t max_read_size = 1048576;
	uint32_t max_write_size = 1048576;
};

#endif
