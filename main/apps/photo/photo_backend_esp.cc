// Firmware flash backend of the photo album: the raw `photos` data partition.
#include "esp_log.h"
#include "esp_partition.h"
#include "apps/photo/photo_backend.h"

namespace {

constexpr char kTag[] = "photos";

class EspBackend : public photos::Backend {
public:
    EspBackend()
        : part_(esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "photos")) {
        if (part_ == nullptr) ESP_LOGE(kTag, "partition \"photos\" not found (flash the merged image)");
    }
    size_t SlotCount() override { return part_ ? part_->size / photos::kSlotBytes : 0; }
    bool Read(size_t slot, size_t offset, void* data, size_t size) override {
        return part_ && esp_partition_read(part_, slot * photos::kSlotBytes + offset, data, size) == ESP_OK;
    }
    bool Erase(size_t slot, size_t offset, size_t size) override {
        return part_ && esp_partition_erase_range(part_, slot * photos::kSlotBytes + offset, size) == ESP_OK;
    }
    bool Write(size_t slot, size_t offset, const void* data, size_t size) override {
        return part_ && esp_partition_write(part_, slot * photos::kSlotBytes + offset, data, size) == ESP_OK;
    }

private:
    const esp_partition_t* part_;
};

}  // namespace

std::unique_ptr<photos::Backend> NewPhotoBackend() { return std::make_unique<EspBackend>(); }
