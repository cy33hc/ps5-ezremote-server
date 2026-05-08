#include <fstream>
#include <curl/curl.h>
#include <sys/time.h>
#include "clients/remote_client.h"
#include "clients/baseclient.h"
#include "config.h"
#include "util.h"

using httplib::Client;
using httplib::DataSink;
using httplib::Headers;
using httplib::Result;

BaseClient::BaseClient(){};

BaseClient::~BaseClient()
{
    if (client != nullptr)
        delete client;
};

int BaseClient::DownloadProgressCallback(void* ptr, double dTotalToDownload, double dNowDownloaded, double dTotalToUpload, double dNowUploaded)
{
    CHTTPClient::ProgressFnStruct *progress_data = (CHTTPClient::ProgressFnStruct*) ptr;
    int64_t *bytes_transfered = (int64_t *) progress_data->pOwner;
	*bytes_transfered = dNowDownloaded;
    return 0;
}

int BaseClient::Connect(const std::string &url, const std::string &username, const std::string &password)
{
    this->host_url = url;
    size_t scheme_pos = url.find("://");
    size_t root_pos = url.find("/", scheme_pos + 3);
    if (root_pos != std::string::npos)
    {
        this->host_url = url.substr(0, root_pos);
        this->base_path = url.substr(root_pos);
    }

    client = new CHTTPClient([](const std::string& log){});
    if (!username.empty())
    {
        client->SetBasicAuth(username, password);
    }
    client->InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);

    this->connected = true;

    return 1;
}

int BaseClient::Get(const std::string &outputfile, const std::string &path, uint64_t offset)
{
    long status;
    *g_bytes_transfered = 0;

    CHTTPClient::HeadersMap headers;
    client->SetProgressFnCallback(g_bytes_transfered, DownloadProgressCallback);
    std::string encoded_url = this->host_url + CHTTPClient::EncodeUrl(GetFullPath(path));
    if (client->DownloadFile(outputfile, encoded_url, status))
    {
        return 1;
    }
    else
    {
        // sprintf(this->response, "%ld - %s", status, lang_strings[STR_FAIL_DOWNLOAD_MSG]);
    }

    return 0;
}



int BaseClient::GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset)
{
    CHTTPClient::HttpResponse res;
    CHTTPClient::HeadersMap headers;

    char range_header[128];
    sprintf(range_header, "bytes=%lu-%lu", offset, offset + size - 1);
    headers["Range"] = range_header;

    std::string encoded_url = this->host_url + CHTTPClient::EncodeUrl(GetFullPath(path));
    if (client->Get(encoded_url, headers, res))
    {
        uint64_t len = MIN(size, res.strBody.size());
        if (!sink.write(res.strBody.data(), len))
            return 0;
        return 1;
    }
    else
    {
        sprintf(this->response, "%s", res.errMessage.c_str());
    }
    return 0;
}

std::string BaseClient::GetPath(std::string ppath1, std::string ppath2)
{
    std::string path1 = ppath1;
    std::string path2 = ppath2;
    path1 = Util::Trim(Util::Trim(path1, " "), "/");
    path2 = Util::Trim(Util::Trim(path2, " "), "/");
    path1 = this->base_path + ((this->base_path.length() > 0) ? "/" : "") + path1 + "/" + path2;
    if (path1[0] != '/')
        path1 = "/" + path1;
    return path1;
}

std::string BaseClient::GetFullPath(std::string ppath1)
{
    std::string path1 = ppath1;
    path1 = Util::Trim(Util::Trim(path1, " "), "/");
    path1 = this->base_path + "/" + path1;
    Util::ReplaceAll(path1, "//", "/");
    return path1;
}

const char *BaseClient::LastResponse()
{
    return this->response;
}

int BaseClient::Quit()
{
    if (client != nullptr)
    {
        client->CleanupSession();
        delete client;
        client = nullptr;
    }
    return 1;
}
