#include "apps/photo/photo_web.h"

#include "apps/photo/photo_store.h"
#include "apps/photo/photo_web_ui.h"
#include <cctype>
#include <string>

#include "cJSON.h"
#include "zos/settings.h"

namespace photos {
namespace web {

std::string UrlDecode(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < text.size() + 0 && isxdigit(static_cast<unsigned char>(text[i + 1])) &&
                   isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

std::string ListJson() {
    Store& store = Store::Get();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "capacity", static_cast<double>(store.capacity()));
    cJSON* list = cJSON_AddArrayToObject(root, "photos");
    for (const Info& info : store.List()) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", info.id);
        cJSON_AddStringToObject(item, "title", info.title.c_str());
        cJSON_AddNumberToObject(item, "time", info.timestamp);
        cJSON_AddItemToArray(list, item);
    }
    char* text = cJSON_PrintUnformatted(root);
    std::string out = text ? text : "{}";
    cJSON_free(text);
    cJSON_Delete(root);
    return out;
}

int Put(const std::string& encoded_title, const uint8_t* data, size_t size, std::string* error) {
    if (size != kImageBytes) {
        *error = "图片大小不对（应为 60000 字节）";
        return 400;
    }
    std::string save_error;
    if (Store::Get().Save(UrlDecode(encoded_title), data, size, &save_error) < 0) {
        *error = save_error;
        return save_error.find("已满") != std::string::npos ? 507 : 500;
    }
    return 200;
}

int Delete(int id, std::string* error) {
    if (!Store::Get().Delete(id)) {
        *error = "没有这张照片";
        return 404;
    }
    return 200;
}

bool Get(int id, std::vector<uint8_t>* image) { return Store::Get().Load(id, image); }

std::string SettingsJson() {
    std::string interval = zos::Settings("photos").GetString("interval", "5m");
    if (interval != "off" && interval != "5m" && interval != "10m" && interval != "15m") interval = "5m";
    return "{\"interval\":\"" + interval + "\"}";
}

int SetSettings(const std::string& json, std::string* error) {
    cJSON* root = cJSON_Parse(json.c_str());
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "interval");
    int code = 200;
    if (!cJSON_IsString(value) ||
        (std::string(value->valuestring) != "off" && std::string(value->valuestring) != "5m" &&
         std::string(value->valuestring) != "10m" && std::string(value->valuestring) != "15m")) {
        *error = "播放间隔只能是 off、5m、10m 或 15m";
        code = 400;
    } else if (!zos::Settings("photos").SetString("interval", value->valuestring)) {
        *error = "保存失败";
        code = 500;
    }
    cJSON_Delete(root);
    return code;
}

}  // namespace web
}  // namespace photos
