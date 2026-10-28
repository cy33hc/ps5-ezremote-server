#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <time.h>
#include <json-c/json.h>
#include <pthread.h>
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
#include "json_object_ref.h"
#include "util.h"

#define SUCCESS_MSG "{ \"result\": { \"success\": true, \"error\": null } }"
#define FAILURE_MSG "{ \"result\": { \"success\": false, \"error\": \"%s\" } }"
#define SUCCESS_MSG_LEN 48
#define PKG_INITIAL_REQUEST_SIZE 8388608ul

using namespace httplib;

Server *svr;
int http_server_port = 6701;
static pthread_t bg_download_thread;
static uint64_t g_dl_offset;
static bool stop_download = false;
static bool stop_server = false;
static bool in_rest_mode = false;
static pthread_mutex_t download_pause_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t download_pause_cond = PTHREAD_COND_INITIALIZER;

namespace HttpServer
{
    static int DownloadFtpCallback(int64_t xfered, void *arg)
    {
        *g_bytes_transfered = g_dl_offset + xfered;
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

    static const size_t CLIENT_POOL_MAX_IDLE = 12;

    static std::mutex client_pool_mutex;
    static std::map<std::string, std::vector<RemoteClient *>> client_pool_idle;
    static std::map<RemoteClient *, std::string> client_pool_key;
    static std::map<RemoteClient *, time_t> client_pool_idle_since;

    static std::string ClientPoolKey(const HostInfo &host_info)
    {
        return std::to_string(host_info.type) + "|" + host_info.url + "|" +
               host_info.username + "|" + host_info.password;
    }

    static RemoteClient *CreatePooledClient(const HostInfo &host_info)
    {
        RemoteClient *client = nullptr;

        if (host_info.type == CLIENT_TYPE_SMB)
        {
            client = new SmbClient();
        }
        else if (host_info.type == CLIENT_TYPE_SFTP)
        {
            client = new SFTPClient();
        }
        else if (host_info.type == CLIENT_TYPE_NFS)
        {
            client = new NfsClient();
        }
        else if (host_info.type == CLIENT_TYPE_FTP)
        {
            client = new FtpClient();
        }
        else if (host_info.type == CLIENT_TYPE_WEBDAV)
        {
            client = new WebDAVClient();
        }
        else if (host_info.type == CLIENT_TYPE_HTTP_SERVER)
        {
            if (host_info.http_server_type.compare(HTTP_SERVER_ARCHIVEORG) == 0)
            {
                client = new ArchiveOrgClient();
            }
            else
            {
                client = new BaseClient();
            }
        }
        else if (host_info.type == CLIENT_TYPE_FILEHOST)
        {
            client = new BaseClient();
        }

        if (client == nullptr)
        {
            return nullptr;
        }

        client->Connect(host_info.url, host_info.username, host_info.password);

        if (host_info.type == CLIENT_TYPE_HTTP_SERVER &&
            host_info.http_server_type.compare(HTTP_SERVER_GITHUB) == 0)
        {
            BaseClient *base_client = (BaseClient *)client;
            base_client->SetClientPathEncoding(false);
        }

        return client;
    }

    static bool ClientIsConnected(RemoteClient *client)
    {
        return client != nullptr && client->IsConnected();
    }

    static void DestroyPooledClient(RemoteClient *client)
    {
        client->Quit();
        delete client;
    }

    // "reusable" says whether the returned client may be kept in the pool and
    // handed out to another request after it is released. When it is false a
    // fresh connection is created and it is destroyed on release.
    static RemoteClient *AcquireRemoteClient(const HostInfo &host_info, bool reusable)
    {
        const std::string key = ClientPoolKey(host_info);
        std::vector<RemoteClient *> expired;
        RemoteClient *client = nullptr;

        if (reusable)
        {
            std::lock_guard<std::mutex> lock(client_pool_mutex);

            auto idle = client_pool_idle.find(key);
            while (idle != client_pool_idle.end() && !idle->second.empty())
            {
                RemoteClient *candidate = idle->second.back();
                idle->second.pop_back();

                auto idle_since = client_pool_idle_since.find(candidate);
                bool too_old = idle_since != client_pool_idle_since.end() &&
                               (time(NULL) - idle_since->second) > (time_t)candidate->GetMaxIdleTimeout();
                client_pool_idle_since.erase(candidate);

                if (too_old)
                {
                    // The server may have dropped the session while it was
                    // idle. Drop it and try the next one.
                    client_pool_key.erase(candidate);
                    expired.push_back(candidate);
                    continue;
                }

                client = candidate;
                break;
            }
        }

        for (RemoteClient *expired_client : expired)
        {
            DestroyPooledClient(expired_client);
        }

        if (client == nullptr)
        {
            // Connecting can block, so never do it while holding the pool mutex.
            client = CreatePooledClient(host_info);
            if (client == nullptr)
            {
                return nullptr;
            }

            if (reusable)
            {
                std::lock_guard<std::mutex> lock(client_pool_mutex);
                client_pool_key[client] = key;
            }
        }

        return client;
    }

    static void ReleasePooledClient(RemoteClient *client, bool success)
    {
        bool pooled = false;

        {
            std::lock_guard<std::mutex> lock(client_pool_mutex);

            auto key = client_pool_key.find(client);
            if (key != client_pool_key.end() && success && ClientIsConnected(client))
            {
                std::vector<RemoteClient *> &idle = client_pool_idle[key->second];
                if (idle.size() < CLIENT_POOL_MAX_IDLE)
                {
                    client->ResetTransferState();
                    idle.push_back(client);
                    client_pool_idle_since[client] = time(NULL);
                    pooled = true;
                }
            }

            if (!pooled)
            {
                client_pool_key.erase(client);
            }
        }

        if (!pooled)
        {
            DestroyPooledClient(client);
        }
    }

    void ClearClientPool()
    {
        std::vector<RemoteClient *> clients;

        {
            std::lock_guard<std::mutex> lock(client_pool_mutex);

            for (auto &entry : client_pool_idle)
            {
                for (RemoteClient *client : entry.second)
                {
                    clients.push_back(client);
                }
            }

            // Clients still checked out are not destroyed here. Their key is
            // dropped, so they get destroyed when they are released.
            client_pool_idle.clear();
            client_pool_key.clear();
            client_pool_idle_since.clear();
        }

        for (RemoteClient *client : clients)
        {
            DestroyPooledClient(client);
        }
    }

    // Returns a client to the pool. "success" says whether the last operation
    // on it succeeded; a client that failed is never handed out again. Clients
    // that were not acquired as reusable are not in the pool and get destroyed.
    static void ReleaseRemoteClient(RemoteClient *tmp_client, bool success)
    {
        if (tmp_client == nullptr)
        {
            return;
        }

        ReleasePooledClient(tmp_client, success);
    }

    void *DownloadFilesThread(void *argp)
    {
        char temp_file[2049];
        uint64_t tmp_file_size;
        int ret;

        while (!stop_download)
        {
            // Wait while paused (e.g. during rest mode)
            pthread_mutex_lock(&download_pause_mutex);
            while (in_rest_mode && !stop_download)
            {
                pthread_cond_wait(&download_pause_cond, &download_pause_mutex);
            }
            pthread_mutex_unlock(&download_pause_mutex);

            if (stop_download)
                break;

            for (int i=0; i < bg_download_list.size(); i++)
            {
                if (bg_download_list[i].state == STATE_PENDING)
                {
                    // The pool hands a client out exclusively, so the download
                    // owns its connection for the whole file and returns it to
                    // the pool when it is done.
                    RemoteClient *tmp_client = AcquireRemoteClient(bg_download_list[i].host_info, true);
                    if (tmp_client == nullptr)
                    {
                        continue;;
                    }

                    g_bytes_transfered = &(bg_download_list[i].bytes_transfered);
                    if (bg_download_list[i].host_info.type == CLIENT_TYPE_FTP)
                    {

                        FtpClient *ftpclient = (FtpClient*)tmp_client;
                        g_dl_offset = 0;
                        ftpclient->SetCallbackBytes(1);
                        ftpclient->SetCallbackXferFunction(DownloadFtpCallback);
                    }

                    bg_download_list[i].state = STATE_DOWNLOADING;
                    CONFIG::SaveBgDownloadData();

                    snprintf(temp_file, sizeof(temp_file), "%s.tmp", bg_download_list[i].dest_path.c_str());
                    Util::RichNotify(bg_download_list[i].id, "Started download %s", bg_download_list[i].dest_path.c_str());

                    ret = tmp_client->Get(temp_file, bg_download_list[i].src_path);

                    if (ret == 0)
                    {
                        if (!in_rest_mode)
                        {
                            bg_download_list[i].state = STATE_FAILED;
                            Util::RichNotify(bg_download_list[i].id, "Failed to download %s", bg_download_list[i].dest_path.c_str());
                        }
                        ReleaseRemoteClient(tmp_client, false);
                        sleep(5);
                        break;
                    }
                    else
                    {
                        FS::Rename(temp_file, bg_download_list[i].dest_path);
                        Util::RichNotify(bg_download_list[i].id, "Completed download %s", bg_download_list[i].dest_path.c_str());
                        bg_download_list[i].state = STATE_SUCCESS;
                    }
                    CONFIG::SaveBgDownloadData();

                    ReleaseRemoteClient(tmp_client, true);
                }
                else if (bg_download_list[i].state == STATE_DOWNLOADING || (bg_download_list[i].state == STATE_FAILED && bg_download_list[i].failed_attempts < 5))
                {
                    // Resume interrupted download
                    // The pool hands a client out exclusively, so the download
                    // owns its connection for the whole file and returns it to
                    // the pool when it is done.
                    RemoteClient *tmp_client = AcquireRemoteClient(bg_download_list[i].host_info, true);
                    if (tmp_client == nullptr)
                    {
                        continue;
                    }

                    g_bytes_transfered = &(bg_download_list[i].bytes_transfered);
                    if (bg_download_list[i].host_info.type == CLIENT_TYPE_FTP)
                    {

                        FtpClient *ftpclient = (FtpClient*)tmp_client;
                        ftpclient->SetCallbackBytes(1);
                        ftpclient->SetCallbackXferFunction(DownloadFtpCallback);
                    }

                    bg_download_list[i].state = STATE_RESUMED;

                    snprintf(temp_file, sizeof(temp_file), "%s.tmp", bg_download_list[i].dest_path.c_str());
                    // Check if temp file still exists, if exists then resume download
                    Util::RichNotify(bg_download_list[i].id, "Resuming download %s", bg_download_list[i].dest_path.c_str());
                    if (FS::FileExists(temp_file))
                    {
                        tmp_file_size = FS::GetSize(temp_file);
                        g_dl_offset = tmp_file_size;
                        ret = tmp_client->Get(temp_file, bg_download_list[i].src_path, tmp_file_size);
                    }
                    else
                    {
                        g_dl_offset = 0;
                        ret = tmp_client->Get(temp_file, bg_download_list[i].src_path);
                    }

                    if (ret == 0)
                    {
                        if (!in_rest_mode)
                        {
                            bg_download_list[i].state = STATE_FAILED;
                            bg_download_list[i].failed_attempts++;
                            Util::RichNotify(bg_download_list[i].id, "Failed to download %s. Attempts: %d", bg_download_list[i].dest_path.c_str(), bg_download_list[i].failed_attempts);
                            if (bg_download_list[i].failed_attempts >= 5)
                            {
                                CONFIG::SaveBgDownloadData();
                            }
                        }
                        ReleaseRemoteClient(tmp_client, false);
                        sleep(bg_download_list[i].failed_attempts * 5);
                        break;
                    }
                    else
                    {
                        FS::Rename(temp_file, bg_download_list[i].dest_path);
                        Util::RichNotify(bg_download_list[i].id, "Completed download %s", bg_download_list[i].dest_path.c_str());
                        bg_download_list[i].state = STATE_SUCCESS;
                    }
                    CONFIG::SaveBgDownloadData();

                    ReleaseRemoteClient(tmp_client, true);
                }
            }

            sleep(1);
        }

        return nullptr;
    }

    void *ServerThread(void *argp)
    {
        svr->Get("/", [&](const Request &req, Response &res)
        {
            res.set_redirect("/index.html");
        });

        svr->Post("/store_bg_install_data", [&](const Request &req, Response &res)
        {
            const char *hash_param;

            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            if (jobj != nullptr)
            {

                PackageInstallData pkg_data;
                std::string hash = JsonGetString(jobj, "hash");
                pkg_data.host_info.url = JsonGetString(jobj, "url");
                pkg_data.host_info.username = JsonGetString(jobj, "username");
                pkg_data.host_info.password = JsonGetString(jobj, "password");
                pkg_data.path = JsonGetString(jobj, "path");
                pkg_data.host_info.http_server_type = JsonGetString(jobj, "http_server_type");
                pkg_data.timestamp = Util::GetTick();
                pkg_data.host_info.type = json_object_get_int(json_object_object_get(jobj, "type"));
                pkg_data.size = json_object_get_uint64(json_object_object_get(jobj, "file_size"));

                if (pkg_data.host_info.url.empty() || hash.empty())
                {
                    bad_request(res, "Required url_param or hash parameter missing");
                    return;
                }

                CONFIG::AddPackageInstallHostData(hash, pkg_data);
                CONFIG::SavePackageInstallHostData();
            }
        });

        svr->Get("/bg_install/(.*)", [&](const Request &req, Response &res)
        {
            std::string hash = req.matches[1];
            // Holds a reference to the entry for the whole request, so a
            // concurrent /store_bg_install_data cannot free it under us.
            std::shared_ptr<PackageInstallData> pkg_host_data = CONFIG::GetPackageInstallHostData(hash);
            RemoteClient *tmp_client;

            if (pkg_host_data == nullptr)
            {
                failed(res, 500, "Cannot resume background install of " + hash + ". Host data not found.");
                return;
            }

            tmp_client = AcquireRemoteClient(pkg_host_data->host_info, true);
            if (tmp_client == nullptr)
            {
                res.status = 500;
                return;
            }

            std::string path = pkg_host_data->path;

            res.status = 206;
                
            res.set_content_provider(
                pkg_host_data->size, "application/octet-stream",
                [tmp_client, path](size_t offset, size_t length, DataSink &sink) {
                    int ret;
                    ret = tmp_client->GetRange(path, sink, length, offset);
                    return (ret==1);
                },
                [tmp_client](bool success) {
                    ReleaseRemoteClient(tmp_client, success);
                });

        });

        svr->Post("/download_url", [&](const Request &req, Response &res)
        {

            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            if (jobj != nullptr)
            {
                BgDownloadData download_data;
                download_data.host_info.type = json_object_get_int(json_object_object_get(jobj, "type"));
                download_data.host_info.url = JsonGetString(jobj, "url");
                download_data.host_info.username = JsonGetString(jobj, "username");
                download_data.host_info.password = JsonGetString(jobj, "password");
                download_data.host_info.http_server_type = JsonGetString(jobj, "http_server_type");
                download_data.src_path = JsonGetString(jobj, "src_path");
                download_data.dest_path = JsonGetString(jobj, "dest_path");
                download_data.file_size = json_object_get_uint64(json_object_object_get(jobj, "size"));
                download_data.state = STATE_PENDING;
                download_data.id = json_object_get_uint64(json_object_object_get(jobj, "id"));
                download_data.bytes_transfered = 0;
                download_data.timestamp = Util::GetTick();
                download_data.failed_attempts = 0;

                if (download_data.host_info.url.empty() || download_data.src_path.empty() || download_data.dest_path.empty())
                {
                    bad_request(res, "Required parameters are missing");
                    return;
                }

                CONFIG::AddBgDownloadData(download_data);
                CONFIG::SaveBgDownloadData();
            }
        });

        svr->Get("/get_download_state", [&](const Request &req, Response &res)
        {
            json_object *download_list = json_object_new_array();
            JsonObjectRef jobj_ref(download_list);

            for (int i=0; i < bg_download_list.size(); i++)
            {
                json_object *download_item_obj = json_object_new_object();
                json_object_object_add(download_item_obj, "path", json_object_new_string(bg_download_list[i].dest_path.c_str()));
                json_object_object_add(download_item_obj, "bytes_transfered", json_object_new_uint64(bg_download_list[i].bytes_transfered));
                json_object_object_add(download_item_obj, "file_size", json_object_new_uint64(bg_download_list[i].file_size));
                json_object_object_add(download_item_obj, "state", json_object_new_int(bg_download_list[i].state));
                json_object_object_add(download_item_obj, "timestamp", json_object_new_uint64(bg_download_list[i].timestamp/1000000));
                json_object_array_add(download_list, download_item_obj);
            }
            
            const char *payload_str = json_object_to_json_string(download_list);

            res.status = 200;
            res.set_content(payload_str, "application/json");
        });

        svr->Get("/stop", [&](const Request & /*req*/, Response & /*res*/)
        {
            stop_server = true;
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
        svr->set_mount_point("/", "/data/homebrew/ezremote-client/assets/");

        svr->listen("0.0.0.0", http_server_port);

        return NULL;
    }

    void PauseDownloadThread()
    {
        pthread_mutex_lock(&download_pause_mutex);
        in_rest_mode = true;
        pthread_mutex_unlock(&download_pause_mutex);
    }

    void ResumeDownloadThread()
    {
        pthread_mutex_lock(&download_pause_mutex);
        in_rest_mode = false;
        pthread_cond_signal(&download_pause_cond);
        pthread_mutex_unlock(&download_pause_mutex);
    }

    void Start()
    {
        if (svr == nullptr)
            svr = new Server();
        if (!svr->is_valid())
        {
            return;
        }

        while (!stop_server)
        {
            if (!in_rest_mode)
            {
                Util::Notify("Starting ezRemote Server %.2f on port %d", EZREMOTE_VERSION, http_server_port);
                ServerThread(nullptr);
            }

            if (!stop_server)
            {
                if (!in_rest_mode)
                {
                    delete svr;
                    svr = new Server();
                }
                else
                {
                    sleep(2);
                }
            }
        }
    }

    void Stop()
    {
        stop_server = true;
        svr->stop();
    }

    void StartDownloadThread()
    {
        pthread_create(&bg_download_thread, NULL, DownloadFilesThread, NULL);
    }

    void StopDownloadThread()
    {
        stop_download = true;
        pthread_cancel(bg_download_thread);
    }

    bool IsStarted()
    {
        httplib::Client client = httplib::Client("http://127.0.0.1:6701");
        if (auto res = client.Get("/version"))
        {
            return true;
        }
        return false;
    }
}
