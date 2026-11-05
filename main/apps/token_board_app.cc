#include "apps/token_board_app.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "apps/ai/ai_platform.h"
#include "zos/chrome.h"

namespace {

constexpr int kMargin = 8;
constexpr int kGap = 8;
constexpr int kHeaderH = 22;
constexpr int kHeroTop = kHeaderH + 4;
constexpr int kHeroH = 74;
constexpr int kRowH = 27;
constexpr int kMaxRows = 3;

const zos::ConfigField kFields[] = {
    {"ak_id", "阿里云 AccessKey ID", zos::ConfigField::Type::kString, "", nullptr,
     "建议用 RAM 子账号，只授予 AliyunBSSReadOnlyAccess（费用中心只读）"},
    {"ak_secret", "阿里云 AccessKey Secret", zos::ConfigField::Type::kSecret, "", nullptr,
     "与 ID 配套，保存后不再显示。留空则不修改"},
    {"ds_key", "DeepSeek API Key", zos::ConfigField::Type::kSecret, "", nullptr,
     "只能查询余额（DeepSeek 没有对 API Key 开放用量接口）。留空则不修改"},
    {"interval", "数据有效期（分钟）", zos::ConfigField::Type::kChoice, "60", "10|30|60",
     "进入页面时，数据超过这个时间才重新获取；不会在后台定时刷新"},
};

std::string Yuan(double amount) { return "¥" + board::FormatYuan(amount); }

// Progress bar in grays: a light track with a darker fill of `fraction` (0..1) and a black outline.
void DrawBar(ZectrixCanvas& canvas, int x, int y, int w, int h, double fraction) {
    canvas.FillGray(x, y, w, h, 2);
    const int fill = std::max(2, static_cast<int>(w * std::min(1.0, std::max(0.0, fraction))));
    canvas.FillGray(x, y, std::min(w, fill), h, 9);
    canvas.Rect(x, y, w, h);
}

// Cut `text` to at most max_w pixels, ending in "…" when shortened.
std::string FitOneLine(ZectrixCanvas& canvas, const std::string& text, int max_w) {
    if (canvas.TextWidth(text.c_str()) <= max_w) return text;
    const int ellipsis_w = canvas.TextWidth("…");
    std::string out;
    int w = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        const size_t len = std::min<size_t>(lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4,
                                            text.size() - i);
        const std::string ch = text.substr(i, len);
        w += canvas.TextWidth(ch.c_str());
        if (w + ellipsis_w > max_w) break;
        out += ch;
        i += len;
    }
    return out + "…";
}

std::string SignatureOf(const board::DayBill& bill) {
    std::string sig = bill.ok ? "|ok" : "|--";
    for (const auto& m : bill.models) sig += m.model + std::to_string(m.cost);
    return sig;
}

}  // namespace

const zos::ConfigField* TokenBoardApp::config_fields() const { return kFields; }
size_t TokenBoardApp::config_field_count() const { return sizeof(kFields) / sizeof(kFields[0]); }

zos::KeyHints TokenBoardApp::hints() const {
    zos::KeyHints hints;
    hints.up = "上一家";
    hints.down = "下一家";
    hints.select = "刷新";
    return hints;
}

void TokenBoardApp::OnEnter(zos::Context& ctx) {
    board::BoardService& bailian = board::BoardService::Get();
    board::DeepSeekService& deepseek = board::DeepSeekService::Get();
    bailian.Start(NewBoardPlatform());
    deepseek.Start(NewBoardPlatform());
    bailian.SetOnChange([&ctx] { ctx.Wake(); });
    deepseek.SetOnChange([&ctx] { ctx.Wake(); });
    provider_ = 0;
    shown_sig_.clear();
    if (ctx.net().connected()) RefreshProvider(false);
}

void TokenBoardApp::OnLeave(zos::Context&) {
    board::BoardService::Get().SetOnChange(nullptr);
    board::DeepSeekService::Get().SetOnChange(nullptr);
}

