#ifndef APPS_AI_CHAT_APP_H_
#define APPS_AI_CHAT_APP_H_

#include <memory>
#include <string>
#include <vector>

#include "apps/ai/ai_chat.h"
#include "apps/ai/voice_recorder.h"
#include "zos/app.h"

/**
 * AI Q&A (Alibaba Bailian Qwen). The question is spoken: OK starts the microphone, OK again stops it, the speech
 * model turns the recording into text and the answer appears when it is complete (non-streaming; the panel is too
 * slow for streamed text). UP/DOWN page through the conversation; hold UP = new session, hold DOWN = the session list
 * (new / switch / delete). The API key is set on the web page (secret field).
 */
class AiChatApp : public zos::App {
public:
    explicit AiChatApp(ZectrixBoard& board) : board_(board) {}

    const char* id() const override { return "aichat"; }
    const char* title() const override { return "AI 问答"; }
    const char* summary() const override { return "千问语音对话"; }
    zos::KeyHints hints() const override;
    const zos::ConfigField* config_fields() const override;
    size_t config_field_count() const override;

    void OnEnter(zos::Context& ctx) override;
    void OnLeave(zos::Context& ctx) override;
    bool OnKey(zos::Context& ctx, zos::Key key) override;
    int64_t NextTickMs(zos::Context& ctx) override;
    void OnTick(zos::Context& ctx) override;
    void OnWake(zos::Context& ctx) override;
    void OnConfigChanged(zos::Context& ctx) override;
    void Render(zos::Context& ctx, ZectrixCanvas& canvas, const zos::Rect& content) override;

private:
    struct Line {
        std::string text;
        bool question = false;
        bool note = false;  // hints and errors: drawn in gray
    };

    void BuildLines(const ai::Session& session, bool has_key, bool started, bool connected, const std::string& ip,
                    int width, ZectrixCanvas& canvas);
    void Wrap(const std::string& text, bool question, bool note, int width, const std::string& prefix,
              ZectrixCanvas& canvas);
    int LinesPerPage(const zos::Rect& content) const;
    std::string Signature(const ai::Session& session, bool has_key, bool started) const;
    void StartRecording(zos::Context& ctx);
    void FinishRecording(zos::Context& ctx);
    void RenderList(ZectrixCanvas& canvas, const zos::Rect& content);
    void RenderStatusBox(ZectrixCanvas& canvas, const zos::Rect& content, const char* line1, const char* line2);

    ZectrixBoard& board_;
    std::unique_ptr<ai::VoiceRecorder> recorder_;
    std::vector<Line> lines_;
    int answer_start_line_ = 0;
    int top_line_ = 0;
    int shown_session_ = 0;
    size_t shown_count_ = 0;
    int per_page_ = 8;
    bool list_mode_ = false;
    int list_sel_ = 0;
    bool recording_ = false;
    int rec_session_ = 0;
    std::string shown_sig_;
};

#endif  // APPS_AI_CHAT_APP_H_
