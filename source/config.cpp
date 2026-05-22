// #include <orbis/UserService.h>
// #include <orbis/Net.h>
#include <string>
#include <cstring>
#include <map>
#include <vector>
#include <regex>
#include <shared_mutex>
#include <stdlib.h>
#include <json-c/json.h>
#include "server/http_server.h"
#include "config.h"
#include "fs.h"
#include "crypt.h"
#include "base64.h"
#include "util.h"

static std::map<std::string, PackageInstallData> pkg_download_history;
std::vector<BgDownloadData> bg_download_list;

unsigned char cipher_key[32] = {'s', '5', 'v', '8', 'y', '/', 'B', '?', 'E', '(', 'H', '+', 'M', 'b', 'Q', 'e', 'T', 'h', 'W', 'm', 'Z', 'q', '4', 't', '7', 'w', '9', 'z', '$', 'C', '&', 'F'};
unsigned char cipher_iv[16] = {'Y', 'p', '3', 's', '6', 'v', '9', 'y', '$', 'B', '&', 'E', ')', 'H', '@', 'M'};

std::shared_mutex pkg_mutex_;
std::shared_mutex download_mutex_;
uint64_t *g_bytes_transfered;

namespace CONFIG
{
    int Encrypt(const std::string &text, std::string &encrypt_text)
    {
        unsigned char tmp_encrypt_text[text.length() * 2];
        int encrypt_text_len;
        memset(tmp_encrypt_text, 0, sizeof(tmp_encrypt_text));
        int ret = openssl_encrypt((unsigned char *)text.c_str(), text.length(), cipher_key, cipher_iv, tmp_encrypt_text, &encrypt_text_len);
        if (ret == 0)
            return 0;
        return Base64::Encode(std::string((const char *)tmp_encrypt_text, encrypt_text_len), encrypt_text);
    }

    int Decrypt(const std::string &text, std::string &decrypt_text)
    {
        std::string tmp_decode_text;
        int ret = Base64::Decode(text, tmp_decode_text);
        if (ret == 0)
            return 0;

        unsigned char tmp_decrypt_text[tmp_decode_text.length() * 2];
        int decrypt_text_len;
        memset(tmp_decrypt_text, 0, sizeof(tmp_decrypt_text));
        ret = openssl_decrypt((unsigned char *)tmp_decode_text.c_str(), tmp_decode_text.length(), cipher_key, cipher_iv, tmp_decrypt_text, &decrypt_text_len);
        if (ret == 0)
            return 0;

        decrypt_text.clear();
        decrypt_text.append(std::string((const char *)tmp_decrypt_text, decrypt_text_len));

        return 1;
    }

	PackageInstallData* GetPackageInstallHostData(const std::string &hash)
	{
        if (pkg_download_history.find(hash) != pkg_download_history.end())
		    return &pkg_download_history[hash];
        return nullptr;
	}

	void AddPackageInstallHostData(const std::string &hash, PackageInstallData pkg_data)
	{
		std::unique_lock<std::shared_mutex> lock(pkg_mutex_);
		std::pair<std::string, PackageInstallData> pair = std::make_pair(hash, pkg_data);
		pkg_download_history.erase(hash);
		pkg_download_history.insert(pair);
	}

	void RemovePackageInstallHostData(const std::string &hash)
	{
		std::unique_lock<std::shared_mutex> lock(pkg_mutex_);
		pkg_download_history.erase(hash);
	}

