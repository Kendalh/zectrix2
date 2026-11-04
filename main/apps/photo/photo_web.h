#ifndef APPS_PHOTO_PHOTO_WEB_H_
#define APPS_PHOTO_PHOTO_WEB_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * The album behind the device web page, free of any HTTP stack (the firmware's esp_http_server routes and the
 * simulator's Python server both call it). The page itself (photo_web_ui.h) turns a phone photo into the 16-gray
 * 400x300 picture in the browser, so the device only receives and stores the packed 4 bpp bytes.
 *   GET    /photos            the page
 *   GET    /api/photos        ListJson()
 *   GET    /api/photos/<id>   the 60000 picture bytes
 *   POST   /api/photos?title= body = the 60000 bytes (Put)
 *   DELETE /api/photos/<id>
 *   GET/POST /api/photo-settings   the slideshow interval
 */
namespace photos {
namespace web {

std::string ListJson();
/** @return an HTTP status (200, 400, 507); *error says why otherwise. `title` is URL-encoded UTF-8. */
int Put(const std::string& encoded_title, const uint8_t* data, size_t size, std::string* error);
int Delete(int id, std::string* error);
bool Get(int id, std::vector<uint8_t>* image);
std::string UrlDecode(const std::string& text);
/** The slideshow interval ("1m", "5m" or "10m"), kept in the album app's settings; JSON {"interval":"5m"}. */
std::string SettingsJson();
/** @return an HTTP status; body is the JSON {"interval":"1m"}. */
int SetSettings(const std::string& json, std::string* error);

extern const char kPageHtml[];

}  // namespace web
}  // namespace photos

#endif  // APPS_PHOTO_PHOTO_WEB_H_
