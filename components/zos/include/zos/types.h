#ifndef ZOS_TYPES_H_
#define ZOS_TYPES_H_

#include <cstddef>
#include <cstdint>

namespace zos {

/**
 * Logical keys delivered to apps. Every app sees the same set, so the key
 * behaviour is identical everywhere:
 *   Up / Down    click: move or adjust
 *   Select       click on the OK (BOOT) key: act on the current item
 *   SelectHold   hold OK 1.5 s: handled by the shell (go Home); apps never see it
 * Hold DOWN 3 s powers the device off; the shell handles that too.
 */
enum class Key : uint8_t {
    kUp,
    kDown,
    kSelect,
    kSelectHold,
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool empty() const { return w <= 0 || h <= 0; }
};

Rect Union(const Rect& a, const Rect& b);

/**
 * Footer notes. Only keys whose meaning is not the obvious "move up/down"
 * need a note; leave a field null to omit it from the footer.
 */
struct KeyHints {
    const char* up = nullptr;
    const char* down = nullptr;
    const char* select = nullptr;
};

/** One user-editable setting, rendered automatically on the web page. */
struct ConfigField {
    enum class Type : uint8_t { kString, kInt, kBool, kChoice };

    const char* key;        ///< NVS key, <= 15 chars
    const char* label;
    Type type;
    const char* def;        ///< default value as text ("1"/"0" for kBool)
    const char* choices;    ///< kChoice: options separated by '|'
    const char* hint;       ///< optional help text, may be null
};

}  // namespace zos

#endif  // ZOS_TYPES_H_
