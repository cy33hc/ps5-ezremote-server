#ifndef EZ_CONFIG_H
#define EZ_CONFIG_H

#include <string>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <set>

#include "clients/remote_client.h"

#define APP_ID "ezremote-client"
#define DATA_PATH "/data/homebrew/" APP_ID
#define PKG_INSTALL_HISTORY_PATH DATA_PATH "/pkg_install_history.json"
#define DEBUG_SERVER_LOG_PATH DATA_PATH "/ezremote_server.log"
#define NOTIFY_ICON_FILE "/user" DATA_PATH "/sce_sys/icon0.png"
#define CLIENT_ELF_PATH DATA_PATH "/ezremote_client.elf"

#define HTTP_SERVER_APACHE "Apache"
#define HTTP_SERVER_MS_IIS "Microsoft IIS"
#define HTTP_SERVER_NGINX "Nginx"
#define HTTP_SERVER_NPX_SERVE "Serve"
#define HTTP_SERVER_RCLONE "RClone"
#define HTTP_SERVER_ARCHIVEORG "Archive.org"
#define HTTP_SERVER_MYRIENT "Myrient"
#define HTTP_SERVER_GITHUB "Github"

#define MAX_PKG_HISTORY_RETENTION 2592000000000L

struct HostInfo
{
    int type;
    std::string http_server_type;
    std::string username;
    std::string password;
    std::string url;
};

struct PackageInstallData
{
    HostInfo host_info;
    std::string path;
    uint64_t timestamp;
};

extern uint64_t *g_bytes_transfered;

namespace CONFIG
{
    PackageInstallData* GetPackageInstallHostData(const std::string &hash);
    void AddPackageInstallHostData(const std::string &hash, PackageInstallData pkg_data);
    void RemovePackageInstallHostData(const std::string &hash);
    void LoadPackageInstallHostData();
    void SavePackageInstallHostData();
}
#endif
