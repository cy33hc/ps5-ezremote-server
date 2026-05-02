#include <curl/curl.h>
#include <json-c/json.h>
#include <fstream>
#include <algorithm>
#include "clients/remote_client.h"
#include "clients/github.h"
#include "config.h"
#include "util.h"

int GithubClient::Connect(const std::string &url, const std::string &username, const std::string &password, bool send_ping)
{
    if (url.find("https://github.com") == std::string::npos)
        return 0;

    this->host_url = "https://api.github.com";
    this->base_path = "/repos" + url.substr(18);
    Util::Rtrim(this->base_path, "/");
    this->base_path += "/releases";
    this->m_download_url = "https://github.com";


    client = new CHTTPClient([](const std::string& log){});
    client->SetBasicAuth(username, password);
    client->InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);

    this->connected = true;
    return 1;
}

int GithubClient::GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset)
{
    if (!ParseReleases())
        return 0;

    std::vector<std::string> path_parts = Util::Split(path, "/");
    
    if (path_parts.size() != 2)
    {
        return 0;
    }

    CHTTPClient::HttpResponse res;
    CHTTPClient::HeadersMap headers;

    char range_header[128];
    sprintf(range_header, "bytes=%lu-%lu", offset, offset + size - 1);
    headers["Range"] = range_header;

    std::string encoded_url = this->m_download_url + CHTTPClient::EncodeUrl(m_assets[path_parts[0]][path_parts[1]].url);
    if (client->Get(encoded_url, headers, res))
    {
        uint64_t len = MIN(size, res.strBody.size());
        sink.write(res.strBody.data(), len);
        return 1;
    }
    else
    {
        sprintf(this->response, "%s", res.errMessage.c_str());
    }
    
    return 0;
}

bool GithubClient::ParseReleases()
{
    CHTTPClient::HeadersMap headers;
    CHTTPClient::HttpResponse res;

    if (!releases_parsed)
    {
        std::string encoded_url = this->host_url + this->base_path + "?per_page=100&page=1";
        if (client->Get(encoded_url, headers, res))
        {
            if (HTTP_SUCCESS(res.iCode))
            {
                json_object *jobj = json_tokener_parse(res.strBody.data());
                struct array_list *areleases = json_object_get_array(jobj);

                for (size_t release_idx = 0; release_idx < areleases->length; ++release_idx)
                {
                    GitRelease release_entry;

                    json_object *release = (json_object *)array_list_get_idx(areleases, release_idx);
                    release_entry.name = std::string(json_object_get_string(json_object_object_get(release, "tag_name")));

                    json_object *obj_assets = json_object_object_get(release, "assets");
                    if (json_object_get_type(obj_assets) == json_type_array)
                    {
                        struct array_list *aassets = json_object_get_array(obj_assets);
                        std::map<std::string, GitAsset> assets;

                        for (size_t asset_idx = 0; asset_idx < aassets->length; ++asset_idx)
                        {
                            GitAsset asset_entry;

                            json_object *asset = (json_object *)array_list_get_idx(aassets, asset_idx);
                            asset_entry.name = std::string(json_object_get_string(json_object_object_get(asset, "name")));
                            asset_entry.size = json_object_get_int64(json_object_object_get(asset, "size"));
                            std::string date_time = std::string(json_object_get_string(json_object_object_get(asset, "updated_at")));
                            asset_entry.url = std::string(json_object_get_string(json_object_object_get(asset, "browser_download_url")));
                            Util::ReplaceAll(asset_entry.url, "https://github.com", "");

                            assets.insert(std::make_pair(asset_entry.name, asset_entry));
                        }

                        m_assets.insert(std::make_pair(release_entry.name, assets));
                    }

                    m_releases.push_back(release_entry);
                }

                releases_parsed = true;
                return 1;
            }
        }
        return 0;
    }

    return 1;
}