void TokenBoardApp::OnConfigChanged(zos::Context& ctx) {
    zos::Settings& settings = ctx.settings();
    board::BoardService& bailian = board::BoardService::Get();
    const std::string id = settings.GetString("ak_id", "");
    const std::string secret = settings.GetString("ak_secret", "");
    if (!id.empty() && !secret.empty()) bailian.SetCredentials(id, secret);
    const std::string ds_key = settings.GetString("ds_key", "");
    if (!ds_key.empty()) board::DeepSeekService::Get().SetApiKey(ds_key);
    bailian.SetIntervalMinutes(std::atoi(settings.GetString("interval", "60").c_str()));
    ctx.Invalidate();
}

void TokenBoardApp::RefreshProvider(bool force) const {
    if (provider_ == 0) board::BoardService::Get().Refresh(force);
    else board::DeepSeekService::Get().Refresh(force);
}

TokenBoardApp::Status TokenBoardApp::CurrentStatus() const {
    Status st;
    std::string sig = std::to_string(provider_);
    if (provider_ == 0) {
        const board::Snapshot s = board::BoardService::Get().GetSnapshot();
        st.configured = s.configured;
        st.loading = s.loading;
        st.error = s.error;
        st.updated = s.updated;
        sig += std::string(s.configured ? "c" : "n") + (s.loading ? "L" : "l") + "|" + s.error + "|" +
               std::to_string(s.updated) + "|" + std::to_string(s.balance) + "|" + std::to_string(s.today_cost) + "|" + std::to_string(s.hour_cost) +
               SignatureOf(s.yesterday) + SignatureOf(s.today);
    } else {
        const board::DeepSeekSnapshot s = board::DeepSeekService::Get().GetSnapshot();
        st.configured = s.configured;
        st.loading = s.loading;
        st.error = s.error;
        st.updated = s.updated;
        sig += std::string(s.configured ? "c" : "n") + (s.loading ? "L" : "l") + "|" + s.error + "|" +
               std::to_string(s.updated) + "|" + std::to_string(s.balance.primary.total) + "|" + std::to_string(s.today_cost) + "|" +
               std::to_string(s.hour_cost) + (s.balance.available ? "|a" : "|u");
    }
    st.signature = sig;
    return st;
}

bool TokenBoardApp::OnKey(zos::Context& ctx, zos::Key key) {
    const int count = static_cast<int>(board::Providers().size());
    switch (key) {
        case zos::Key::kSelect:
            if (ctx.net().connected()) RefreshProvider(true);
            ctx.Invalidate();
            return true;
        case zos::Key::kUp:
        case zos::Key::kDown:
            if (count < 2) return false;
            provider_ = (provider_ + (key == zos::Key::kDown ? 1 : count - 1)) % count;
            if (ctx.net().connected()) RefreshProvider(false);  // stale data of the newly shown provider is fetched now
            ctx.Invalidate();
            return true;
        default:
            return false;
    }
}

void TokenBoardApp::OnWake(zos::Context& ctx) {
    if (CurrentStatus().signature != shown_sig_) ctx.Invalidate();
}

