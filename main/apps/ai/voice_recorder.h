#ifndef APPS_AI_VOICE_RECORDER_H_
#define APPS_AI_VOICE_RECORDER_H_

#include <cstdint>
#include <memory>
#include <string>

class ZectrixBoard;

namespace ai {

constexpr int kRecordSampleRate = 16000;   // the codec runs at 16 kHz mono
constexpr int kMaxRecordSeconds = 20;      // a question is a sentence; 640 KB of PCM at most

/** Mono 16-bit little-endian WAV (44-byte header + PCM) of `samples` samples. */
std::string BuildWav(const int16_t* pcm, size_t samples, int sample_rate);

/** Microphone capture into memory. Start/Stop/recording from the shell thread. */
class VoiceRecorder {
public:
    virtual ~VoiceRecorder() = default;

    /** Begin capturing. False when the microphone could not be brought up. */
    virtual bool Start() = 0;
    /** True from Start() until Stop() or the kMaxRecordSeconds limit. */
    virtual bool recording() const = 0;
    virtual int elapsed_ms() const = 0;
    /** End the capture and return the WAV, or "" when nothing usable was recorded. */
    virtual std::string Stop() = 0;
};

/** The ES8311 microphone (firmware: voice_recorder_esp.cc; the simulator has a canned one). */
std::unique_ptr<VoiceRecorder> NewVoiceRecorder(ZectrixBoard& board);

}  // namespace ai

#endif  // APPS_AI_VOICE_RECORDER_H_
