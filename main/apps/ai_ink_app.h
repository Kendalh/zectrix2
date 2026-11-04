#ifndef APPS_AI_INK_APP_H_
#define APPS_AI_INK_APP_H_

#include <memory>
#include <string>

#include "apps/ai/ai_paint.h"
#include "apps/ai/voice_recorder.h"
#include "zos/app.h"

/**
 * AI ink painter: speak a subject, an image model paints it as a Chinese ink wash painting and the result is shown in
 * the panel's 16 grays (full screen). OK starts and stops the recording; while the picture is being made the page shows
 * the phase. On the picture: OK back, UP paint it again. Back on the page, UP shows the last
 * picture again. Uses the Bailian API Key of the AI Q&A app.
 */
class AiInkApp : public zos::App {
public:
    explicit AiInkApp(ZectrixBoard& board) : board_(board) {}

    const char* id() const override { return "inkpaint"; }
    const char* title() const override { return "AI 水墨"; }
    const char* summary() const override { return "语音作画，16 级灰度"; }
    zos::KeyHints hints() const override;
    bool fullscreen() const override { return showing_; }

    void OnEnter(zos::Context& ctx) override;
    void OnLeave(zos::Context& ctx) override;
    bool OnKey(zos::Context& ctx, zos::Key key) override;
    int64_t NextTickMs(zos::Context& ctx) override;
    void OnTick(zos::Context& ctx) override;
    void OnWake(zos::Context& ctx) override;
    void Render(zos::Context& ctx, ZectrixCanvas& canvas, const zos::Rect& content) override;

private:
    std::string Signature(const ai::paint::Status& status) const;
    void StartRecording(zos::Context& ctx);
    void FinishRecording(zos::Context& ctx);
    void ShowImage(zos::Context& ctx);

    ZectrixBoard& board_;
    std::unique_ptr<ai::VoiceRecorder> recorder_;
    bool recording_ = false;
    bool showing_ = false;           // the picture is on the panel (full screen)
    uint32_t shown_revision_ = 0;    // the newest picture already shown
    std::string shown_sig_;
};

#endif  // APPS_AI_INK_APP_H_