void TokenBoardApp::Render(zos::Context& ctx, ZectrixCanvas& canvas, const zos::Rect& content) {
    const Status st = CurrentStatus();
    shown_sig_ = st.signature;
    const auto& providers = board::Providers();

    // Header: provider name as a black label on the left, page dots on the right.
    const char* name = providers[provider_].name;
    const int pill_w = canvas.TextWidth(name) + 16;
    canvas.FillRect(kMargin, content.y + 2, pill_w, 20, true);
    canvas.Text(kMargin + 8, content.y + 4, name, 1, true);
    constexpr int kDot = 10;
    const int n = static_cast<int>(providers.size());
    int x = ZectrixCanvas::kWidth - kMargin - n * kDot - (n - 1) * 6;
    for (int i = 0; i < n; ++i, x += kDot + 6) {
        if (i == provider_) canvas.FillRect(x, content.y + 7, kDot, kDot, true);
        else canvas.Rect(x, content.y + 7, kDot, kDot);
    }

    if (!st.configured) {
        const char* msg = provider_ == 0 ? "尚未设置 AccessKey" : "尚未设置 API Key";
        const std::string url = "http://" + (ctx.net().ip().empty() ? std::string("设备地址") : ctx.net().ip()) +
                                "/#/app/token";
        const int cy = content.y + content.h / 2;
        canvas.Rect(kMargin, content.y + kHeroTop, ZectrixCanvas::kWidth - 2 * kMargin, content.h - kHeroTop - kGap);
        canvas.Text((ZectrixCanvas::kWidth - canvas.TextWidth(msg, 2)) / 2, cy - 30, msg, 2);
        canvas.Text(std::max(4, (ZectrixCanvas::kWidth - canvas.TextWidth("在浏览器打开：")) / 2), cy + 8, "在浏览器打开：");
        canvas.Text(std::max(4, (ZectrixCanvas::kWidth - canvas.TextWidth(url.c_str())) / 2), cy + 28, url.c_str());
        return;
    }

    const int card_top = content.y + kHeroTop + kHeroH + kGap;
    const int card_h = content.y + content.h - 20 - card_top;
    const int card_w = (ZectrixCanvas::kWidth - 2 * kMargin - kGap) / 2;
    if (provider_ == 0) {
        const board::Snapshot snap = board::BoardService::Get().GetSnapshot();
        RenderHero(canvas, content, snap.has_balance, snap.balance, snap.today_cost, snap.hour_cost);
        const struct { const char* title; const board::DayBill* bill; bool pending; } days[2] = {
            {"昨日", &snap.yesterday, snap.loading && !snap.yesterday.ok},
            {"今日(延迟)", &snap.today, snap.loading && !snap.today.ok}};
        for (int i = 0; i < 2; ++i) {
            std::vector<Row> rows;
            for (size_t m = 0; m < days[i].bill->models.size() && m < static_cast<size_t>(kMaxRows); ++m) {
                rows.push_back({days[i].bill->models[m].model, days[i].bill->models[m].cost});
            }
            RenderRowsCard(canvas, {kMargin + i * (card_w + kGap), card_top, card_w, card_h}, days[i].title,
                           days[i].bill->ok ? Yuan(days[i].bill->total) : "", rows,
                           days[i].bill->ok ? "暂无账单" : (days[i].pending ? "加载中…" : "—"));
        }
    } else {
        // DeepSeek has no usage API an API key can call, so the card shows what the balance consists of.
        const board::DeepSeekSnapshot snap = board::DeepSeekService::Get().GetSnapshot();
        RenderHero(canvas, content, snap.has_balance, snap.balance.primary.total, snap.today_cost, snap.hour_cost);
        std::vector<Row> rows;
        if (snap.has_balance) {
            rows.push_back({"充值余额", snap.balance.primary.topped_up});
            rows.push_back({"赠送余额", snap.balance.primary.granted});
            for (const auto& line : snap.balance.lines) {
                if (line.currency != snap.balance.primary.currency) rows.push_back({line.currency + " 余额", line.total});
            }
        }
        RenderRowsCard(canvas, {kMargin, card_top, ZectrixCanvas::kWidth - 2 * kMargin, card_h}, "余额构成",
                       snap.has_balance
                           ? (snap.balance.available ? snap.balance.primary.currency + " 可用" : "余额不足")
                           : "",
                       rows, snap.loading ? "加载中…" : "—");
    }

    // Status line under the cards.
    std::string status;
    if (!ctx.net().connected()) {
        status = "未连接 Wi-Fi";
    } else if (st.loading) {
        status = "刷新中…";
    } else if (!st.error.empty()) {
        status = FitOneLine(canvas, "失败：" + st.error, ZectrixCanvas::kWidth - 2 * kMargin);
    } else if (st.updated) {
        const board::DateParts t = board::BeijingParts(st.updated);
        char buf[24];
        std::snprintf(buf, sizeof(buf), "更新 %02d:%02d", t.hour, static_cast<int>((st.updated % 3600) / 60));
        status = buf;
    }
    if (!status.empty()) {
        canvas.Text(ZectrixCanvas::kWidth - kMargin - canvas.TextWidth(status.c_str()), content.y + content.h - 18,
                    status.c_str());
    }
}

