#include "zos/web_api.h"

#include <cstring>

#include "cJSON.h"
#include "zos/network.h"
#include "zos/shell.h"

namespace zos {
namespace web_api {

namespace {

std::string Print(cJSON* json) {
    char* text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    std::string out = text ? text : "null";
    cJSON_free(text);
    return out;
}

const char* TypeName(ConfigField::Type type) {
    switch (type) {
        case ConfigField::Type::kInt: return "int";
        case ConfigField::Type::kBool: return "bool";
        case ConfigField::Type::kChoice: return "choice";
        case ConfigField::Type::kSecret: return "secret";
        default: return "string";
    }
}

}  // namespace

std::string StatusJson(Shell& shell) {
    const StatusInfo status = shell.status();
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "ssid", shell.net().ssid().c_str());
    cJSON_AddStringToObject(json, "ip", shell.net().ip().c_str());
    cJSON_AddNumberToObject(json, "battery",
                            status.battery_valid ? status.battery_percent : -1);
    return Print(json);
}

std::string AppsJson(Shell& shell) {
    cJSON* list = cJSON_CreateArray();
    for (size_t i = 0; i < shell.app_count(); ++i) {
        App& app = shell.app(i);
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", app.id());
        cJSON_AddStringToObject(item, "title", app.title());
        cJSON_AddStringToObject(item, "summary", app.summary());
        cJSON* fields = cJSON_AddArrayToObject(item, "fields");
        for (size_t f = 0; f < app.config_field_count(); ++f) {
            const ConfigField& field = app.config_fields()[f];
            cJSON* entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "key", field.key);
            cJSON_AddStringToObject(entry, "label", field.label);
            cJSON_AddStringToObject(entry, "type", TypeName(field.type));
            if (field.type == ConfigField::Type::kSecret) {
                // Never sent back: only whether one is stored.
                cJSON_AddStringToObject(entry, "value", "");
                cJSON_AddBoolToObject(
                    entry, "set",
                    !shell.app_settings(i).GetField(field).empty());
            } else {
                cJSON_AddStringToObject(
                    entry, "value",
                    shell.app_settings(i).GetField(field).c_str());
            }
            if (field.hint != nullptr) {
                cJSON_AddStringToObject(entry, "hint", field.hint);
            }
            if (field.type == ConfigField::Type::kChoice) {
                cJSON* choices = cJSON_AddArrayToObject(entry, "choices");
                const char* cursor = field.choices;
                while (cursor != nullptr) {
                    const char* end = std::strchr(cursor, '|');
                    const std::string option =
                        end ? std::string(cursor, end - cursor) : cursor;
                    cJSON_AddItemToArray(choices,
                                         cJSON_CreateString(option.c_str()));
                    cursor = end ? end + 1 : nullptr;
                }
            }
            cJSON_AddItemToArray(fields, entry);
        }
        cJSON_AddItemToArray(list, item);
    }
    return Print(list);
}

int ApplyConfig(Shell& shell, const std::string& id, const char* body,
                size_t length, std::string* error) {
    size_t index = shell.app_count();
    for (size_t i = 0; i < shell.app_count(); ++i) {
        if (id == shell.app(i).id()) index = i;
    }
    if (index == shell.app_count()) {
        *error = "unknown app";
        return 404;
    }
    cJSON* json = cJSON_ParseWithLength(body, length);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        *error = "bad json";
        return 400;
    }
    App& app = shell.app(index);
    // Validate every value first so a bad field changes nothing.
    for (size_t f = 0; f < app.config_field_count() && error->empty(); ++f) {
        const ConfigField& field = app.config_fields()[f];
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(json, field.key);
        if (value != nullptr && !cJSON_IsString(value)) {
            *error = std::string("not a string: ") + field.key;
        }
    }
    if (error->empty()) {
        for (size_t f = 0; f < app.config_field_count(); ++f) {
            const ConfigField& field = app.config_fields()[f];
            const cJSON* value =
                cJSON_GetObjectItemCaseSensitive(json, field.key);
            if (value == nullptr) continue;
            if (!shell.app_settings(index).SetField(field, value->valuestring)) {
                *error = std::string("invalid value for ") + field.label;
                break;
            }
        }
    }
    cJSON_Delete(json);
    if (!error->empty()) return 400;
    shell.PostConfigChanged(index);
    return 200;
}

}  // namespace web_api
}  // namespace zos
