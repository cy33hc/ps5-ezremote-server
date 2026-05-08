#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <cstring>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include "sceSystemService.h"
#include "clients/nfsclient.h"
#include "config.h"
#include "fs.h"

#define BUF_SIZE 1048576

NfsClient::NfsClient()
{
}

NfsClient::~NfsClient()
{
}

int NfsClient::Connect(const std::string &url, const std::string &user, const std::string &pass)
{
	nfs = nfs_init_context();
	if (nfs == nullptr)
	{
		sprintf(response, "%s", "Failed to init nfs context");
		return 0;
	}

	struct nfs_url *nfsurl = nfs_parse_url_full(nfs, url.c_str());
	if (nfsurl == nullptr) {
		sprintf(response, "%s", nfs_get_error(nfs));
		nfs_destroy_context(nfs);
		return 0;
	}

	std::string export_path = std::string(nfsurl->path) + nfsurl->file;
	int ret = nfs_mount(nfs, nfsurl->server, export_path.c_str());
	if (ret != 0)
	{
		sprintf(response, "%s", nfs_get_error(nfs));
		nfs_destroy_url(nfsurl);
		nfs_destroy_context(nfs);
		nfs = nullptr;
		return 0;
	}
	nfs_destroy_url(nfsurl);

	connected = true;
	return 1;
}

/*
 * LastResponse - return a pointer to the last response received
 */
const char *NfsClient::LastResponse()
{
	return (const char *)response;
}


/*
 * Quit - disconnect from remote
 *
 * return 1 if successful, 0 otherwise
 */
int NfsClient::Quit()
{
	if (nfs != nullptr)
	{
		nfs_umount(nfs);
		nfs_destroy_context(nfs);
		nfs = nullptr;
	}
	connected = false;
	return 1;
}

/*
 * Get - issue a GET command and write received data to output
 *
 * return 1 if successful, 0 otherwise
 */

int NfsClient::Get(const std::string &outputfile, const std::string &ppath, uint64_t offset)
{
	struct nfsfh *nfsfh = nullptr;
	int ret = nfs_open(nfs, ppath.c_str(), 0400, &nfsfh);
	if (ret != 0)
	{
		sprintf(response, "%s", nfs_get_error(nfs));
		return 0;
	}

	FILE* out = FS::Create(outputfile);
	if (out == NULL)
	{
		// sprintf(response, "%s", lang_strings[STR_FAILED]);
		return 0;
	}

	void *buff = malloc(BUF_SIZE);
	int count = 0;
	*g_bytes_transfered = 0;

	while ((count = nfs_read(nfs, nfsfh, BUF_SIZE, buff)) > 0)
	{
		if (count < 0)
		{
			sprintf(response, "%s", nfs_get_error(nfs));
			FS::Close(out);
			nfs_close(nfs, nfsfh);
			free((void*)buff);
			return 0;
		}
		FS::Write(out, buff, count);
		*g_bytes_transfered += count;
		sceSystemServicePowerTick();
	}

	FS::Close(out);
	nfs_close(nfs, nfsfh);
	free((void*)buff);

	return 1;
}



int NfsClient::GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset)
{
	struct nfsfh *nfsfh = nullptr;
	int ret = nfs_open(nfs, path.c_str(), 0400, &nfsfh);
	if (ret != 0)
	{
		return 0;
	}

	ret = this->GetRange((void *)nfsfh, sink, size, offset);
	nfs_close(nfs, nfsfh);

	return ret;
}

int NfsClient::GetRange(void *fp, DataSink &sink, uint64_t size, uint64_t offset)
{
	struct nfsfh *nfsfh = (struct nfsfh *)fp;

	int ret = nfs_lseek(nfs, nfsfh, offset, SEEK_SET, NULL);
	if (ret != 0)
	{
		return 0;
	}

	void *buff = malloc(BUF_SIZE);
	int count = 0;
	size_t bytes_remaining = size;
	do
	{
		size_t bytes_to_read = std::min<size_t>(BUF_SIZE, bytes_remaining);
		count = nfs_read(nfs, nfsfh, bytes_to_read, buff);
		if (count > 0)
		{
			bytes_remaining -= count;
			bool ok = sink.write((char *)buff, count);
			if (!ok)
			{
				free((void *)buff);
				return 0;
			}
		}
		else
		{
			break;
		}
	} while (1);

	free((void *)buff);
	return 1;
}