// Top: balance in the biggest text that fits on the left, today and this hour stacked on the right.
// Today/hour are balance-based estimates (see token_board.h).
void TokenBoardApp::RenderHero(ZectrixCanvas& canvas, const zos::Rect& content, bool has_balance, double balance,
                               double today, double hour) {
    constexpr int kHeroW = 208;
    const int top = content.y + kHeroTop;
    canvas.Rect(kMargin, top, kHeroW, kHeroH);
    canvas.Rect(kMargin + 1, top + 1, kHeroW - 2, kHeroH - 2);
    canvas.Text(kMargin + 12, top + 4, "余额");
    const std::string text = has_balance ? Yuan(balance) : "—";
    const int scale = canvas.TextWidth(text.c_str(), 3) <= kHeroW - 24 ? 3 : 2;
    canvas.Text(kMargin + 12, top + 22 + (3 - scale) * 8, text.c_str(), scale);

    const int side_x = kMargin + kHeroW + kGap;
    const int side_w = ZectrixCanvas::kWidth - kMargin - side_x;
    const int side_h = (kHeroH - kGap) / 2;
    const struct { const char* label; double value; } smalls[2] = {{"今日", today}, {"本小时", hour}};
    for (int i = 0; i < 2; ++i) {
        const int y = top + i * (side_h + kGap);
        canvas.Rect(side_x, y, side_w, side_h);
        if (i == 0) canvas.FillGray(side_x + 1, y + 1, side_w - 2, side_h - 2, 2);  // today: light gray ground
        canvas.Text(side_x + 8, y + (side_h - 16) / 2, smalls[i].label);
        const std::string value = has_balance ? Yuan(smalls[i].value) : "—";
        canvas.Text(side_x + side_w - 8 - canvas.TextWidth(value.c_str()), y + (side_h - 16) / 2, value.c_str());
    }
}

// A card with a title chip and a right-aligned total, then at most kMaxRows rows, each a name, an amount and a gray
// bar whose length is the row's share of the largest value.
void TokenBoardApp::RenderRowsCard(ZectrixCanvas& canvas, const zos::Rect& card, const char* title,
                                   const std::string& right_text, const std::vector<Row>& rows,
                                   const char* empty_text) {
    canvas.Rect(card.x, card.y, card.w, card.h);
    const int chip_w = canvas.TextWidth(title) + 12;
    canvas.FillRect(card.x + 6, card.y + 3, chip_w, 18, true);
    canvas.Text(card.x + 12, card.y + 4, title, 1, true);
    if (!right_text.empty()) {
        canvas.Text(card.x + card.w - 8 - canvas.TextWidth(right_text.c_str()), card.y + 4, right_text.c_str());
    }

    const int inner_x = card.x + 8;
    const int inner_w = card.w - 16;
    int row_y = card.y + 26;
    if (rows.empty()) {
        canvas.Text(inner_x + 2, row_y + 4, empty_text);
        return;
    }
    double top = 0;
    for (const auto& r : rows) top = std::max(top, r.value);
    if (top <= 0) top = 1;
    for (size_t i = 0; i < rows.size() && i < static_cast<size_t>(kMaxRows); ++i, row_y += kRowH) {
        const std::string amount = board::FormatYuan(rows[i].value);
        const int amount_w = canvas.TextWidth(amount.c_str());
        canvas.Text(inner_x, row_y, FitOneLine(canvas, rows[i].name, inner_w - amount_w - 8).c_str());
        canvas.Text(inner_x + inner_w - amount_w, row_y, amount.c_str());
        DrawBar(canvas, inner_x, row_y + 18, inner_w, 7, rows[i].value / top);
    }
}
