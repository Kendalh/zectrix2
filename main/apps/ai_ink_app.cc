#include "apps/ai_ink_app.h"

#include <algorithm>
#include <cstdio>

#include "apps/ai/ai_platform.h"
#include "apps/photo/photo_store.h"
#include "esp_log.h"
#include "zos/chrome.h"

namespace {

constexpr char kTag[] = "ai_ink";

void CenterText(ZectrixCanvas& canvas, int y, const char* text, int scale = 1) {
    canvas.Text(std::max(4, (ZectrixCanvas::kWidth - canvas.TextWidth(text, scale)) / 2), y, text, scale);
}

}  // namespace

zos::KeyHints AiInkApp::hints() const {
    zos::KeyHints hints;
    if (showing_) {  // on the picture: UP paint again, DOWN save to the album, OK speak a new subject
        hints.up = "重画";
        hints.select = "说话";
        const uint32_t revision = ai::paint::PaintService::Get().GetStatus().revision;
        hints.down = saved_revision_ == revision ? "已保存" : save_failure_ == 2 ? "相册已满"
                     : save_failure_ == 1 ? "保存失败" : "保存";
    } else if (recording_) {
        hints.select = "结束录音";
    } else {
        if (ai::paint::PaintService::Get().GetStatus().has_image) hints.up = "上一幅";
        hints.select = "说话";
    }
    return hints;
}

void AiInkApp::OnEnter(zos::Context& ctx) {
    ai::paint::PaintService& service = ai::paint::PaintService::Get();
    service.Start(NewChatPlatform());
    service.SetOnChange([&ctx] { ctx.Wake(); });
    if (!recorder_) recorder_ = ai::NewVoiceRecorder(board_);
    recording_ = false;
    showing_ = false;
    shown_sig_.clear();
    shown_revision_ = service.GetStatus().revision;  // entering again does not pop up an old picture
}

void AiInkApp::OnLeave(zos::Context&) {
    ai::paint::PaintService::Get().SetOnChange(nullptr);
    if (recorder_ && recorder_->recording()) recorder_->Stop();
    recording_ = false;
    showing_ = false;
}

void AiInkApp::ShowImage(zos::Context& ctx) {
    std::vector<uint8_t> image;
    ai::paint::PaintService& service = ai::paint::PaintService::Get();
    if (!service.CopyImage(&image) || image.size() != ai::paint::kImageBytes) return;
    const uint32_t revision = service.GetStatus().revision;
    if (revision != shown_revision_) save_failure_ = 0;
    shown_revision_ = revision;
    showing_ = true;  // before the refresh: the footer of the picture shows the hints of this state
    const esp_err_t err = ctx.ShowImageUnderChrome(image.data(), image.size());
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "4bpp refresh failed: %s", esp_err_to_name(err));
        service.SetError("屏幕刷新失败");
        showing_ = false;
    }
}

void AiInkApp::StartRecording(zos::Context&) {
    ai::paint::PaintService& service = ai::paint::PaintService::Get();
    std::string error;
    if (!service.VoiceReady(&error)) {
        service.SetError(error);
        return;
    }
    if (!recorder_->Start()) {
        service.SetError("麦克风无法启动");
        return;
    }
    recording_ = true;
}

void AiInkApp::FinishRecording(zos::Context&) {
    recording_ = false;
    ai::paint::PaintService& service = ai::paint::PaintService::Get();
    std::string wav = recorder_->Stop();
    if (wav.empty()) {
        service.SetError("没有录到声音，请再试一次");
        return;
    }
    std::string error;
    if (!service.AskVoice(std::move(wav), &error)) service.SetError(error);
}

void AiInkApp::SaveToAlbum(const ai::paint::Status& status) {
    if (saved_revision_ == status.revision) return;  // already in the album: the footer says so
    std::vector<uint8_t> image;
    if (!ai::paint::PaintService::Get().CopyImage(&image)) {
        save_failure_ = 1;
        return;
    }
    // The painting is a whole-panel picture, just like the album's: the header and footer were only laid over it.
    std::string error;
    if (photos::Store::Get().Save(status.subject, image.data(), image.size(), &error) < 0) {
        save_failure_ = error.find("已满") != std::string::npos ? 2 : 1;
        return;
    }
    saved_revision_ = status.revision;
    save_failure_ = 0;
}

