#include "apps/photo/photo_store.h"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace photos {

namespace {

constexpr uint32_t kMagic = 0x5A504831;  // "ZPH1"

struct Header {
    uint32_t magic;
    uint32_t seq;
    uint32_t timestamp;
    char title[kTitleBytes];
};

}  // namespace

Store& Store::Get() {
    static Store instance;
    return instance;
}

bool Store::ReadHeader(size_t slot, Entry* out) {
    Header header;
    if (!backend_->Read(slot, 0, &header, sizeof(header)) || header.magic != kMagic) return false;
    header.title[kTitleBytes - 1] = '\0';
    out->info.id = static_cast<int>(slot);
    out->info.seq = header.seq;
    out->info.timestamp = header.timestamp;
    out->info.title = header.title;
    return true;
}

void Store::Init(std::unique_ptr<Backend> backend) {
    std::lock_guard<std::mutex> lock(mu_);
    if (backend_) return;
    backend_ = std::move(backend);
    const size_t slots = backend_->SlotCount();
    used_.assign(slots, false);
    for (size_t slot = 0; slot < slots; ++slot) {
        Entry entry;
        if (!ReadHeader(slot, &entry)) continue;
        used_[slot] = true;
        next_seq_ = std::max(next_seq_, entry.info.seq + 1);
        entries_.push_back(std::move(entry));
    }
}

bool Store::ready() {
    std::lock_guard<std::mutex> lock(mu_);
    return backend_ != nullptr;
}

size_t Store::capacity() {
    std::lock_guard<std::mutex> lock(mu_);
    return used_.size();
}

std::vector<Info> Store::List() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Info> out;
    for (const Entry& e : entries_) out.push_back(e.info);
    std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) { return a.seq > b.seq; });
    return out;
}

std::string Store::FitTitle(const std::string& title) {
    std::string out = title;
    // Whitespace at both ends and line breaks are not part of a title.
    for (char& c : out)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(0, 1);
    if (out.size() > kTitleBytes - 1) {
        out.resize(kTitleBytes - 1);
        // Do not leave half of a UTF-8 character behind.
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
        if (!out.empty() && static_cast<unsigned char>(out.back()) >= 0xC0) out.pop_back();
    }
    return out;
}

int Store::Save(const std::string& title, const uint8_t* image, size_t size, std::string* error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!backend_) {
        *error = "相册没有准备好";
        return -1;
    }
    if (size != kImageBytes) {
        *error = "图片大小不对";
        return -1;
    }
    size_t slot = used_.size();
    for (size_t i = 0; i < used_.size(); ++i) {
        if (!used_[i]) {
            slot = i;
            break;
        }
    }
    if (slot == used_.size()) {
        *error = "相册已满，请先删除一些照片";
        return -1;
    }
    Header header = {};
    header.magic = kMagic;
    header.seq = next_seq_;
    const time_t now = time(nullptr);
    header.timestamp = now > 1700000000 ? static_cast<uint32_t>(now) : 0;  // before that the clock was never set
    std::string fitted = FitTitle(title);
    if (fitted.empty()) fitted = "未命名";
    std::snprintf(header.title, sizeof(header.title), "%s", fitted.c_str());

    // Picture first, header last: an interrupted save leaves an invalid slot.
    if (!backend_->Erase(slot, 0, kSlotBytes) || !backend_->Write(slot, kHeaderBytes, image, kImageBytes) ||
        !backend_->Write(slot, 0, &header, sizeof(header))) {
        *error = "写入存储失败";
        return -1;
    }
    ++next_seq_;
    used_[slot] = true;
    Entry entry;
    entry.info.id = static_cast<int>(slot);
    entry.info.seq = header.seq;
    entry.info.timestamp = header.timestamp;
    entry.info.title = fitted;
    entries_.push_back(std::move(entry));
    return static_cast<int>(slot);
}

bool Store::Load(int id, std::vector<uint8_t>* image) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!backend_ || id < 0 || static_cast<size_t>(id) >= used_.size() || !used_[id]) return false;
    image->resize(kImageBytes);
    return backend_->Read(static_cast<size_t>(id), kHeaderBytes, image->data(), kImageBytes);
}

bool Store::Delete(int id) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!backend_ || id < 0 || static_cast<size_t>(id) >= used_.size() || !used_[id]) return false;
    // Erasing the header sector is enough: the slot is invalid until the next save erases the rest.
    if (!backend_->Erase(static_cast<size_t>(id), 0, kHeaderBytes)) return false;
    used_[id] = false;
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [id](const Entry& e) { return e.info.id == id; }),
                   entries_.end());
    return true;
}

}  // namespace photos