    void LoadPackageInstallHostData()
    {
        if (FS::FileExists(PKG_INSTALL_HISTORY_PATH))
        {
            json_object *jobj = json_object_from_file(PKG_INSTALL_HISTORY_PATH);
            struct array_list *history_list = json_object_get_array(jobj);

            for (size_t history_idx = 0; history_idx < history_list->length; ++history_idx)
            {
                PackageInstallData history_item;

                json_object *history_item_obj = (json_object *)array_list_get_idx(history_list, history_idx);
                std::string hash = std::string(json_object_get_string(json_object_object_get(history_item_obj, "hash")));
                history_item.host_info.url = std::string(json_object_get_string(json_object_object_get(history_item_obj, "url")));
                history_item.path = std::string(json_object_get_string(json_object_object_get(history_item_obj, "path")));
                history_item.host_info.username = std::string(json_object_get_string(json_object_object_get(history_item_obj, "username")));
                std::string encrypted_password = std::string(json_object_get_string(json_object_object_get(history_item_obj, "password")));
                history_item.host_info.type = json_object_get_int(json_object_object_get(history_item_obj, "type"));
                history_item.timestamp = json_object_get_uint64(json_object_object_get(history_item_obj, "timestamp"));
                history_item.host_info.client = nullptr;

                if (history_item.host_info.type == CLIENT_TYPE_HTTP_SERVER)
		        {
                    history_item.host_info.http_server_type = std::string(json_object_get_string(json_object_object_get(history_item_obj, "http_server_type")));
                }

                int ret = Decrypt(encrypted_password, history_item.host_info.password);
                if (ret == 0)
                {
                    history_item.host_info.password = encrypted_password;
                }
                AddPackageInstallHostData(hash, history_item);
            }
            json_object_put(jobj);
        }
    }

    void SavePackageInstallHostData()
    {
        std::unique_lock<std::shared_mutex> lock(pkg_mutex_);

        if (!FS::FolderExists(DATA_PATH))
        {
            FS::MkDirs(DATA_PATH);
        }

        json_object *history_list = json_object_new_array();
        uint64_t current_time = Util::GetTick();

        for (auto it = pkg_download_history.begin(); it != pkg_download_history.end(); ++it)
        {
            if (current_time - it->second.timestamp < MAX_PKG_HISTORY_RETENTION)
            {
                json_object *history_item_obj = json_object_new_object();
                json_object_object_add(history_item_obj, "hash", json_object_new_string(it->first.c_str()));
                json_object_object_add(history_item_obj, "url", json_object_new_string(it->second.host_info.url.c_str()));
                json_object_object_add(history_item_obj, "path", json_object_new_string(it->second.path.c_str()));
                json_object_object_add(history_item_obj, "username", json_object_new_string(it->second.host_info.username.c_str()));
                json_object_object_add(history_item_obj, "type", json_object_new_int(it->second.host_info.type));
                json_object_object_add(history_item_obj, "timestamp", json_object_new_uint64(it->second.timestamp));
                if (it->second.host_info.type == CLIENT_TYPE_HTTP_SERVER)
                {
                    json_object_object_add(history_item_obj, "http_server_type", json_object_new_string(it->second.host_info.http_server_type.c_str()));
                }

                std::string encrypted_password;
                if (!it->second.host_info.password.empty())
                {
                    Encrypt(it->second.host_info.password, encrypted_password);
                }
                json_object_object_add(history_item_obj, "password", json_object_new_string(encrypted_password.c_str()));

                json_object_array_add(history_list, history_item_obj);
            }
        }
        
        json_object_to_file(PKG_INSTALL_HISTORY_PATH, history_list);
        json_object_put(history_list);
    }

	void AddBgDownloadData(BgDownloadData pkg_data)
	{
		std::unique_lock<std::shared_mutex> lock(download_mutex_);
		bg_download_list.push_back(pkg_data);
	}