bool AiInkApp::OnKey(zos::Context& ctx, zos::Key key) {
    ai::paint::PaintService& service = ai::paint::PaintService::Get();
    if (showing_) {  // the picture is up (header and footer stay): UP = paint it again, DOWN = save to the album,
                     // OK = speak a new subject; nothing pages through anything here
        switch (key) {
            case zos::Key::kSelect:
                showing_ = false;
                StartRecording(ctx);
                ctx.Invalidate();  // the shell redraws the page with a full refresh
                break;
            case zos::Key::kUp: {
                showing_ = false;
                std::string error;
                if (!service.Regenerate(&error)) service.SetError(error);
                ctx.Invalidate();
                break;
            }
            case zos::Key::kDown:
                SaveToAlbum(service.GetStatus());
                ctx.UpdateContentFooter();  // the footer tells the result; the picture is not redrawn
                break;
            default:
                break;
        }
        return true;
    }
    if (recording_) {  // the microphone is busy: only OK ends it
        if (key == zos::Key::kSelect) {
            FinishRecording(ctx);
            ctx.Invalidate();
        }
        return true;
    }
    switch (key) {
        case zos::Key::kSelect:
            if (service.GetStatus().working) return true;
            StartRecording(ctx);
            ctx.Invalidate();
            return true;
        case zos::Key::kUp:
            if (service.GetStatus().has_image && !service.GetStatus().working) {
                ShowImage(ctx);
                if (!showing_) ctx.Invalidate();
                return true;
            }
            return false;
        default:
            return false;
    }
}

int64_t AiInkApp::NextTickMs(zos::Context&) { return recording_ ? 1000 : -1; }

void AiInkApp::OnTick(zos::Context& ctx) {
    if (!recording_) return;
    if (!recorder_->recording()) FinishRecording(ctx);  // the time limit ended it: use what was said
    ctx.Invalidate();
}

std::string AiInkApp::Signature(const ai::paint::Status& s) const {
    return std::to_string(static_cast<int>(s.phase)) + "|" + s.subject + "|" + s.error + "|" +
           std::to_string(s.revision) + (s.has_image ? "i" : "-") + (recording_ ? "r" : "-");
}

void AiInkApp::OnWake(zos::Context& ctx) {
    if (showing_ || recording_) return;
    const ai::paint::Status status = ai::paint::PaintService::Get().GetStatus();
    if (status.phase == ai::paint::Phase::Ready && status.has_image && status.revision != shown_revision_) {
        ShowImage(ctx);  // a new picture is finished: show it full screen
        if (showing_) return;
    }
    if (Signature(status) != shown_sig_) ctx.Invalidate();
}

void AiInkApp::Render(zos::Context& ctx, ZectrixCanvas& canvas, const zos::Rect& content) {
    ai::paint::PaintService& service = ai::paint::PaintService::Get();
    const ai::paint::Status status = service.GetStatus();
    shown_sig_ = Signature(status);
    const int cy = content.y + content.h / 2;

    if (recording_) {
        char elapsed[48];
        std::snprintf(elapsed, sizeof(elapsed), "已录 %d 秒（最长 %d 秒）", recorder_->elapsed_ms() / 1000,
                      ai::kMaxRecordSeconds);
        canvas.Rect(30, cy - 44, 340, 88);
        CenterText(canvas, cy - 30, "请说出想画的内容…", 1);
        CenterText(canvas, cy - 8, elapsed);
        CenterText(canvas, cy + 14, "说完按 OK");
        return;
    }
    if (!service.HasApiKey()) {
        CenterText(canvas, content.y + 20, "AI 水墨", 2);
        CenterText(canvas, content.y + 76, "还没有设置百炼 API Key");
        CenterText(canvas, content.y + 102, "请在浏览器打开：");
        const std::string url = "http://" + (ctx.net().ip().empty() ? std::string("设备地址") : ctx.net().ip()) +
                                "/#/app/aichat";
        CenterText(canvas, content.y + 126, url.c_str());
        CenterText(canvas, content.y + 152, "（与 AI 问答共用同一个 Key）");
        return;
    }

    CenterText(canvas, content.y + 12, "AI 水墨", 2);
    canvas.FillGray(60, content.y + 52, ZectrixCanvas::kWidth - 120, 2, 8);

    if (status.working) {
        CenterText(canvas, content.y + 72, "正在绘图…");
        return;
    }

    if (status.phase == ai::paint::Phase::Error) {
        CenterText(canvas, content.y + 72, "没有画成");
        // The error can be long: wrap it by characters into at most two lines.
        std::string rest = status.error, line;
        int y = content.y + 98;
        for (int rows = 0; rows < 2 && !rest.empty(); ++rows, y += 22) {
            line.clear();
            while (!rest.empty()) {
                const unsigned char lead = static_cast<unsigned char>(rest[0]);
                const size_t n = std::min<size_t>(lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4,
                                                  rest.size());
                if (canvas.TextWidth((line + rest.substr(0, n)).c_str()) > ZectrixCanvas::kWidth - 40) break;
                line += rest.substr(0, n);
                rest.erase(0, n);
            }
            CenterText(canvas, y, line.c_str());
        }
    } else {
        CenterText(canvas, content.y + 72, "按 OK，说出你想画的内容");
        CenterText(canvas, content.y + 100, "例如：雨中的西湖");
        CenterText(canvas, content.y + 124, "雪夜孤舟   黄山云海   竹林小径");
    }
    if (status.has_image) {
        const std::string line = "UP 看上一幅：" + status.subject;
        canvas.Text(std::max(4, (ZectrixCanvas::kWidth - canvas.TextWidth(line.c_str())) / 2), content.y + 190,
                    line.c_str());
    }
}
