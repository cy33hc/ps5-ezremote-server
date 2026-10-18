#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <algorithm>
#include <cstring>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include "sceSystemService.h"
#include "config.h"
#include "fs.h"
#include "clients/smbclient.h"
#include "util.h"

SmbClient::SmbClient()
{
}

SmbClient::~SmbClient()
{
}

int SmbClient::Connect(const std::string &url, const std::string &user, const std::string &pass)
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
/*
 * SmbGet - issue a GET command and write received data to output
 *
 * return 1 if successful, 0 otherwise
 */

int SmbClient::Get(const std::string &outputfile, const std::string &ppath, uint64_t offset)
{
	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");

	struct smb2fh* in = smb2_open(smb2, path.c_str(), O_RDONLY);
	if (in == NULL)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}

	FILE* out = NULL;
	if (offset > 0)
	{
		out = FS::Append(outputfile);
	}
	else
	{
		out = FS::Create(outputfile);
	}

	if (out == NULL)
	{
		// sprintf(response, "%s", lang_strings[STR_FAILED]);
		return 0;
	}

	uint8_t *buff = (uint8_t*)malloc(max_read_size);
	int count = 0;
	*g_bytes_transfered = offset;

	if (offset > 0)
	{
		smb2_lseek(smb2, in, offset, SEEK_SET, NULL);
	}

	while ((count = smb2_read(smb2, in, buff, max_read_size)) != 0)
	{
		if (count < 0)
		{
			sprintf(response, "%s", smb2_get_error(smb2));
			FS::Close(out);
			smb2_close(smb2, in);
			free((void*)buff);
			return 0;
		}
		FS::Write(out, buff, count);
		*g_bytes_transfered += count;
		sceSystemServicePowerTick();
	}

	FS::Close(out);
	smb2_close(smb2, in);
	free((void*)buff);
	return 1;
}


bool SmbClient::IsConnected() const
{
	return smb2 != NULL && connected;
}

int SmbClient::GetRange(const std::string &ppath, DataSink &sink, uint64_t size, uint64_t offset)
{
	if (smb2 == NULL)
	{
		sprintf(response, "SMB client is not connected");
		return 0;
	}

	std::string path = std::string(ppath);
	path = Util::Trim(path, "/");
	struct smb2fh *in = smb2_open(smb2, path.c_str(), O_RDONLY);
	if (in == NULL)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		return 0;
	}

	int ret = this->GetRange((void *)in, sink, size, offset);
	smb2_close(smb2, in);

	return ret;
}

int SmbClient::GetRange(void *fp, DataSink &sink, uint64_t size, uint64_t offset)
{
	struct smb2fh *in = (struct smb2fh *)fp;

	if (smb2 == NULL)
	{
		sprintf(response, "SMB client is not connected");
		return 0;
	}

	if (size == 0)
	{
		return 1;
	}

	// Never ask for more than the server negotiated, libsmb2 would silently
	// clip the request and we would get a short read on every iteration.
	uint32_t chunk_size = max_read_size;
	uint32_t negotiated_size = smb2_get_max_read_size(smb2);
	if (negotiated_size > 0 && negotiated_size < chunk_size)
	{
		chunk_size = negotiated_size;
	}

	uint8_t *buff = (uint8_t *)malloc(chunk_size);
	if (buff == NULL)
	{
		sprintf(response, "Failed to allocate %u bytes", chunk_size);
		return 0;
	}

	if (smb2_lseek(smb2, in, offset, SEEK_SET, NULL) < 0)
	{
		sprintf(response, "%s", smb2_get_error(smb2));
		free(buff);
		return 0;
	}

	size_t bytes_remaining = size;
	while (bytes_remaining > 0)
	{
		uint32_t bytes_to_read = (uint32_t)std::min<size_t>(chunk_size, bytes_remaining);
		int count = smb2_read(smb2, in, buff, bytes_to_read);

		if (count < 0)
		{
			sprintf(response, "%s", smb2_get_error(smb2));
			free(buff);
			return 0;
		}

		if (count == 0)
		{
			// The file ended before the requested range was satisfied. Report
			// an error, otherwise the caller would re-request the same offset
			// forever.
			snprintf(response, sizeof(response), "Unexpected end of file at offset %" PRIu64,
			         offset + (size - bytes_remaining));
			free(buff);
			return 0;
		}

		bytes_remaining -= count;

		if (!sink.write((char *)buff, count))
		{
			free(buff);
			return 0;
		}
	}

	free(buff);

	return 1;
}
