// #include <orbis/UserService.h>
// #include <orbis/Net.h>
#include <string>
#include <cstring>
#include <map>
#include <vector>
#include <regex>
#include <stdlib.h>
#include <json-c/json.h>
#include "server/http_server.h"
#include "config.h"
#include "fs.h"
#include "crypt.h"
#include "base64.h"
#include "util.h"

static std::map<std::string, PackageInstallHostData> pkg_download_history;

unsigned char cipher_key[32] = {'s', '5', 'v', '8', 'y', '/', 'B', '?', 'E', '(', 'H', '+', 'M', 'b', 'Q', 'e', 'T', 'h', 'W', 'm', 'Z', 'q', '4', 't', '7', 'w', '9', 'z', '$', 'C', '&', 'F'};
unsigned char cipher_iv[16] = {'Y', 'p', '3', 's', '6', 'v', '9', 'y', '$', 'B', '&', 'E', ')', 'H', '@', 'M'};

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

	PackageInstallHostData* GetPackageInstallHostData(const std::string &hash)
	{
        if (pkg_download_history.find(hash) != pkg_download_history.end())
		    return &pkg_download_history[hash];
        return nullptr;
	}

	void AddPackageInstallHostData(const std::string &hash, PackageInstallHostData pkg_data)
	{
		std::pair<std::string, PackageInstallHostData> pair = std::make_pair(hash, pkg_data);
		if (pkg_download_history.find(hash) == pkg_download_history.end())
		    pkg_download_history.insert(pair);
	}

	void RemovePackageInstallHostData(const std::string &hash)
	{
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
                PackageInstallHostData history_item;

                json_object *history_item_obj = (json_object *)array_list_get_idx(history_list, history_idx);
                std::string hash = std::string(json_object_get_string(json_object_object_get(history_item_obj, "hash")));
                history_item.url = std::string(json_object_get_string(json_object_object_get(history_item_obj, "url")));
                history_item.path = std::string(json_object_get_string(json_object_object_get(history_item_obj, "path")));
                history_item.username = std::string(json_object_get_string(json_object_object_get(history_item_obj, "username")));
                std::string encrypted_password = std::string(json_object_get_string(json_object_object_get(history_item_obj, "password")));
                history_item.timestamp = json_object_get_uint64(json_object_object_get(history_item_obj, "timestamp"));

                size_t pos = history_item.url.find("://");
                if (pos != std::string::npos)
		        {
			        std::string type = history_item.url.substr(0, pos);
                    if (type.compare("http") == 0 || type.compare("https") ==0)
                    {
                        history_item.http_server_type = std::string(json_object_get_string(json_object_object_get(history_item_obj, "http_server_type")));
                    }
                }


                int ret = Decrypt(encrypted_password, history_item.password);
                if (ret == 0)
                {
                    history_item.password = encrypted_password;
                }
                AddPackageInstallHostData(hash, history_item);
            }
        }
    }

    void SavePackageInstallHostData()
    {
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
                json_object_object_add(history_item_obj, "url", json_object_new_string(it->second.url.c_str()));
                json_object_object_add(history_item_obj, "path", json_object_new_string(it->second.path.c_str()));
                json_object_object_add(history_item_obj, "username", json_object_new_string(it->second.username.c_str()));
                json_object_object_add(history_item_obj, "timestamp", json_object_new_uint64(it->second.timestamp));

                size_t pos = it->second.url.find("://");
                if (pos != std::string::npos)
		        {
			        std::string type = it->second.url.substr(0, pos);
                    if (type.compare("http") == 0 || type.compare("https") ==0)
                    {
                        json_object_object_add(history_item_obj, "http_server_type", json_object_new_string(it->second.http_server_type.c_str()));
                    }
                }

                std::string encrypted_password;
                Encrypt(it->second.password, encrypted_password);
                json_object_object_add(history_item_obj, "password", json_object_new_string(encrypted_password.c_str()));

                json_object_array_add(history_list, history_item_obj);
            }
        }
        
        json_object_to_file(PKG_INSTALL_HISTORY_PATH, history_list);
    }
}
