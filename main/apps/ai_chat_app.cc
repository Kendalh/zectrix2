#include "apps/ai_chat_app.h"

#include <algorithm>
#include <cstdio>

#include "apps/ai/ai_platform.h"
#include "esp_log.h"
#include "zos/chrome.h"

namespace {

constexpr char kTag[] = "ai_chat";
constexpr int kMarginX = 10;
constexpr int kLinePitch = 18;   // 16 px glyphs + 2
constexpr int kHeadH = 22;       // counter line and its rule
constexpr int kRowH = 26;        // session list rows

const zos::ConfigField kFields[] = {
    {"key", "阿里云百炼 API Key", zos::ConfigField::Type::kSecret, "", nullptr,
     "用于千问对话和语音识别（同一个 Key）。留空则不修改"},
};

size_t Utf8Len(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead >> 5) == 0x6) return 2;
    if ((lead >> 4) == 0xE) return 3;
    if ((lead >> 3) == 0x1E) return 4;
    return 1;
}

// While a question is pending the panel keeps the screen as before the question: the question and its answer
// appear together, in one refresh, when the answer arrives (same rule as the original e-ink app).
void HidePending(ai::Session* s) {
    if (!s->pending) return;
    if (!s->messages.empty() && s->messages.back().role == ai::Message::User) s->messages.pop_back();
}

}  // namespace

const zos::ConfigField* AiChatApp::config_fields() const { return kFields; }
size_t AiChatApp::config_field_count() const { return sizeof(kFields) / sizeof(kFields[0]); }

zos::KeyHints AiChatApp::hints() const {
    zos::KeyHints hints;
    if (recording_) {
        hints.select = "结束录音";
    } else if (list_mode_) {
        hints.select = "确定";
    } else {
        hints.up_hold = "新建";
        hints.down_hold = "列表";
        hints.select = "提问";
    }
    return hints;
}

void AiChatApp::OnEnter(zos::Context& ctx) {
    ai::ChatService& service = ai::ChatService::Get();
    service.Start(NewChatPlatform());
    service.SetOnChange([&ctx] { ctx.Wake(); });
    if (!recorder_) recorder_ = ai::NewVoiceRecorder(board_);
    top_line_ = 0;
    shown_session_ = 0;  // re-anchor on the newest question
    shown_count_ = 0;
    list_mode_ = false;
    recording_ = false;
}

void AiChatApp::OnLeave(zos::Context&) {
    ai::ChatService::Get().SetOnChange(nullptr);
    if (recorder_ && recorder_->recording()) recorder_->Stop();
    recording_ = false;
}

void AiChatApp::OnConfigChanged(zos::Context& ctx) {
    ai::ChatService& service = ai::ChatService::Get();
    if (!service.Started()) return;
    const std::string key = ctx.settings().GetString("key", "");
    if (!key.empty()) service.SetApiKey(key);
    ctx.Invalidate();
}

int AiChatApp::LinesPerPage(const zos::Rect& content) const {
    return std::max(1, (content.h - kHeadH - 6) / kLinePitch);
}

// Greedy wrap by glyph widths (CJK breaks anywhere).
void AiChatApp::Wrap(const std::string& text, bool question, bool note, int width, const std::string& prefix,
                     ZectrixCanvas& canvas) {
    std::string current = prefix;
    int current_w = prefix.empty() ? 0 : canvas.TextWidth(prefix.c_str());
    auto flush = [&] {
        lines_.push_back({current, question, note});
        current.clear();
        current_w = 0;
    };
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\n') {
            flush();
            ++i;
            continue;
        }
        const size_t n = std::min(Utf8Len(static_cast<unsigned char>(text[i])), text.size() - i);
        const std::string ch = text.substr(i, n);
        i += n;
        if (ch == "\r") continue;
        const int w = canvas.TextWidth(ch.c_str());
        if (current_w + w > width && !current.empty()) flush();
        current += ch;
        current_w += w;
    }
    if (!current.empty() || lines_.empty()) flush();
}