    void LoadBgDownloadData()
    {
        if (FS::FileExists(BG_DOWNLOAD_HISTORY_PATH))
        {
            json_object *jobj = json_object_from_file(BG_DOWNLOAD_HISTORY_PATH);
            struct array_list *history_list = json_object_get_array(jobj);

            for (size_t history_idx = 0; history_idx < history_list->length; ++history_idx)
            {
                BgDownloadData history_item;

                json_object *history_item_obj = (json_object *)array_list_get_idx(history_list, history_idx);
                history_item.host_info.type = json_object_get_int(json_object_object_get(history_item_obj, "type"));
                history_item.host_info.url = std::string(json_object_get_string(json_object_object_get(history_item_obj, "url")));
                history_item.host_info.username = std::string(json_object_get_string(json_object_object_get(history_item_obj, "username")));
                std::string encrypted_password = std::string(json_object_get_string(json_object_object_get(history_item_obj, "password")));

                if (history_item.host_info.type == CLIENT_TYPE_HTTP_SERVER)
		        {
                    history_item.host_info.http_server_type = std::string(json_object_get_string(json_object_object_get(history_item_obj, "http_server_type")));
                }

                int ret = Decrypt(encrypted_password, history_item.host_info.password);
                if (ret == 0)
                {
                    history_item.host_info.password = encrypted_password;
                }

                history_item.src_path = std::string(json_object_get_string(json_object_object_get(history_item_obj, "src_path")));
                history_item.dest_path = std::string(json_object_get_string(json_object_object_get(history_item_obj, "dest_path")));
                history_item.file_size = json_object_get_uint64(json_object_object_get(history_item_obj, "file_size"));
                history_item.bytes_transfered = json_object_get_uint64(json_object_object_get(history_item_obj, "bytes_transfered"));
                history_item.state = static_cast<DownloadState>(json_object_get_int(json_object_object_get(history_item_obj, "state")));
                history_item.id = json_object_get_uint64(json_object_object_get(history_item_obj, "id"));
                history_item.timestamp = json_object_get_uint64(json_object_object_get(history_item_obj, "timestamp"));

                AddBgDownloadData(history_item);
            }
            json_object_put(jobj);
        }
    }

    void SaveBgDownloadData()
    {
        std::unique_lock<std::shared_mutex> lock(download_mutex_);

        if (!FS::FolderExists(DATA_PATH))
        {
            FS::MkDirs(DATA_PATH);
        }

        json_object *history_list = json_object_new_array();
        uint64_t current_time = Util::GetTick();

        for (auto it = bg_download_list.begin(); it != bg_download_list.end(); ++it)
        {
            if (current_time - it->timestamp < MAX_PKG_HISTORY_RETENTION)
            {
                json_object *history_item_obj = json_object_new_object();
                json_object_object_add(history_item_obj, "type", json_object_new_int(it->host_info.type));
                json_object_object_add(history_item_obj, "url", json_object_new_string(it->host_info.url.c_str()));
                if (it->host_info.type == CLIENT_TYPE_HTTP_SERVER)
                {
                    json_object_object_add(history_item_obj, "http_server_type", json_object_new_string(it->host_info.http_server_type.c_str()));
                }

                std::string encrypted_password;
                if (!it->host_info.password.empty())
                {
                    Encrypt(it->host_info.password, encrypted_password);
                }

                json_object_object_add(history_item_obj, "username", json_object_new_string(it->host_info.username.c_str()));
                json_object_object_add(history_item_obj, "password", json_object_new_string(encrypted_password.c_str()));
                json_object_object_add(history_item_obj, "src_path", json_object_new_string(it->src_path.c_str()));
                json_object_object_add(history_item_obj, "dest_path", json_object_new_string(it->dest_path.c_str()));
                json_object_object_add(history_item_obj, "file_size", json_object_new_uint64(it->file_size));
                json_object_object_add(history_item_obj, "bytes_transfered", json_object_new_uint64(it->bytes_transfered));
                json_object_object_add(history_item_obj, "state", json_object_new_int(it->state));
                json_object_object_add(history_item_obj, "id", json_object_new_uint64(it->id));
                json_object_object_add(history_item_obj, "timestamp", json_object_new_uint64(it->timestamp));

                json_object_array_add(history_list, history_item_obj);
            }
        }
        
        json_object_to_file(BG_DOWNLOAD_HISTORY_PATH, history_list);
        json_object_put(history_list);
    }
}
