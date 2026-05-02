#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include "clients/remote_client.h"
#include "clients/sftpclient.h"
#include "fs.h"

#define FTP_CLIENT_BUFSIZ 1048576

SFTPClient::SFTPClient()
{
    session = nullptr;
    sftp_session = nullptr;
    sock = 0;
};

SFTPClient::~SFTPClient(){};

int SFTPClient::Connect(const std::string &url, const std::string &username, const std::string &password, bool send_ping)
{
    int port = 22;
    std::string host = url.substr(7);
    size_t colon_pos = host.find(":");
    if (colon_pos != std::string::npos)
    {
        port = std::atoi(host.substr(colon_pos + 1).c_str());
        host = host.substr(0, colon_pos);
    }

    struct hostent *he;
    struct in_addr **addr_list;
    char ip[20];
    int i;

    if (strcmp(host.c_str(), "localhost") == 0)
    {
        sprintf(ip, "%s", "127.0.0.1");
    }
    else
    {
        if ((he = gethostbyname(host.c_str())) == NULL)
        {
            return 0;
        }

        addr_list = (struct in_addr **)he->h_addr_list;
        for (i = 0; addr_list[i] != NULL; i++)
        {
            strcpy(ip, inet_ntoa(*addr_list[i]));
            break;
        }
    }

    in_addr dst_addr;
    sockaddr_in server_addr;
    int on = 1;
    int32_t retval;

    memset(&server_addr, 0, sizeof(server_addr));
    inet_pton(AF_INET, ip, (void *)&dst_addr);
    server_addr.sin_addr = dst_addr;
    server_addr.sin_port = htons(port);
    server_addr.sin_family = AF_INET;

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    retval = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&on, sizeof(on));
    int const size = FTP_CLIENT_BUFSIZ;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) == -1)
    {
        close(sock);
        return 0;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == -1)
    {
        close(sock);
        return 0;
    }

    if (connect(sock, (struct sockaddr *)(&server_addr), sizeof(struct sockaddr_in)) != 0)
    {
        sprintf(this->response, "%s", "Failed to connect!");
        return 0;
    }
    /* Create a session instance
     */
    session = libssh2_session_init();
    libssh2_session_set_blocking(session, 1);
    libssh2_keepalive_config(session, 1, 5);

    if (!session)
    {
        sprintf(this->response, "Failed to connect");
        return 0;
    }

    /* ... start it up. This will trade welcome banners, exchange keys,
     * and setup crypto, compression, and MAC layers
     */
    usleep(100000);
    int rc = libssh2_session_handshake(session, sock);
    if (rc)
    {
        sprintf(this->response, "Failed SSL handshake %d", rc);
        return 0;
    }

    /* At this point we havn't yet authenticated.  The first thing to do
     * is check the hostkey's fingerprint against our known hosts Your app
     * may have it hard coded, may go to a file, may present it to the
     * user, that's your call
     */
    const char *fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);

    /* check what authentication methods are available */
    char *userauthlist = libssh2_userauth_list(session, username.c_str(), username.length());

    int auth_pw = 0;
    if (strstr(userauthlist, "password") != NULL)
    {
        auth_pw |= 1;
    }
    if (strstr(userauthlist, "keyboard-interactive") != NULL)
    {
        auth_pw |= 2;
    }
    if (strstr(userauthlist, "publickey") != NULL)
    {
        auth_pw |= 4;
    }

    bool use_identity = password.find("file://") != std::string::npos;
    if (auth_pw & 1 && !use_identity)
    {
        /* We could authenticate via password */
        if (libssh2_userauth_password(session, username.c_str(), password.c_str()))
        {
            sprintf(this->response, "%s", "Authentication by password failed!");
            goto shutdown;
        }
    }
    else if (auth_pw & 4 && use_identity)
    {
        /* Or by public key */
        std::string publickey = password.substr(7) + "/id_rsa.pub";
        std::string privatekey = password.substr(7) + "/id_rsa";
        if (!FS::FileExists(publickey.c_str()))
        {
            sprintf(response, "SSH public key %s is not found", publickey.c_str());
            goto shutdown;
        }
        if (!FS::FileExists(privatekey.c_str()))
        {
            sprintf(response, "SSH private key %s is not found", privatekey.c_str());
            goto shutdown;
        }
        if (libssh2_userauth_publickey_fromfile(session, username.c_str(), publickey.c_str(), privatekey.c_str(), ""))
        {
            sprintf(this->response, "%s", "Authentication by public key failed!");
            goto shutdown;
        }
    }
    else
    {
        sprintf(this->response, "%s", "No supported authentication methods found!");
        goto shutdown;
    }

    sftp_session = libssh2_sftp_init(session);
    this->connected = true;
    return 1;

shutdown:
    libssh2_session_disconnect(session, "Normal Shutdown");
    libssh2_session_free(session);
    close(sock);
    libssh2_exit();
    session = nullptr;
    sock = 0;
    return 0;
}

int SFTPClient::GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset)
{
    LIBSSH2_SFTP_HANDLE *sftp_handle = libssh2_sftp_open(sftp_session, path.c_str(), LIBSSH2_FXF_READ, 0);
    if (!sftp_handle)
    {
        sprintf(response, "Unable to open file with SFTP: %ld", libssh2_sftp_last_error(sftp_session));
        return 0;
    }

    int ret = this->GetRange((void *)sftp_handle, sink, size, offset);
    libssh2_sftp_close(sftp_handle);

    return ret;
}

int SFTPClient::GetRange(void *fp, DataSink &sink, uint64_t size, uint64_t offset)
{
    LIBSSH2_SFTP_HANDLE *sftp_handle = (LIBSSH2_SFTP_HANDLE *)fp;

    libssh2_sftp_seek64(sftp_handle, offset);

    char *buff = (char *)malloc(FTP_CLIENT_BUFSIZ);
    int rc, count = 0;
    size_t bytes_remaining = size;
    do
    {
        size_t bytes_to_read = std::min<size_t>(FTP_CLIENT_BUFSIZ, bytes_remaining);
        rc = libssh2_sftp_read(sftp_handle, buff, bytes_to_read);
        if (rc > 0)
        {
            bytes_remaining -= rc;
            bool ok = sink.write(buff, rc);
            if (!ok)
            {
                free((char *)buff);
                return 0;
            }
        }
        else
        {
            break;
        }
    } while (1);

    free((char *)buff);

    return 1;
}

const char *SFTPClient::LastResponse()
{
    return this->response;
}

int SFTPClient::Quit()
{
    if (sftp_session != nullptr)
        libssh2_sftp_shutdown(sftp_session);
    if (session != nullptr)
    {
        libssh2_session_disconnect(session, "Normal Shutdown");
        libssh2_session_free(session);
        close(sock);
        libssh2_exit();
    }
    session = nullptr;
    sftp_session = nullptr;
    sock = 0;
    return 1;
}