void AiChatApp::BuildLines(const ai::Session& session, bool has_key, bool started, bool connected,
                           const std::string& ip, int width, ZectrixCanvas& canvas) {
    lines_.clear();
    answer_start_line_ = 0;
    if (!started || !connected) {
        Wrap("AI 服务未启动", false, false, width, "", canvas);
        Wrap("请先连接 Wi-Fi。", false, true, width, "", canvas);
        return;
    }
    if (!has_key) {
        Wrap("还没有设置百炼 API Key", false, false, width, "", canvas);
        Wrap("请用手机或电脑浏览器打开：", false, true, width, "", canvas);
        Wrap("http://" + ip + "/#/app/aichat", false, true, width, "", canvas);
        Wrap("填入阿里云百炼 API Key。", false, true, width, "", canvas);
        return;
    }
    if (session.messages.empty() && !session.pending) {
        Wrap("新会话", false, false, width, "", canvas);
        Wrap("按 OK 开始说出问题，再按 OK 结束并发送。", false, true, width, "", canvas);
        Wrap("长按 UP 新会话，长按 DOWN 会话列表。", false, true, width, "", canvas);
    }
    for (const ai::Message& m : session.messages) {
        if (m.role == ai::Message::User) {
            if (!lines_.empty()) lines_.push_back({"", false, true});  // gap between turns
            answer_start_line_ = static_cast<int>(lines_.size());
            Wrap(m.text, true, false, width, "问：", canvas);
        } else {
            Wrap(m.text, false, false, width, "", canvas);
        }
    }
    if (!session.last_error.empty()) {
        answer_start_line_ = static_cast<int>(lines_.size());
        Wrap("提问失败：" + session.last_error, false, true, width, "", canvas);
    }
}

std::string AiChatApp::Signature(const ai::Session& s, bool has_key, bool started) const {
    return std::to_string(s.id) + "/" + std::to_string(s.messages.size()) + "/" + (s.pending ? "p" : "-") + "/" +
           s.last_error + "/" + (has_key ? "k" : "-") + (started ? "s" : "-") + (recording_ ? "r" : "-");
}

void AiChatApp::StartRecording(zos::Context& ctx) {
    ai::ChatService& service = ai::ChatService::Get();
    const int id = service.CurrentSessionId();
    std::string error;
    if (!service.VoiceReady(id, &error)) {  // no key / busy: say so instead of recording for nothing
        service.SetSessionError(id, error);
        return;
    }
    if (!recorder_->Start()) {
        service.SetSessionError(id, "麦克风无法启动");
        return;
    }
    recording_ = true;
    rec_session_ = id;
    ESP_LOGI(kTag, "recording for session %d", id);
}

void AiChatApp::FinishRecording(zos::Context&) {
    recording_ = false;
    ai::ChatService& service = ai::ChatService::Get();
    std::string wav = recorder_->Stop();
    if (wav.empty()) {
        service.SetSessionError(rec_session_, "没有录到声音，请再试一次");
        return;
    }
    std::string error;
    if (!service.AskVoice(rec_session_, std::move(wav), &error)) service.SetSessionError(rec_session_, error);
}

