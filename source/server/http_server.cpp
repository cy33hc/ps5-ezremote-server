#include <string>
#include <json-c/json.h>
#include "http/httplib.h"
#include "server/http_server.h"
#include "clients/remote_client.h"
#include "clients/archiveorg.h"
#include "clients/baseclient.h"
#include "clients/ftpclient.h"
#include "clients/nfsclient.h"
#include "clients/smbclient.h"
#include "clients/sftpclient.h"
#include "clients/webdav.h"
#include "config.h"
#include "fs.h"
#include "util.h"

#define SUCCESS_MSG "{ \"result\": { \"success\": true, \"error\": null } }"
#define FAILURE_MSG "{ \"result\": { \"success\": false, \"error\": \"%s\" } }"
#define SUCCESS_MSG_LEN 48
#define PKG_INITIAL_REQUEST_SIZE 8388608ul

using namespace httplib;

enum DownloadState { STATE_PENDING, STATE_DOWNLOADING, STATE_FAILED, STATE_SUCCESS };

struct BgDownloadData {
    HostInfo host_info;
    std::string src_path;
    std::string dest_path;
    uint64_t bytes_transfered;
    uint64_t file_size;
    DownloadState state;
    uint64_t id;
};

Server *svr;
int http_server_port = 6701;
static std::vector<BgDownloadData> bg_download_list;
static pthread_t bg_download_thread;

namespace HttpServer
{
    static int FtpCallback(int64_t xfered, void *arg)
    {
        return 1;
    }

    std::string dump_headers(const Headers &headers)
    {
        std::string s;
        char buf[BUFSIZ];

        for (auto it = headers.begin(); it != headers.end(); ++it)
        {
            const auto &x = *it;
            snprintf(buf, sizeof(buf), "%s: %s\n", x.first.c_str(), x.second.c_str());
            s += buf;
        }

        return s;
    }

    std::string log(const Request &req, const Response &res)
    {
        std::string s;
        char buf[BUFSIZ];

        s += "================================\n";

        snprintf(buf, sizeof(buf), "%s %s %s", req.method.c_str(),
                 req.version.c_str(), req.path.c_str());
        s += buf;

        std::string query;
        for (auto it = req.params.begin(); it != req.params.end(); ++it)
        {
            const auto &x = *it;
            snprintf(buf, sizeof(buf), "%c%s=%s",
                     (it == req.params.begin()) ? '?' : '&', x.first.c_str(),
                     x.second.c_str());
            query += buf;
        }
        snprintf(buf, sizeof(buf), "%s\n", query.c_str());
        s += buf;

        s += dump_headers(req.headers);

        s += "--------------------------------\n";

        snprintf(buf, sizeof(buf), "%d %s\n", res.status, res.version.c_str());
        s += buf;
        s += dump_headers(res.headers);
        s += "\n";

        if (!res.body.empty())
        {
            s += res.body;
        }

        s += "\n";

        return s;
    }

    void failed(Response &res, int status, const std::string &msg)
    {
        res.status = status;
        char response_msg[msg.length() + strlen(FAILURE_MSG) + 2];
        snprintf(response_msg, sizeof(response_msg), "{ \"result\": { \"success\": false, \"error\": \"%s\" } }", msg.c_str());
        res.set_content(response_msg, strlen(response_msg), "application/json");
        return;
    }

    void bad_request(Response &res, const std::string &msg)
    {
        failed(res, 200, msg);
        return;
    }

    void success(Response &res)
    {
        res.status = 200;
        res.set_content(SUCCESS_MSG, SUCCESS_MSG_LEN, "application/json");
        return;
    }

    static RemoteClient *GetRemoteClient(HostInfo *host_info)
    {
        RemoteClient *tmp_client = nullptr;
        if (host_info->type == CLIENT_TYPE_HTTP_SERVER)
        {
            if (host_info->http_server_type.compare(HTTP_SERVER_ARCHIVEORG))
            {
                tmp_client = new ArchiveOrgClient();
            }
            else if (host_info->http_server_type.compare(HTTP_SERVER_APACHE))
            {
                tmp_client = new BaseClient();
            }
            else if (host_info->http_server_type.compare(HTTP_SERVER_MS_IIS))
            {
                tmp_client = new BaseClient();
            }
            else if (host_info->http_server_type.compare(HTTP_SERVER_NGINX))
            {
                tmp_client = new BaseClient();
            }
            else if (host_info->http_server_type.compare(HTTP_SERVER_RCLONE))
            {
                tmp_client = new BaseClient();
            }
            else if (host_info->http_server_type.compare(HTTP_SERVER_NPX_SERVE))
            {
                tmp_client = new BaseClient();
            }
        }
        else if (host_info->type == CLIENT_TYPE_SMB)
        {
            tmp_client = new SmbClient();
        }
        else if (host_info->type == CLIENT_TYPE_FILEHOST)
        {
            tmp_client = new BaseClient();
        }
        else if (host_info->type == CLIENT_TYPE_WEBDAV)
        {
            tmp_client = new WebDAVClient();
        }
        else if (host_info->type == CLIENT_TYPE_SFTP)
        {
            tmp_client = new SFTPClient();
        }
        else if (host_info->type == CLIENT_TYPE_NFS)
        {
            tmp_client = new NfsClient();
        }
        else if (host_info->type == CLIENT_TYPE_FTP)
        {
            tmp_client = new FtpClient();
            FtpClient *ftp_client = (FtpClient*) tmp_client;
            ftp_client->SetCallbackXferFunction(FtpCallback);
        }

        if (tmp_client != nullptr)
            tmp_client->Connect(host_info->url, host_info->username, host_info->password);

        return tmp_client;
    }

