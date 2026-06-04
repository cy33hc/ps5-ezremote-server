#include <fstream>
#include <curl/curl.h>
#include <sys/time.h>
#include "sceSystemService.h"
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

int BaseClient::SetCookies(Headers &headers)
{
    if (this->cookies.size() > 0)
    {
        std::string cookie;
        for (std::map<std::string, std::string>::iterator it = this->cookies.begin(); it != this->cookies.end();)
        {
            cookie.append(it->first).append("=").append(it->second);
            if (std::next(it, 1) != this->cookies.end())
            {
                cookie.append("; ");
            }
            ++it;
        }
        headers.emplace("Cookie", cookie);
    }

    return 1;
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

    client = new httplib::Client(this->host_url);
    if (username.length() > 0)
        client->set_basic_auth(username, password);
    client->set_keep_alive(true);
    client->set_follow_location(true);
    client->set_connection_timeout(30);
    client->set_read_timeout(30);
    client->enable_server_certificate_verification(false);
    this->connected = true;

    return 1;
}

int BaseClient::Get(const std::string &outputfile, const std::string &path, uint64_t offset)
{
    std::ofstream file_stream;
    if (offset > 0)
    {
        file_stream.open(outputfile, std::ofstream::out | std::ofstream::binary | std::ofstream::app);
    }
    else
    {
        file_stream.open(outputfile, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc);
    }

    *g_bytes_transfered =  offset;
    Headers headers;
    SetCookies(headers);
    
    if (offset > 0)
    {
        char range_header[128];
        sprintf(range_header, "bytes=%lu-", offset);
        headers.emplace("Range", range_header);
    }

    if (auto res = client->Get(GetFullPath(path), headers,
                               [&](const char *data, size_t data_length)
                               {
                                   file_stream.write(data, data_length);
                                   *g_bytes_transfered = *g_bytes_transfered + data_length;
                                   return true;
                               }))
    {
        file_stream.close();
        return 1;
    }
    else
    {
        sprintf(this->response, "%s", httplib::to_string(res.error()).c_str());
    }

    return 0;
}

int BaseClient::GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset)
{
    char range_header[64];
    sprintf(range_header, "bytes=%lu-%lu", offset, offset + size - 1);
    Headers headers = {{"Range", range_header}};
    SetCookies(headers);

    size_t bytes_read = 0;
    if (auto res = client->Get(GetFullPath(path), headers,
                               [&](const char *data, size_t data_length)
                               {
                                   bytes_read += data_length;
                                   bool ok = sink.write(data, data_length);
                                   return ok;
                               }))
    {
        return bytes_read == size;
    }
    else
    {
        sprintf(this->response, "%s", httplib::to_string(res.error()).c_str());
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
        delete client;
        client = nullptr;
    }
    return 1;
}
