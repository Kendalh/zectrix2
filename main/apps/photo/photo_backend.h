#ifndef APPS_PHOTO_PHOTO_BACKEND_H_
#define APPS_PHOTO_PHOTO_BACKEND_H_

#include <memory>

#include "apps/photo/photo_store.h"

/** The storage under the album: firmware = the `photos` flash partition, simulator = files in /tmp/zectrix_sim_photos. */
std::unique_ptr<photos::Backend> NewPhotoBackend();

#endif  // APPS_PHOTO_PHOTO_BACKEND_H_
