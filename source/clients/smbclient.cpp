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


struct AsyncOpenContext
{
    struct smb2fh *fh;
    int result;
    bool complete;
};

static void smb2_async_open_cb(struct smb2_context *smb2, int status, void *command_data, void *private_data)
{
    AsyncOpenContext *ctx = (AsyncOpenContext *)private_data;
    if (status < 0)
    {
        ctx->fh = NULL;
        ctx->result = 0;
    }
    else
    {
        ctx->fh = (struct smb2fh *)command_data;
        ctx->result = 1;
    }
    ctx->complete = true;
}

static void smb2_async_close_cb(struct smb2_context *smb2, int status, void *command_data, void *private_data)
{
    bool *complete = (bool *)private_data;
    *complete = true;
}

int SmbClient::GetRange(const std::string &ppath, DataSink &sink, uint64_t size, uint64_t offset)
{
    std::string path = std::string(ppath);
    path = Util::Trim(path, "/");

    // Async open
    AsyncOpenContext open_ctx = {};
    open_ctx.fh = NULL;
    open_ctx.result = 0;
    open_ctx.complete = false;

    int ret = smb2_open_async(smb2, path.c_str(), O_RDONLY, smb2_async_open_cb, &open_ctx);
    if (ret != 0)
        return 0;

    struct pollfd pfd;
    while (!open_ctx.complete)
    {
        pfd.fd = smb2_get_fd(smb2);
        pfd.events = smb2_which_events(smb2);
        if (poll(&pfd, 1, 1000) < 0)
            return 0;
        if (pfd.revents == 0)
            continue;
        if (smb2_service(smb2, pfd.revents) < 0)
            return 0;
    }

    if (open_ctx.fh == NULL)
        return 0;


    // Async read
    int result = this->GetRange((void *)open_ctx.fh, sink, size, offset);

    // Async close
    bool close_complete = false;
    smb2_close_async(smb2, open_ctx.fh, smb2_async_close_cb, &close_complete);
    while (!close_complete)
    {
        pfd.fd = smb2_get_fd(smb2);
        pfd.events = smb2_which_events(smb2);
        if (poll(&pfd, 1, 1000) < 0)
            break;
        if (pfd.revents == 0)
            continue;
        if (smb2_service(smb2, pfd.revents) < 0)
            break;
    }

    return result;
}

struct AsyncReadContext
{
    DataSink *sink;
    uint8_t *buff;
    size_t bytes_remaining;
    int result;
    bool complete;
    struct smb2_context *smb2;
    struct smb2fh *fh;
    uint32_t max_read_size;
};

static void smb2_async_read_cb(struct smb2_context *smb2, int status, void *command_data, void *private_data)
{
    AsyncReadContext *ctx = (AsyncReadContext *)private_data;

    if (status < 0)
    {
        ctx->result = 0;
        ctx->complete = true;
        return;
    }

    if (status == 0)
    {
        ctx->complete = true;
        return;
    }

    bool ok = ctx->sink->write((char *)ctx->buff, status);
    if (!ok)
    {
        ctx->result = 0;
        ctx->complete = true;
        return;
    }

    ctx->bytes_remaining -= status;
    if (ctx->bytes_remaining == 0)
    {
        ctx->complete = true;
        return;
    }

    size_t bytes_to_read = std::min<size_t>(ctx->max_read_size, ctx->bytes_remaining);
    int ret = smb2_pread_async(ctx->smb2, ctx->fh, ctx->buff, bytes_to_read, 0, smb2_async_read_cb, ctx);
    if (ret != 0)
    {
        ctx->result = 0;
        ctx->complete = true;
    }
}

static void smb2_async_lseek_cb(struct smb2_context *smb2, int status, void *command_data, void *private_data)
{
    AsyncReadContext *ctx = (AsyncReadContext *)private_data;

    if (status < 0)
    {
        ctx->result = 0;
        ctx->complete = true;
        return;
    }

    size_t bytes_to_read = std::min<size_t>(ctx->max_read_size, ctx->bytes_remaining);
    int ret = smb2_pread_async(ctx->smb2, ctx->fh, ctx->buff, bytes_to_read, 0, smb2_async_read_cb, ctx);
    if (ret != 0)
    {
        ctx->result = 0;
        ctx->complete = true;
    }
}

int SmbClient::GetRange(void *fp, DataSink &sink, uint64_t size, uint64_t offset)
{
    struct smb2fh *in = (struct smb2fh *)fp;

    uint8_t *buff = (uint8_t *)malloc(max_read_size);
    if (buff == NULL)
        return 0;

    AsyncReadContext ctx = {};
    ctx.sink = &sink;
    ctx.buff = buff;
    ctx.bytes_remaining = size;
    ctx.result = 1;
    ctx.complete = false;
    ctx.smb2 = smb2;
    ctx.fh = in;
    ctx.max_read_size = max_read_size;

    // Kick off async lseek, which chains into async reads
    int ret = smb2_lseek_async(smb2, in, offset, SEEK_SET, smb2_async_lseek_cb, &ctx);
    if (ret != 0)
    {
        free(buff);
        return 0;
    }

    // Service the event loop until transfer is complete
    struct pollfd pfd;
    while (!ctx.complete)
    {
        pfd.fd = smb2_get_fd(smb2);
        pfd.events = smb2_which_events(smb2);
        if (poll(&pfd, 1, 1000) < 0)
        {
            ctx.result = 0;
            break;
        }
        if (pfd.revents == 0)
            continue;
        if (smb2_service(smb2, pfd.revents) < 0)
        {
            ctx.result = 0;
            break;
        }
    }

    free(buff);
    return ctx.result;
}
