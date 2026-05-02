#ifndef EZ_GITHUB_H
#define EZ_GITHUB_H

#include <string>
#include <vector>
#include "clients/remote_client.h"
#include "clients/baseclient.h"

class GithubClient : public BaseClient
{
public:
    int Connect(const std::string &url, const std::string &username, const std::string &password, bool send_ping=false);
    int GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset);

private:
    struct GitAsset
    {
        std::string name;
        std::string url;
        uint64_t size;
    };

    struct GitRelease
    {
        std::string name;
    };

    std::vector<GitRelease> m_releases;
    std::map<std::string, std::map<std::string, GitAsset>> m_assets;
    bool releases_parsed = false;
    std::string m_download_url;

    bool ParseReleases();
};

#endif