bool AiChatApp::OnKey(zos::Context& ctx, zos::Key key) {
    ai::ChatService& service = ai::ChatService::Get();
    if (recording_) {  // the microphone is busy: only OK ends it
        if (key == zos::Key::kSelect) {
            FinishRecording(ctx);
            ctx.Invalidate();
        }
        return true;
    }

    // Holds work in both views: hold UP = new session, hold DOWN = the session list.
    if (key == zos::Key::kUpHold || key == zos::Key::kDownHold) {
        if (!service.Started()) return true;
        if (key == zos::Key::kUpHold) {
            if (service.CreateSession() >= 0) {
                list_mode_ = false;
                top_line_ = 0;
            }
        } else {
            list_mode_ = true;
            list_sel_ = 0;
            const std::vector<ai::SessionInfo> sessions = service.List();
            for (size_t i = 0; i < sessions.size(); ++i)
                if (sessions[i].id == service.CurrentSessionId()) list_sel_ = static_cast<int>(i) + 1;
        }
        ctx.Invalidate();
        return true;
    }

    if (list_mode_ && service.Started()) {
        const std::vector<ai::SessionInfo> sessions = service.List();
        const int rows = static_cast<int>(sessions.size()) + 2;  // new, sessions..., delete current
        switch (key) {
            case zos::Key::kUp: list_sel_ = (list_sel_ + rows - 1) % rows; break;
            case zos::Key::kDown: list_sel_ = (list_sel_ + 1) % rows; break;
            case zos::Key::kSelect:
                if (list_sel_ == 0) {
                    if (service.CreateSession() < 0) return true;  // full: stay in the list
                } else if (list_sel_ == rows - 1) {
                    service.DeleteSession(service.CurrentSessionId());  // the last one is replaced by an empty one
                } else {
                    service.SelectSession(sessions[list_sel_ - 1].id);
                }
                list_mode_ = false;
                break;
            default: return false;
        }
        ctx.Invalidate();
        return true;
    }

    switch (key) {
        case zos::Key::kUp:
            top_line_ = std::max(0, top_line_ - per_page_);
            ctx.Invalidate();
            return true;
        case zos::Key::kDown: {
            const int last_top = std::max(0, static_cast<int>(lines_.size()) - per_page_);
            top_line_ = std::min(last_top, top_line_ + per_page_);
            ctx.Invalidate();
            return true;
        }
        case zos::Key::kSelect:
            if (!service.Started() || !service.HasApiKey()) return true;
            StartRecording(ctx);
            ctx.Invalidate();
            return true;
        default:
            return false;
    }
}

int64_t AiChatApp::NextTickMs(zos::Context&) { return recording_ ? 1000 : -1; }

void AiChatApp::OnTick(zos::Context& ctx) {
    if (!recording_) return;
    if (!recorder_->recording()) {  // the time limit ended it: send what was said
        FinishRecording(ctx);
    }
    ctx.Invalidate();
}

void AiChatApp::OnWake(zos::Context& ctx) {
    ai::ChatService& service = ai::ChatService::Get();
    ai::Session session;
    if (service.Started() && service.Snapshot(service.CurrentSessionId(), &session)) {
        HidePending(&session);
        if (Signature(session, service.HasApiKey(), true) == shown_sig_) return;  // nothing visible changed
    }
    ctx.Invalidate();
}

void AiChatApp::RenderStatusBox(ZectrixCanvas& canvas, const zos::Rect& content, const char* line1, const char* line2) {
    const int cy = content.y + content.h / 2;
    canvas.Rect(30, cy - 44, 340, 88);
    canvas.Text((ZectrixCanvas::kWidth - canvas.TextWidth(line1, 2)) / 2, cy - 30, line1, 2);
    canvas.Text((ZectrixCanvas::kWidth - canvas.TextWidth(line2)) / 2, cy + 14, line2);
}

