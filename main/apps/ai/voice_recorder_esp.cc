// Firmware microphone: the ES8311 through ZectrixBoard::PrepareAudio(), read on its own task into PSRAM.
#include <atomic>
#include <vector>

#include "apps/ai/voice_recorder.h"
#include "audio_codec.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zectrix_board.h"

namespace {

constexpr char kTag[] = "voice";
constexpr size_t kChunkSamples = 320;  // 20 ms
constexpr size_t kMaxSamples = static_cast<size_t>(ai::kRecordSampleRate) * ai::kMaxRecordSeconds;
constexpr size_t kMinSamples = ai::kRecordSampleRate / 2;  // shorter than 0.5 s is a slip of the finger

class EspRecorder : public ai::VoiceRecorder {
public:
    explicit EspRecorder(ZectrixBoard& board) : board_(board) {}

    bool Start() override {
        if (running_ || !finished_) return false;
        audio_ = board_.PrepareAudio();
        if (audio_ == nullptr || !audio_->valid()) {
            ESP_LOGE(kTag, "microphone not available");
            return false;
        }
        audio_->EnableInput(true);
        pcm_.clear();
        pcm_.reserve(kMaxSamples);
        started_us_ = esp_timer_get_time();
        running_ = true;
        finished_ = false;
        if (xTaskCreate(Task, "voice_rec", 6144, this, 4, nullptr) != pdPASS) {
            running_ = false;
            finished_ = true;
            return false;
        }
        return true;
    }

    bool recording() const override { return running_; }

    int elapsed_ms() const override {
        return running_ ? static_cast<int>((esp_timer_get_time() - started_us_) / 1000) : 0;
    }

    std::string Stop() override {
        running_ = false;
        for (int i = 0; i < 100 && !finished_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
        if (!finished_ || pcm_.size() < kMinSamples) {
            ESP_LOGW(kTag, "nothing recorded (%u samples)", static_cast<unsigned>(pcm_.size()));
            return "";
        }
        ESP_LOGI(kTag, "recorded %u samples", static_cast<unsigned>(pcm_.size()));
        std::string wav = ai::BuildWav(pcm_.data(), pcm_.size(), ai::kRecordSampleRate);
        std::vector<int16_t>().swap(pcm_);
        return wav;
    }

private:
    static void Task(void* arg) {
        auto* self = static_cast<EspRecorder*>(arg);
        std::vector<int16_t> chunk(kChunkSamples);
        while (self->running_ && self->pcm_.size() + kChunkSamples <= kMaxSamples) {
            if (self->audio_->InputData(chunk)) {
                self->pcm_.insert(self->pcm_.end(), chunk.begin(), chunk.end());
            }
        }
        self->running_ = false;  // also the limit case: the app sees recording() turn false
        self->finished_ = true;
        vTaskDelete(nullptr);
    }

    ZectrixBoard& board_;
    AudioCodec* audio_ = nullptr;
    std::vector<int16_t> pcm_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{true};
    int64_t started_us_ = 0;
};

}  // namespace

namespace ai {
std::unique_ptr<VoiceRecorder> NewVoiceRecorder(ZectrixBoard& board) { return std::make_unique<EspRecorder>(board); }
}  // namespace ai