    static void DeleteRemoteClient(RemoteClient *tmp_client)
    {
        tmp_client->Quit();
        delete tmp_client;
    }

    void *DownloadFilesThread(void *argp)
    {
        char temp_file[2049];

        while (true)
        {
            for (int i=0; i < bg_download_list.size(); i++)
            {
                if (bg_download_list[i].state == STATE_PENDING)
                {
                    RemoteClient *tmp_client = GetRemoteClient(&(bg_download_list[i].host_info));
                    g_bytes_transfered = &(bg_download_list[i].bytes_transfered);
                    bg_download_list[i].state = STATE_DOWNLOADING;
                    snprintf(temp_file, sizeof(temp_file), "%s.tmp", bg_download_list[i].dest_path.c_str());
                    Util::RichNotify(bg_download_list[i].id, "Started download %s", bg_download_list[i].dest_path.c_str());
                    int ret = tmp_client->Get(temp_file, bg_download_list[i].src_path);
                    FS::Rename(temp_file, bg_download_list[i].dest_path);
                    if (ret == 0)
                    {
                        bg_download_list[i].state = STATE_FAILED;
                        Util::RichNotify(bg_download_list[i].id, "Failed to download %s", bg_download_list[i].dest_path.c_str());
                    }
                    else
                    {
                        Util::RichNotify(bg_download_list[i].id, "Completed download %s", bg_download_list[i].dest_path.c_str());
                        bg_download_list[i].state = STATE_SUCCESS;
                    }
                    DeleteRemoteClient(tmp_client);
                }
            }

            sleep(1);
        }

        return nullptr;
    }

