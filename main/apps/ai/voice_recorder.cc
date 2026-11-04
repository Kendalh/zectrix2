#include "apps/ai/voice_recorder.h"

namespace ai {

std::string BuildWav(const int16_t* pcm, size_t samples, int sample_rate) {
    const uint32_t data_bytes = static_cast<uint32_t>(samples * 2);
    std::string wav;
    wav.reserve(44 + data_bytes);
    auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) wav.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    };
    auto put16 = [&](uint16_t v) {
        wav.push_back(static_cast<char>(v & 0xFF));
        wav.push_back(static_cast<char>(v >> 8));
    };
    wav += "RIFF";
    put32(36 + data_bytes);
    wav += "WAVEfmt ";
    put32(16);
    put16(1);  // PCM
    put16(1);  // mono
    put32(static_cast<uint32_t>(sample_rate));
    put32(static_cast<uint32_t>(sample_rate) * 2);
    put16(2);
    put16(16);
    wav += "data";
    put32(data_bytes);
    for (size_t i = 0; i < samples; ++i) put16(static_cast<uint16_t>(pcm[i]));
    return wav;
}

}  // namespace ai
