#ifndef ZOS_SETTINGS_H_
#define ZOS_SETTINGS_H_

#include <cstdint>
#include <string>

#include "zos/types.h"

namespace zos {

/** Per-app key/value store in its own NVS namespace ("zos_<app id>"). */
class Settings {
public:
    explicit Settings(const char* app_id);

    std::string GetString(const char* key, const char* def) const;
    int32_t GetInt(const char* key, int32_t def) const;
    bool GetBool(const char* key, bool def) const;

    bool SetString(const char* key, const char* value);
    bool SetInt(const char* key, int32_t value);
    bool SetBool(const char* key, bool value);

    /** Value of a declared field as text, falling back to its default. */
    std::string GetField(const ConfigField& field) const;
    /** Validate and store text for a declared field. */
    bool SetField(const ConfigField& field, const std::string& value);

private:
    char ns_[16] = {};
};

}  // namespace zos

#endif  // ZOS_SETTINGS_H_
