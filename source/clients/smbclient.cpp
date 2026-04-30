#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <cstring>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include "clients/smbclient.h"
#include "util.h"

SmbClient::SmbClient()
{
}

SmbClient::~SmbClient()
{
}

int SmbClient::Connect(const std::string &url, const std::string &user, const std::string &pass, bool send_ping)
{
	struct smb2_url *smb_url;

	smb2 = smb2_init_context();
	if (smb2 == NULL)
	{
		sprintf(response, "Failed to init SMB context");
		return 0;
	}

	smb_url = smb2_parse_url(smb2, url.c_str());
	if (smb_url == NULL || smb_url->share == NULL || strlen(smb_url->share) == 0)
	{
		sprintf(response, "Invalid SMB Url");
		return 0;
	}

	if (pass.length() > 0)
		smb2_set_password(smb2, pass.c_str());
	smb2_set_security_mode(smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);
	smb2_set_version(smb2, SMB2_VERSION_ANY);
	smb2_set_timeout(smb2, 30);

	if (smb2_connect_share(smb2, smb_url->server, smb_url->share, user.c_str()) < 0)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}

	smb2_destroy_url(smb_url);
	//max_read_size = smb2_get_max_read_size(smb2);
	//max_write_size = smb2_get_max_write_size(smb2);
	connected = true;

	return 1;
}

/*
 * SmbLastResponse - return a pointer to the last response received
 */
const char *SmbClient::LastResponse()
{
	return (const char *)response;
}

/*
 * SmbQuit - disconnect from remote
 *
 * return 1 if successful, 0 otherwise
 */
int SmbClient::Quit()
{
	smb2_destroy_context(smb2);
	smb2 = NULL;
	connected = false;
	return 1;
}

int SmbClient::GetRange(const std::string &ppath, DataSink &sink, uint64_t size, uint64_t offset)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");
	struct smb2fh *in = smb2_open(smb2, path.c_str(), O_RDONLY);
	if (in == NULL)
	{
		return 0;
	}

	int ret = this->GetRange((void *)in, sink, size, offset);
	smb2_close(smb2, in);

	return ret;
}

int SmbClient::GetRange(void *fp, DataSink &sink, uint64_t size, uint64_t offset)
{
	struct smb2fh *in = (struct smb2fh *)fp;

	smb2_lseek(smb2, in, offset, SEEK_SET, NULL);

	uint8_t *buff = (uint8_t *)malloc(max_read_size);
	int count = 0;
	size_t bytes_remaining = size;
	do
	{
		size_t bytes_to_read = std::min<size_t>(max_read_size, bytes_remaining);
		count = smb2_read(smb2, in, buff, bytes_to_read);
		if (count > 0)
		{
			bytes_remaining -= count;
			bool ok = sink.write((char *)buff, count);
			if (!ok)
			{
				free((uint8_t *)buff);
				return 0;
			}
		}
		else
		{
			break;
		}
	} while (1);

	free((char *)buff);

	return 1;
}

std::string SmbClient::GetPath(std::string ppath1, std::string ppath2)
{
	std::string path1 = ppath1;
	std::string path2 = ppath2;
	path1 = Util::Rtrim(Util::Trim(path1, " "), "/");
	path2 = Util::Rtrim(Util::Trim(path2, " "), "/");
	path1 = path1 + "/" + path2;
	return Util::Ltrim(path1, "/");
}