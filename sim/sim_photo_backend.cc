// Simulator storage of the photo album: one 64 KB file per slot in /tmp/zectrix_sim_photos (survives sim restarts).
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apps/photo/photo_backend.h"

namespace {

constexpr char kDir[] = "/tmp/zectrix_sim_photos";
constexpr size_t kSlots = 160;  // a 10 MB partition

class FileBackend : public photos::Backend {
public:
    FileBackend() { mkdir(kDir, 0755); }
    size_t SlotCount() override { return kSlots; }
    bool Read(size_t slot, size_t offset, void* data, size_t size) override {
        FILE* f = fopen(Path(slot).c_str(), "rb");
        if (!f) {  // a slot that was never written reads as erased flash
            memset(data, 0xFF, size);
            return true;
        }
        bool ok = fseek(f, static_cast<long>(offset), SEEK_SET) == 0;
        const size_t got = ok ? fread(data, 1, size, f) : 0;
        fclose(f);
        if (got < size) memset(static_cast<char*>(data) + got, 0xFF, size - got);
        return ok;
    }
    bool Erase(size_t slot, size_t offset, size_t size) override {
        std::vector<char> ff(size, '\xFF');
        return Put(slot, offset, ff.data(), size);
    }
    bool Write(size_t slot, size_t offset, const void* data, size_t size) override {
        return Put(slot, offset, data, size);
    }

private:
    static std::string Path(size_t slot) {
        char name[64];
        snprintf(name, sizeof(name), "%s/slot_%03zu.bin", kDir, slot);
        return name;
    }
    bool Put(size_t slot, size_t offset, const void* data, size_t size) {
        FILE* f = fopen(Path(slot).c_str(), "r+b");
        if (!f) f = fopen(Path(slot).c_str(), "w+b");
        if (!f) return false;
        const bool ok = fseek(f, static_cast<long>(offset), SEEK_SET) == 0 && fwrite(data, 1, size, f) == size;
        fclose(f);
        return ok;
    }
};

}  // namespace

std::unique_ptr<photos::Backend> NewPhotoBackend() { return std::make_unique<FileBackend>(); }
