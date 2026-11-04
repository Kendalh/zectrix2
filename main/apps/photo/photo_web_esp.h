#ifndef APPS_PHOTO_PHOTO_WEB_ESP_H_
#define APPS_PHOTO_PHOTO_WEB_ESP_H_

namespace zos {
class WebServer;
}

/** Add the album page and API to the device web server (firmware only; the simulator has its own server). */
void RegisterPhotoRoutes(zos::WebServer& web);

#endif  // APPS_PHOTO_PHOTO_WEB_ESP_H_