void AiChatApp::Render(zos::Context& ctx, ZectrixCanvas& canvas, const zos::Rect& content) {
    ai::ChatService& service = ai::ChatService::Get();
    const bool started = service.Started();
    const bool has_key = service.HasApiKey();
    ai::Session session;
    const std::vector<ai::SessionInfo> sessions = service.List();
    int session_index = 0;
    if (started) {
        const int current = service.CurrentSessionId();
        service.Snapshot(current, &session);
        HidePending(&session);
        for (size_t i = 0; i < sessions.size(); ++i)
            if (sessions[i].id == current) session_index = static_cast<int>(i);
    }
    shown_sig_ = Signature(session, has_key, started);

    if (recording_) {
        char elapsed[48];
        std::snprintf(elapsed, sizeof(elapsed), "已录 %d 秒（最长 %d 秒）", recorder_->elapsed_ms() / 1000,
                      ai::kMaxRecordSeconds);
        RenderStatusBox(canvas, content, "录音中…", elapsed);
        return;
    }
    if (list_mode_ && started) {
        RenderList(canvas, content);
        return;
    }

    BuildLines(session, has_key, started, ctx.net().connected(), ctx.net().ip(),
               ZectrixCanvas::kWidth - 2 * kMarginX, canvas);
    // New session / new messages: start at the newest question so its answer is read from the top.
    if (session.id != shown_session_ || session.messages.size() != shown_count_) {
        top_line_ = answer_start_line_;
        shown_session_ = session.id;
        shown_count_ = session.messages.size();
    }
    per_page_ = LinesPerPage(content);
    top_line_ = std::max(0, std::min(top_line_, std::max(0, static_cast<int>(lines_.size()) - per_page_)));
    const int pages = std::max(1, (static_cast<int>(lines_.size()) + per_page_ - 1) / per_page_);
    const int page = (top_line_ + per_page_ - 1) / per_page_;  // a page starting mid-way counts as the next

    char counter[64];
    std::snprintf(counter, sizeof(counter), "会话 %d/%d  第 %d/%d 页", session_index + 1,
                  static_cast<int>(std::max<size_t>(1, sessions.size())), page + 1, pages);
    canvas.Text(kMarginX, content.y + 3, counter);
    if (session.pending) {
        const char* wait = session.transcribing ? "识别中…" : "思考中…";
        canvas.Text(ZectrixCanvas::kWidth - kMarginX - canvas.TextWidth(wait), content.y + 3, wait);
    }
    canvas.FillGray(kMarginX, content.y + kHeadH - 2, ZectrixCanvas::kWidth - 2 * kMarginX, 2, 8);

    const int text_top = content.y + kHeadH + 4;
    const int shown = std::min(per_page_, static_cast<int>(lines_.size()) - top_line_);
    for (int row = 0; row < shown; ++row) {
        const Line& line = lines_[top_line_ + row];
        const int y = text_top + row * kLinePitch;
        canvas.Text(kMarginX, y, line.text.c_str(), 1, line.question);
    }
}

void AiChatApp::RenderList(ZectrixCanvas& canvas, const zos::Rect& content) {
    ai::ChatService& service = ai::ChatService::Get();
    const std::vector<ai::SessionInfo> sessions = service.List();
    const int current = service.CurrentSessionId();
    const int rows_total = static_cast<int>(sessions.size()) + 2;
    list_sel_ = std::max(0, std::min(list_sel_, rows_total - 1));

    canvas.Text(kMarginX, content.y + 3, "会话列表");
    canvas.FillGray(kMarginX, content.y + kHeadH - 2, ZectrixCanvas::kWidth - 2 * kMarginX, 2, 8);

    const int list_top = content.y + kHeadH + 4;
    const int visible = std::max(1, (content.y + content.h - list_top) / kRowH);
    const int first = std::max(0, std::min(list_sel_ - visible + 1, rows_total - visible));
    for (int row = 0; row < std::min(visible, rows_total - first); ++row) {
        const int index = first + row;
        const int y = list_top + row * kRowH;
        const bool selected = index == list_sel_;
        std::string label, tail;
        if (index == 0) {
            label = "+ 新会话";
        } else if (index == rows_total - 1) {
            label = "× 删除当前会话";
        } else {
            const ai::SessionInfo& s = sessions[index - 1];
            label = std::string(s.id == current ? "> " : "  ") + std::to_string(index) + ". " +
                    (s.title.empty() ? "（空）" : s.title);
            tail = s.pending ? "回答中" : std::to_string(s.message_count / 2) + "轮";
        }
        if (selected) canvas.FillRect(kMarginX, y, ZectrixCanvas::kWidth - 2 * kMarginX, kRowH - 2, true);
        canvas.Text(kMarginX + 6, y + 5, label.c_str(), 1, selected);
        if (!tail.empty()) {
            canvas.Text(ZectrixCanvas::kWidth - kMarginX - 6 - canvas.TextWidth(tail.c_str()), y + 5, tail.c_str(), 1,
                        selected);
        }
    }
}
