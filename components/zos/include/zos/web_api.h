#ifndef ZOS_WEB_API_H_
#define ZOS_WEB_API_H_

#include <string>

namespace zos {

class Shell;

/**
 * The JSON behind the device web page, free of any HTTP stack so the PC
 * simulator serves exactly the same data.
 */
namespace web_api {

/** GET /api/status body. */
std::string StatusJson(Shell& shell);
/** GET /api/apps body: apps with their settings and current values. */
std::string AppsJson(Shell& shell);
/**
 * POST /api/apps/<id>/config. Validates every value before saving any, then
 * notifies the app. Returns an HTTP status (200, 400, 404); on failure
 * `*error` says why.
 */
int ApplyConfig(Shell& shell, const std::string& id, const char* body,
                size_t length, std::string* error);

}  // namespace web_api
}  // namespace zos

#endif  // ZOS_WEB_API_H_
