#include <lexbor/html/parser.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/node.h>
#include <fstream>
#include <map>
#include "config.h"
#include "clients/remote_client.h"
#include "clients/archiveorg.h"
#include "util.h"

std::string ArchiveOrgClient::GenerateRandomId(const int len)
{
    static const char alphanum[] = "0123456789abcdef";
    std::string tmp_s;
    tmp_s.reserve(len);

    for (int i = 0; i < len; ++i) {
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    
    return tmp_s;
}

int ArchiveOrgClient::Connect(const std::string &url, const std::string &username, const std::string &password)
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
    client->InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);

    client->SetCookie("donation-identifier", GenerateRandomId(32));
    client->SetCookie("test-cookie", "1");
    client->SetCookie("abtest-identifier", GenerateRandomId(32));

    if (username.length() > 0)
        return Login(username, password);
    this->connected = true;
    return 1;
}

int ArchiveOrgClient::Login(const std::string &username, const std::string &password)
{
    CHTTPClient::HeadersMap headers;
    CHTTPClient::HttpResponse res;

    std::string encoded_path = this->host_url + CHTTPClient::EncodeUrl("/account/login");
    CHTTPClient::PostFormInfo formdata;
    formdata.AddFormContent("username", username);
    formdata.AddFormContent("password", password);
    formdata.AddFormContent("remember", "true");
    formdata.AddFormContent("referer", "https://archive.org/");
    formdata.AddFormContent("login", "true");
    formdata.AddFormContent("submit_by_js", "true");

    if (client->UploadForm(encoded_path, headers, formdata, res))
    {
        if (res.cookies.size() > 0)
        {
            for (CHTTPClient::HeadersMap::iterator it = res.cookies.begin(); it != res.cookies.end();)
            {
                this->client->SetCookie(it->first, it->second);
                ++it;
            }
        }
        this->connected = true;
        return 1;
    }

    return 0;
}