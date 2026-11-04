#ifndef APPS_PHOTO_PHOTO_STORE_H_
#define APPS_PHOTO_PHOTO_STORE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/**
 * Photo album: full-panel 400x300 pictures in the panel's 16 grays (packed 4 bpp,
 * high nibble = left pixel, 0 black .. 15 white, 60000 bytes), stored in fixed 64 KB slots of the `photos` flash partition (no file system). A slot is one 4 KB header
 * sector (magic, sequence number, time, title) followed by the picture; the header is written last, so a slot that was
 * interrupted by a power loss is simply invalid. Hardware-free: the flash (firmware) or a directory of files (simulator)
 * sits behind Backend.
 */
namespace photos {

constexpr size_t kImageBytes = 400 * 300 / 2;  // the whole panel
constexpr size_t kSlotBytes = 64 * 1024;
constexpr size_t kHeaderBytes = 4096;  // one flash sector
constexpr size_t kTitleBytes = 48;     // including the terminating NUL

struct Info {
    int id = 0;            // slot number
    uint32_t seq = 0;      // grows with every save
    uint32_t timestamp = 0;  // Unix time of the save, 0 when the clock was not set
    std::string title;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual size_t SlotCount() = 0;
    virtual bool Read(size_t slot, size_t offset, void* data, size_t size) = 0;
    /** Erase [offset, offset+size) of the slot; both multiples of 4096. */
    virtual bool Erase(size_t slot, size_t offset, size_t size) = 0;
    virtual bool Write(size_t slot, size_t offset, const void* data, size_t size) = 0;
};

class Store {
public:
    static Store& Get();

    /** Scan the slots. Idempotent: the first backend wins. */
    void Init(std::unique_ptr<Backend> backend);
    bool ready();

    size_t capacity();
    /** Newest first. */
    std::vector<Info> List();
    /** Cut `title` to a valid UTF-8 prefix of at most kTitleBytes-1 bytes. */
    static std::string FitTitle(const std::string& title);
    /** @return the new id, or -1 with *error (Chinese, user-facing). */
    int Save(const std::string& title, const uint8_t* image, size_t size, std::string* error);
    bool Load(int id, std::vector<uint8_t>* image);
    bool Delete(int id);

private:
    struct Entry {
        Info info;
    };
    bool ReadHeader(size_t slot, Entry* out);

    std::mutex mu_;
    std::unique_ptr<Backend> backend_;
    std::vector<Entry> entries_;     // valid slots, unsorted
    std::vector<bool> used_;         // per slot
    uint32_t next_seq_ = 1;

public:
    Store() = default;  // public for tests; the app uses Get()
};

}  // namespace photos

#endif  // APPS_PHOTO_PHOTO_STORE_H_
