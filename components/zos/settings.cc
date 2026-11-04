#include "zos/settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "nvs.h"

namespace zos {

namespace {

class Handle {
public:
    Handle(const char* ns, nvs_open_mode_t mode) {
        ok_ = nvs_open(ns, mode, &handle_) == ESP_OK;
    }
    ~Handle() {
        if (ok_) nvs_close(handle_);
    }
    bool ok() const { return ok_; }
    nvs_handle_t get() const { return handle_; }

private:
    nvs_handle_t handle_ = 0;
    bool ok_ = false;
};

bool ChoiceAllowed(const char* choices, const std::string& value) {
    if (choices == nullptr) return false;
    const char* cursor = choices;
    while (true) {
        const char* end = std::strchr(cursor, '|');
        const size_t length = end ? static_cast<size_t>(end - cursor)
                                  : std::strlen(cursor);
        if (length == value.size() &&
            std::strncmp(cursor, value.c_str(), length) == 0) {
            return true;
        }
        if (end == nullptr) return false;
        cursor = end + 1;
    }
}

}  // namespace

Settings::Settings(const char* app_id) {
    std::snprintf(ns_, sizeof(ns_), "zos_%s", app_id);
}

std::string Settings::GetString(const char* key, const char* def) const {
    Handle handle(ns_, NVS_READONLY);
    size_t length = 0;
    if (!handle.ok() ||
        nvs_get_str(handle.get(), key, nullptr, &length) != ESP_OK ||
        length == 0) {
        return def;
    }
    std::string value(length, '\0');
    if (nvs_get_str(handle.get(), key, value.data(), &length) != ESP_OK) {
        return def;
    }
    value.resize(std::strlen(value.c_str()));
    return value;
}

int32_t Settings::GetInt(const char* key, int32_t def) const {
    Handle handle(ns_, NVS_READONLY);
    int32_t value = def;
    if (handle.ok() && nvs_get_i32(handle.get(), key, &value) != ESP_OK) {
        value = def;
    }
    return value;
}

bool Settings::GetBool(const char* key, bool def) const {
    Handle handle(ns_, NVS_READONLY);
    uint8_t value = def ? 1 : 0;
    if (handle.ok() && nvs_get_u8(handle.get(), key, &value) != ESP_OK) {
        value = def ? 1 : 0;
    }
    return value != 0;
}

bool Settings::SetString(const char* key, const char* value) {
    Handle handle(ns_, NVS_READWRITE);
    return handle.ok() && nvs_set_str(handle.get(), key, value) == ESP_OK &&
           nvs_commit(handle.get()) == ESP_OK;
}

bool Settings::SetInt(const char* key, int32_t value) {
    Handle handle(ns_, NVS_READWRITE);
    return handle.ok() && nvs_set_i32(handle.get(), key, value) == ESP_OK &&
           nvs_commit(handle.get()) == ESP_OK;
}

bool Settings::SetBool(const char* key, bool value) {
    Handle handle(ns_, NVS_READWRITE);
    return handle.ok() &&
           nvs_set_u8(handle.get(), key, value ? 1 : 0) == ESP_OK &&
           nvs_commit(handle.get()) == ESP_OK;
}

std::string Settings::GetField(const ConfigField& field) const {
    switch (field.type) {
        case ConfigField::Type::kInt:
            return std::to_string(GetInt(field.key, std::atoi(field.def)));
        case ConfigField::Type::kBool:
            return GetBool(field.key, std::strcmp(field.def, "1") == 0) ? "1"
                                                                        : "0";
        default:
            return GetString(field.key, field.def);
    }
}

bool Settings::SetField(const ConfigField& field, const std::string& value) {
    switch (field.type) {
        case ConfigField::Type::kInt: {
            char* end = nullptr;
            const long parsed = std::strtol(value.c_str(), &end, 10);
            if (value.empty() || *end != '\0') return false;
            return SetInt(field.key, static_cast<int32_t>(parsed));
        }
        case ConfigField::Type::kBool:
            if (value == "1" || value == "true") return SetBool(field.key, true);
            if (value == "0" || value == "false") return SetBool(field.key, false);
            return false;
        case ConfigField::Type::kChoice:
            return ChoiceAllowed(field.choices, value) &&
                   SetString(field.key, value.c_str());
        case ConfigField::Type::kSecret:
            return value.empty() || (value.size() <= 255 && SetString(field.key, value.c_str()));
        case ConfigField::Type::kString:
            return value.size() <= 63 && SetString(field.key, value.c_str());
    }
    return false;
}

}  // namespace zos
