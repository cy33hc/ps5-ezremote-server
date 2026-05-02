#include <string>
#include <json-c/json.h>
#include "http/httplib.h"
#include "server/http_server.h"
#include "clients/remote_client.h"
#include "clients/archiveorg.h"
#include "clients/baseclient.h"
#include "clients/ftpclient.h"
#include "clients/github.h"
#include "clients/nfsclient.h"
#include "clients/smbclient.h"
#include "clients/sftpclient.h"
#include "clients/webdav.h"
#include "config.h"
#include "util.h"

#define SUCCESS_MSG "{ \"result\": { \"success\": true, \"error\": null } }"
#define FAILURE_MSG "{ \"result\": { \"success\": false, \"error\": \"%s\" } }"
#define SUCCESS_MSG_LEN 48
#define PKG_INITIAL_REQUEST_SIZE 8388608ul

using namespace httplib;

Server *svr;
int http_server_port = 6701;

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

	bool IsDirectPackageInstallerEnabled()
	{
		in_addr_t in_addr;
		int sockfd;
		struct hostent *hostent;
		struct sockaddr_in sockaddr_in;
		unsigned short server_port = 9040;
	
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd == -1)
		{
			return false;
		}
	
		/* Prepare sockaddr_in. */
		hostent = gethostbyname("127.0.0.1");
		if (hostent == NULL)
		{
			printf("error: gethostbyname(\"%s\")\n", "127.0.0.1");
			return false;
		}
	
		in_addr = inet_addr(inet_ntoa(*(struct in_addr *)*(hostent->h_addr_list)));
		if (in_addr == (in_addr_t)-1)
		{
			printf("error: inet_addr(\"%s\")\n", *(hostent->h_addr_list));
			return false;
		}
	
		sockaddr_in.sin_addr.s_addr = in_addr;
		sockaddr_in.sin_family = AF_INET;
		sockaddr_in.sin_port = htons(server_port);
		/* Do the actual connection. */
		if (connect(sockfd, (struct sockaddr *)&sockaddr_in, sizeof(sockaddr_in)) == -1)
		{
			printf("Couldn't connect to ELF loader\n");
			return false;
		}

		return true;
	}

	int StartDirectPackageInstaller()
	{
		char buffer[8192];
		in_addr_t in_addr;
		int filefd;
		int sockfd;
		ssize_t read_return;
		struct hostent *hostent;
		struct sockaddr_in sockaddr_in;
		unsigned short server_port = 9021;
	
		if (IsDirectPackageInstallerEnabled())
			return 0;

		filefd = open(DPI_ELF_PATH, O_RDONLY);
		if (filefd == -1)
		{
			return -1;
		}
	
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd == -1)
		{
			return -1;
		}
	
		/* Prepare sockaddr_in. */
		hostent = gethostbyname("127.0.0.1");
		if (hostent == NULL)
		{
			return -1;
		}
	
		in_addr = inet_addr(inet_ntoa(*(struct in_addr *)*(hostent->h_addr_list)));
		if (in_addr == (in_addr_t)-1)
		{
			return -1;
		}
	
		sockaddr_in.sin_addr.s_addr = in_addr;
		sockaddr_in.sin_family = AF_INET;
		sockaddr_in.sin_port = htons(server_port);
		/* Do the actual connection. */
		if (connect(sockfd, (struct sockaddr *)&sockaddr_in, sizeof(sockaddr_in)) == -1)
		{
			return -1;
		}
	
		while (1)
		{
			read_return = read(filefd, buffer, 8192);
			if (read_return == 0)
				break;
			if (read_return == -1)
			{
				return -1;
			}
			if (write(sockfd, buffer, read_return) == -1)
			{
				return -1;
			}
		}
	
		close(filefd);
		close(sockfd);
	
		return 0;
	}

	int InstallWithDirectPackageInstaller(const std::string &url)
	{
		char buffer[256];
		in_addr_t in_addr;
		int sockfd;
		ssize_t ret;
		struct hostent *hostent;
		struct sockaddr_in sockaddr_in;
		unsigned short server_port = 9040;
	
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd == -1)
		{
			return -1;
		}
	
		/* Prepare sockaddr_in. */
		int yes = 1;
		setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &yes, sizeof(int));

		hostent = gethostbyname("127.0.0.1");
		if (hostent == NULL)
		{
			return -1;
		}
	
		in_addr = inet_addr(inet_ntoa(*(struct in_addr *)*(hostent->h_addr_list)));
		if (in_addr == (in_addr_t)-1)
		{
			return -1;
		}
	
		sockaddr_in.sin_addr.s_addr = in_addr;
		sockaddr_in.sin_family = AF_INET;
		sockaddr_in.sin_port = htons(server_port);
		/* Do the actual connection. */
		if (connect(sockfd, (struct sockaddr *)&sockaddr_in, sizeof(sockaddr_in)) == -1)
		{
			return -1;
		}
	
		ret = send(sockfd, url.c_str(), url.length(), MSG_DONTWAIT);
		if (ret < 0)
		{
			goto cleanup;
		}

		memset(buffer, 0, sizeof buffer);
		ret = read(sockfd, buffer, 256);
		if (ret <= 0)
		{
			goto cleanup;
		}

		ret = atoi(buffer);

	cleanup:
		shutdown(sockfd, SHUT_RDWR);
		close(sockfd);
	
		return ret;
	}

    static RemoteClient *GetRemoteClient(PackageInstallHostData *pkg_host_data)
    {
        RemoteClient *tmp_client = nullptr;
        if (pkg_host_data->type == CLIENT_TYPE_HTTP_SERVER)
        {
            if (pkg_host_data->http_server_type.compare(HTTP_SERVER_GITHUB))
            {
                tmp_client = new GithubClient();
            }
            else if (pkg_host_data->http_server_type.compare(HTTP_SERVER_ARCHIVEORG))
            {
                tmp_client = new ArchiveOrgClient();
            }
            else if (pkg_host_data->http_server_type.compare(HTTP_SERVER_APACHE))
            {
                tmp_client = new BaseClient();
            }
            else if (pkg_host_data->http_server_type.compare(HTTP_SERVER_MS_IIS))
            {
                tmp_client = new BaseClient();
            }
            else if (pkg_host_data->http_server_type.compare(HTTP_SERVER_NGINX))
            {
                tmp_client = new BaseClient();
            }
            else if (pkg_host_data->http_server_type.compare(HTTP_SERVER_RCLONE))
            {
                tmp_client = new BaseClient();
            }
            else if (pkg_host_data->http_server_type.compare(HTTP_SERVER_NPX_SERVE))
            {
                tmp_client = new BaseClient();
            }
        }
        else if (pkg_host_data->type == CLIENT_TYPE_SMB)
        {
            tmp_client = new SmbClient();
        }
        else if (pkg_host_data->type == CLIENT_TYPE_FILEHOST)
        {
            tmp_client = new BaseClient();
        }
        else if (pkg_host_data->type == CLIENT_TYPE_WEBDAV)
        {
            tmp_client = new WebDAVClient();
        }
        else if (pkg_host_data->type == CLIENT_TYPE_SFTP)
        {
            tmp_client = new SFTPClient();
        }
        else if (pkg_host_data->type == CLIENT_TYPE_NFS)
        {
            tmp_client = new NfsClient();
        }
        else if (pkg_host_data->type == CLIENT_TYPE_FTP)
        {
            tmp_client = new FtpClient();
            FtpClient *ftp_client = (FtpClient*) tmp_client;
            ftp_client->SetCallbackXferFunction(FtpCallback);
        }

        if (tmp_client != nullptr)
            tmp_client->Connect(pkg_host_data->url, pkg_host_data->username, pkg_host_data->password, false);

        return tmp_client;
    }

    static void DeleteRemoteClient(RemoteClient *tmp_client)
    {
        tmp_client->Quit();
        delete tmp_client;
    }
    
    void *ServerThread(void *argp)
    {
        svr->Get("/", [&](const Request &req, Response &res)
                 { res.set_redirect("/index.html"); });

        svr->Post("/install", [&](const Request &req, Response &res)
        {
            const char *path_param;
            const char *url_param;
            const char *username_param;
            const char *password_param;
            const char *http_server_type_param;

            json_object *jobj = json_tokener_parse(req.body.c_str());
            if (jobj != nullptr)
            {
                url_param = json_object_get_string(json_object_object_get(jobj, "url"));
                path_param = json_object_get_string(json_object_object_get(jobj, "path"));
                username_param  = json_object_get_string(json_object_object_get(jobj, "username"));
                password_param = json_object_get_string(json_object_object_get(jobj, "password"));
                http_server_type_param = json_object_get_string(json_object_object_get(jobj, "http_server_type"));

                if (url_param == nullptr)
                {
                    bad_request(res, "Required url_param or type parameter missing");
                    return;
                }

                PackageInstallHostData pkg_data;
                std::string unique_string = url_param;

                pkg_data.url = url_param;
                if (username_param != nullptr)
                {
                    pkg_data.username = username_param;
                    unique_string += username_param;
                }
                if (password_param != nullptr)
                {
                    pkg_data.password = password_param;
                    unique_string += password_param;
                }
                if (path_param != nullptr)
                {
                    pkg_data.path = path_param;
                    unique_string += path_param;
                }
                if (http_server_type_param != nullptr)
                {
                    pkg_data.http_server_type = http_server_type_param;
                    unique_string += http_server_type_param;
                }
                pkg_data.timestamp = Util::GetTick();

                std::string hash = Util::UrlHash(unique_string);
                CONFIG::AddPackageInstallHostData(hash, pkg_data);
                CONFIG::SavePackageInstallHostData();

                std::string url = std::string("http://localhost:") + std::to_string(http_server_port) + "/bg_install/" + hash;
                InstallWithDirectPackageInstaller(url);
            }
        });

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

                PackageInstallHostData pkg_data;
                pkg_data.url = url_param;
                if (username_param != nullptr)
                    pkg_data.username = username_param;
                if (password_param != nullptr)
                    pkg_data.password = password_param;
                if (path_param != nullptr)
                    pkg_data.path = path_param;
                if (http_server_type_param != nullptr)
                    pkg_data.http_server_type = http_server_type_param;
                pkg_data.timestamp = Util::GetTick();
                pkg_data.type = type_param;

                CONFIG::AddPackageInstallHostData(hash_param, pkg_data);
                CONFIG::SavePackageInstallHostData();
            }
        });

        svr->Get("/bg_install/(.*)", [&](const Request &req, Response &res)
        {
            std::string hash = req.matches[1];
            PackageInstallHostData* pkg_host_data = CONFIG::GetPackageInstallHostData(hash);

            if (pkg_host_data == nullptr)
            {
                failed(res, 500, "Cannot resume background install of " + hash + ". Host data not found.");
                return;
            }

            RemoteClient *tmp_client = GetRemoteClient(pkg_host_data);
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
}