    void *ServerThread(void *argp)
    {
        svr->Get("/", [&](const Request &req, Response &res)
                 { res.set_redirect("/index.html"); });

        svr->Post("/store_bg_install_data", [&](const Request &req, Response &res)
        {
            const char *hash_param;
            const char *path_param;
            const char *url_param;
            const char *username_param;
            const char *password_param;
            const char *http_server_type_param;
            int type_param;

            json_object *jobj = json_tokener_parse(req.body.c_str());
            if (jobj != nullptr)
            {
                hash_param = json_object_get_string(json_object_object_get(jobj, "hash"));
                url_param = json_object_get_string(json_object_object_get(jobj, "url"));
                path_param = json_object_get_string(json_object_object_get(jobj, "path"));
                username_param  = json_object_get_string(json_object_object_get(jobj, "username"));
                password_param = json_object_get_string(json_object_object_get(jobj, "password"));
                http_server_type_param = json_object_get_string(json_object_object_get(jobj, "http_server_type"));
                type_param = json_object_get_int(json_object_object_get(jobj, "type"));

                if (url_param == nullptr || hash_param == nullptr)
                {
                    bad_request(res, "Required url_param or hash parameter missing");
                    return;
                }

                PackageInstallData pkg_data;
                pkg_data.host_info.url = url_param;
                if (username_param != nullptr)
                    pkg_data.host_info.username = username_param;
                if (password_param != nullptr)
                    pkg_data.host_info.password = password_param;
                if (path_param != nullptr)
                    pkg_data.path = path_param;
                if (http_server_type_param != nullptr)
                    pkg_data.host_info.http_server_type = http_server_type_param;
                pkg_data.timestamp = Util::GetTick();
                pkg_data.host_info.type = type_param;

                CONFIG::AddPackageInstallHostData(hash_param, pkg_data);
                CONFIG::SavePackageInstallHostData();
            }
        });

        svr->Get("/bg_install/(.*)", [&](const Request &req, Response &res)
        {
            std::string hash = req.matches[1];
            PackageInstallData* pkg_host_data = CONFIG::GetPackageInstallHostData(hash);

            if (pkg_host_data == nullptr)
            {
                failed(res, 500, "Cannot resume background install of " + hash + ". Host data not found.");
                return;
            }

            RemoteClient *tmp_client = GetRemoteClient(&(pkg_host_data->host_info));
            if (tmp_client == nullptr)
            {
                res.status = 500;
                return;
            }

            std::string path = pkg_host_data->path;

            res.status = 206;
            size_t range_len = (req.ranges[0].second - req.ranges[0].first) + 1;
                
            std::pair<ssize_t, ssize_t> range = req.ranges[0];
            res.set_content_provider(
                range_len, "application/octet-stream",
                [tmp_client, path, range, range_len](size_t offset, size_t length, DataSink &sink) {
                    int ret;
                    ret = tmp_client->GetRange(path, sink, range_len, range.first);
                    return (ret==1);
                },
                [tmp_client](bool success) {
                    DeleteRemoteClient(tmp_client);
                });

        });

        svr->Post("/download_url", [&](const Request &req, Response &res)
        {
            int type_param;
            const char *url_param;
            const char *username_param;
            const char *password_param;
            const char *http_server_type_param;
            const char *src_path_param;
            const char *dest_path_param;
            uint64_t file_size_param;
            uint64_t id_param;

            json_object *jobj = json_tokener_parse(req.body.c_str());
            if (jobj != nullptr)
            {
                type_param = json_object_get_int(json_object_object_get(jobj, "type"));
                url_param = json_object_get_string(json_object_object_get(jobj, "url"));
                username_param  = json_object_get_string(json_object_object_get(jobj, "username"));
                password_param = json_object_get_string(json_object_object_get(jobj, "password"));
                http_server_type_param = json_object_get_string(json_object_object_get(jobj, "http_server_type"));
                src_path_param = json_object_get_string(json_object_object_get(jobj, "src_path"));
                dest_path_param = json_object_get_string(json_object_object_get(jobj, "dest_path"));
                file_size_param = json_object_get_uint64(json_object_object_get(jobj, "size"));
                id_param = json_object_get_uint64(json_object_object_get(jobj, "id"));

                if (url_param == nullptr || src_path_param == nullptr || dest_path_param == nullptr)
                {
                    bad_request(res, "Required parameters are missing");
                    return;
                }

                BgDownloadData download_data;
                download_data.host_info.url = url_param;
                download_data.host_info.type = type_param;
                download_data.src_path = src_path_param;
                download_data.dest_path = dest_path_param;
                download_data.file_size = file_size_param;
                download_data.state = STATE_PENDING;
                download_data.id = id_param;

                if (username_param != nullptr)
                    download_data.host_info.username = username_param;
                if (password_param != nullptr)
                    download_data.host_info.password = password_param;
                if (http_server_type_param != nullptr)
                    download_data.host_info.http_server_type = http_server_type_param;

                bg_download_list.push_back(download_data);
            }
        });

        svr->Get("/get_download_state", [&](const Request &req, Response &res)
        {
            json_object *download_list = json_object_new_array();

            for (int i=0; i < bg_download_list.size(); i++)
            {
                json_object *download_item_obj = json_object_new_object();
                json_object_object_add(download_item_obj, "path", json_object_new_string(bg_download_list[i].dest_path.c_str()));
                json_object_object_add(download_item_obj, "bytes_transfered", json_object_new_uint64(bg_download_list[i].bytes_transfered));
                json_object_object_add(download_item_obj, "file_size", json_object_new_uint64(bg_download_list[i].file_size));
                json_object_object_add(download_item_obj, "state", json_object_new_int(bg_download_list[i].state));
                json_object_array_add(download_list, download_item_obj);
            }
            
            const char *payload_str = json_object_to_json_string(download_list);

            res.status = 200;
            res.set_content(payload_str, "application/json");
        });
 
        svr->Get("/stop", [&](const Request & /*req*/, Response & /*res*/)
        {
            svr->stop();
        });

        svr->Get("/version", [&](const Request & req, Response &res)
        {
            res.status = 200;
            char version[20];
            sprintf(version, "%.2f", EZREMOTE_VERSION);
            res.set_content(version, "text/html");
        });

        svr->set_error_handler([](const Request & /*req*/, Response &res)
        {
            const char *fmt = "<p>Error Status: <span style='color:red;'>%d</span></p>";
            char buf[BUFSIZ];
            snprintf(buf, sizeof(buf), fmt, res.status);
            res.set_content(buf, "text/html");
        });

        /*
        svr->set_logger([](const Request &req, const Response &res)
        {
            dbglogger_log("%s", log(req, res).c_str());
        });
        */
       
        svr->set_payload_max_length(1024 * 1024 * 12);
        svr->set_tcp_nodelay(true);
        svr->set_mount_point("/", "/");

        svr->listen("0.0.0.0", http_server_port);

        return NULL;
    }

    void Start()
    {
        if (svr == nullptr)
            svr = new Server();
        if (!svr->is_valid())
        {
            return;
        }

        Util::Notify("Starting ezRemote Server %.2f on port %d", EZREMOTE_VERSION, http_server_port);
        ServerThread(nullptr);
    }

    void Stop()
    {
        if (svr != nullptr)
            svr->stop();
    }

    void StartDownloadThread()
    {
        pthread_create(&bg_download_thread, NULL, DownloadFilesThread, NULL);
    }
}
