#ifndef APPS_TOKEN_BOARD_APP_H_
#define APPS_TOKEN_BOARD_APP_H_

#include <string>
#include <vector>

#include "apps/token/token_board.h"
#include "zos/app.h"

/**
 * Token board: AI account balance and cost, one provider per page (Alibaba Bailian with a per-model breakdown,
 * DeepSeek with the balance only). UP/DOWN switch the provider, OK refreshes. Nothing is fetched in the
 * background: data older than the refresh interval is fetched when the page is entered or switched to.
 * Keys and the interval are set on the web page. See apps/token/token_board.h for how today/this hour are estimated.
 */
class TokenBoardApp : public zos::App {
public:
    const char* id() const override { return "token"; }
    const char* title() const override { return "Token 看板"; }
    const char* summary() const override { return "AI 余额和费用"; }
    zos::KeyHints hints() const override;
    const zos::ConfigField* config_fields() const override;
    size_t config_field_count() const override;

    void OnEnter(zos::Context& ctx) override;
    void OnLeave(zos::Context& ctx) override;
    bool OnKey(zos::Context& ctx, zos::Key key) override;
    void OnWake(zos::Context& ctx) override;
    void OnConfigChanged(zos::Context& ctx) override;
    void Render(zos::Context& ctx, ZectrixCanvas& canvas, const zos::Rect& content) override;

private:
    struct Status {
        bool configured = false;
        bool loading = false;
        std::string error;
        time_t updated = 0;
        std::string signature;
    };
    struct Row {
        std::string name;
        double value = 0;
    };

    Status CurrentStatus() const;
    void RefreshProvider(bool force) const;
    void RenderHero(ZectrixCanvas& canvas, const zos::Rect& content, bool has_balance, double balance, double today,
                    double hour);
    void RenderRowsCard(ZectrixCanvas& canvas, const zos::Rect& card, const char* title, const std::string& right_text,
                        const std::vector<Row>& rows, const char* empty_text);

    int provider_ = 0;
    std::string shown_sig_;
};

#endif  // APPS_TOKEN_BOARD_APP_H_
