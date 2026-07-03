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
#define BG_DOWNLOAD_HISTORY_PATH DATA_PATH "/bg_download_history.json"
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

#define MAX_PKG_HISTORY_RETENTION 1209600000000L

enum DownloadState { STATE_PENDING, STATE_DOWNLOADING, STATE_RESUMED, STATE_FAILED, STATE_SUCCESS };

struct HostInfo
{
    int type;
    std::string url;
    std::string http_server_type;
    std::string username;
    std::string password;
    RemoteClient *client;
};

struct PackageInstallData
{
    HostInfo host_info;
    std::string path;
    uint64_t timestamp;
};

struct BgDownloadData {
    HostInfo host_info;
    std::string src_path;
    std::string dest_path;
    uint64_t bytes_transfered;
    uint64_t file_size;
    DownloadState state;
    uint64_t id;
    uint64_t timestamp;
    int32_t failed_attempts;
};

extern uint64_t *g_bytes_transfered;
extern std::vector<BgDownloadData> bg_download_list;

namespace CONFIG
{
    PackageInstallData* GetPackageInstallHostData(const std::string &hash);
    void AddPackageInstallHostData(const std::string &hash, PackageInstallData pkg_data);
    void RemovePackageInstallHostData(const std::string &hash);
    void LoadPackageInstallHostData();
    void SavePackageInstallHostData();
    void AddBgDownloadData(BgDownloadData bg_download_data);
    void LoadBgDownloadData();
    void SaveBgDownloadData();
}
#endif
