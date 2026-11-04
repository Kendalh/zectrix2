#include "apps/ai/ai_paint.h"

#include <cJSON.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <thread>

#include "apps/ai/png_decode.h"

namespace ai {
namespace paint {

namespace {

// The look: traditional Chinese ink wash painting (shuimo) on rice paper, shown in the panel's 16 grays. The scene itself
// (subject, time of day, weather) comes from the planner; only the style that must hold for every picture is fixed here.
const char kPlannerSystem[] =
    "You write image prompts for a traditional Chinese ink wash painting (shuimo) that will be shown on a 16-level grayscale "
    "e-ink display. The painting is black ink on white rice paper only: no color at all, just shades of gray from pale wash "
    "to deep black.\n"
    "The user gives a short subject (often Chinese, spoken aloud). Reply with ONE English paragraph of at most 90 words "
    "describing the painting, and nothing else: no preface, no quotes, no lists, no Markdown.\n"
    "Rules:\n"
    "1. Name the subject's real, recognisable features (for a landmark: its true shape and structure; for an animal or "
    "person: pose and silhouette). It is the focal point, painted with confident brush strokes: bold deep-black ink for the "
    "main forms, medium ink for the body, pale wet washes for distance and mist, dry-brush strokes for texture.\n"
    "2. Compose like a Song-dynasty scroll: asymmetric, poetic, with generous empty space (liubai) of white paper or soft mist "
    "left on purpose; distant hills fade into mist in several tonal layers; the horizon sits low or high, never in the "
    "middle.\n"
    "3. Take the time of day, weather and season from the user's words and express them only with ink tones: a moon or sun is "
    "an empty paper-white circle against a gray wash sky; rain is thin slanting strokes over misty washes; snow is "
    "paper-white shapes against a gray wash; night is a darker overall wash with the subject in pale paper tones; if nothing is "
    "said, use a misty morning.\n"
    "4. Add a few small fitting details (a tiny boat, a pine, bamboo, a bird, a hut, a small figure) and a foreground of water, "
    "rock or grass in a few strokes.\n"
    "5. A header bar covers the top 11% and a footer bar the bottom 10% of the picture: keep the subject and its important details away from the very top and bottom edges (sky, mist or water may run off there).\n"
    "6. Say that everything is monochrome ink. No text, no calligraphy, no seals, no color, no frame or border.";
const char kPosterPrefix[] = "Traditional Chinese ink wash painting (shuimo), black ink on white rice paper. ";
const char kPosterSuffix[] =
    " Monochrome grayscale only, rich ink gradation from deep black to pale gray wash, expressive brush strokes, dry-brush "
    "texture, generous empty space, soft misty atmosphere, off-white paper background. No color, no text, no seal, no border.";
const char kFallbackPrefix[] = "Traditional Chinese ink wash painting (shuimo) of ";
const char kFallbackMiddle[] =
    ", black ink on white rice paper. The subject is the focal point, painted with bold deep-black strokes and medium ink, "
    "distant hills and mist in pale washes, a small foreground of water or rock, asymmetric composition with generous "
    "empty space.";
const char kNegativePrompt[] =
    "color, colorful, red, blue, yellow, green, seal stamp, text, calligraphy, letters, watermark, photograph, 3D render, "
    "frame, border, cartoon";
constexpr size_t kMaxPlanChars = 1000;

bool IsUtf8Continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

size_t Utf8Len(unsigned char lead) {
    if (lead >= 0xF0) return 4;
    if (lead >= 0xE0) return 3;
    if (lead >= 0xC0) return 2;
    return 1;
}

bool IsFiller(const std::string& ch) {
    static const char* const kFiller[] = {"。", "，", "！", "？", "、", "…", "；", "：", "“", "”", "～"};
    if (ch.size() == 1) return !isalnum(static_cast<unsigned char>(ch[0])) && ch[0] != '(' && ch[0] != ')';
    for (const char* f : kFiller)
        if (ch == f) return true;
    return false;
}

std::vector<std::string> SplitChars(const std::string& s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        const size_t n = std::min(Utf8Len(static_cast<unsigned char>(s[i])), s.size() - i);
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

}  // namespace

std::string NormalizeSubject(const std::string& raw) {
    std::vector<std::string> chars = SplitChars(raw);
    size_t b = 0, e = chars.size();
    while (b < e && (IsFiller(chars[b]) || isspace(static_cast<unsigned char>(chars[b][0])))) ++b;
    while (e > b && (IsFiller(chars[e - 1]) || isspace(static_cast<unsigned char>(chars[e - 1][0])))) --e;
    std::string out;
    size_t count = 0;
    for (size_t i = b; i < e && count < kMaxSubjectChars; ++i, ++count) {
        out += (chars[i] == "\n" || chars[i] == "\r") ? " " : chars[i];
    }
    return out;
}

const char* PlannerSystemPrompt() { return kPlannerSystem; }

std::string BuildPlanRequestJson(const ChatConfig& config, const std::string& subject) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", config.model.c_str());
    cJSON* messages = cJSON_AddArrayToObject(root, "messages");
    cJSON* system = cJSON_CreateObject();
    cJSON_AddStringToObject(system, "role", "system");
    cJSON_AddStringToObject(system, "content", kPlannerSystem);
    cJSON_AddItemToArray(messages, system);
    cJSON* user = cJSON_CreateObject();
    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", subject.c_str());
    cJSON_AddItemToArray(messages, user);
    cJSON_AddNumberToObject(root, "max_tokens", 300);
    cJSON_AddBoolToObject(root, "stream", false);
    char* printed = cJSON_PrintUnformatted(root);
    std::string json = printed ? printed : "";
    cJSON_free(printed);
    cJSON_Delete(root);
    return json;
}

bool CleanPlan(const std::string& raw, std::string* description) {
    std::string text;
    for (char ch : raw) {  // one line; drop Markdown marks and quotes the model may wrap around the paragraph
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
        if (ch == '*' || ch == '#' || ch == '`' || ch == '"') continue;
        if (ch == ' ' && !text.empty() && text.back() == ' ') continue;
        text += ch;
    }
    size_t b = 0, e = text.size();
    while (b < e && text[b] == ' ') ++b;
    while (e > b && text[e - 1] == ' ') --e;
    text = text.substr(b, e - b);
    if (text.size() > kMaxPlanChars) {
        size_t cut = kMaxPlanChars;
        while (cut > 0 && IsUtf8Continuation(static_cast<unsigned char>(text[cut]))) --cut;
        text.resize(cut);
    }
    if (text.size() < 20) return false;  // an empty or one-word answer is not a scene
    *description = text;
    return true;
}

std::string ImagePrompt(const std::string& description) {
    return std::string(kPosterPrefix) + description + kPosterSuffix;
}

std::string FallbackPrompt(const std::string& subject) {
    return std::string(kFallbackPrefix) + subject + kFallbackMiddle + kPosterSuffix;
}

const char* NegativePrompt() { return kNegativePrompt; }

std::string BuildImageRequestJson(const PaintConfig& config, const std::string& prompt) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", config.model.c_str());
    cJSON* input = cJSON_AddObjectToObject(root, "input");
    cJSON* messages = cJSON_AddArrayToObject(input, "messages");
    cJSON* message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "user");
    cJSON* content = cJSON_AddArrayToObject(message, "content");
    cJSON* part = cJSON_CreateObject();
    cJSON_AddStringToObject(part, "text", prompt.c_str());
    cJSON_AddItemToArray(content, part);
    cJSON_AddItemToArray(messages, message);
    cJSON* params = cJSON_AddObjectToObject(root, "parameters");
    cJSON_AddStringToObject(params, "negative_prompt", kNegativePrompt);
    cJSON_AddStringToObject(params, "size", config.size.c_str());
    cJSON_AddNumberToObject(params, "n", 1);
    cJSON_AddBoolToObject(params, "prompt_extend", false);  // keep our wording
    cJSON_AddBoolToObject(params, "watermark", false);
    char* printed = cJSON_PrintUnformatted(root);
    std::string json = printed ? printed : "";
    cJSON_free(printed);
    cJSON_Delete(root);
    return json;
}

std::string FriendlyError(const std::string& e) {
    auto has = [&](const char* needle) { return e.find(needle) != std::string::npos; };
    if (has("rate limit") || has("Throttling") || has("Too many requests")) return "请求太频繁，请稍后再试";
    if (has("Arrearage") || has("overdue") || has("quota") || has("insufficient")) return "额度不足或账号欠费";
    if (has("Invalid API-key") || has("InvalidApiKey")) return "API Key 无效";
    if (has("DataInspectionFailed") || has("inappropriate content")) return "内容未通过审核，换个说法试试";
    return e;
}

bool ParseImageResponse(const std::string& body, std::string* image_url, std::string* error) {
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        *error = "响应格式错误";
        return false;
    }
    bool ok = false;
    cJSON* output = cJSON_GetObjectItemCaseSensitive(root, "output");
    cJSON* choices = cJSON_GetObjectItemCaseSensitive(output, "choices");
    cJSON* first = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : nullptr;
    cJSON* parts = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(first, "message"), "content");
    cJSON* part = cJSON_IsArray(parts) ? cJSON_GetArrayItem(parts, 0) : nullptr;
    cJSON* image = cJSON_GetObjectItemCaseSensitive(part, "image");
    cJSON* message = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (cJSON_IsString(image) && image->valuestring && image->valuestring[0]) {
        *image_url = image->valuestring;
        ok = true;
    } else if (cJSON_IsString(message) && message->valuestring) {
        *error = message->valuestring;
    } else {
        *error = "没有收到图片";
    }
    cJSON_Delete(root);
    return ok;
}

bool PngToGray16(const std::string& png_bytes, std::vector<uint8_t>* out, std::string* error) {
    png::Image img;
    std::string why;
    if (!png::Decode(reinterpret_cast<const uint8_t*>(png_bytes.data()), png_bytes.size(), &img, &why)) {
        *error = "图片解码失败: " + why;
        return false;
    }
    // The model may paint a paper-coloured frame: crop 2% on every side, then area-average to the panel size.
    const int cx = static_cast<int>(img.width * 2 / 100), cy = static_cast<int>(img.height * 2 / 100);
    const int cw = static_cast<int>(img.width) - 2 * cx, ch = static_cast<int>(img.height) - 2 * cy;
    if (cw < kWidth / 2 || ch < kHeight / 2) {
        *error = "图片尺寸太小";
        return false;
    }
    std::vector<uint8_t> luma(static_cast<size_t>(kWidth) * kHeight);
    int histogram[256] = {};
    for (int y = 0; y < kHeight; ++y) {
        const int sy0 = cy + y * ch / kHeight;
        const int sy1 = std::max(sy0 + 1, cy + (y + 1) * ch / kHeight);
        for (int x = 0; x < kWidth; ++x) {
            const int sx0 = cx + x * cw / kWidth;
            const int sx1 = std::max(sx0 + 1, cx + (x + 1) * cw / kWidth);
            int sum = 0, n = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                const uint8_t* p = img.rgb.data() + (static_cast<size_t>(sy) * img.width + sx0) * 3;
                for (int sx = sx0; sx < sx1; ++sx, p += 3, ++n) sum += p[0] * 299 + p[1] * 587 + p[2] * 114;
            }
            const uint8_t v = static_cast<uint8_t>(sum / n / 1000);
            luma[static_cast<size_t>(y) * kWidth + x] = v;
            ++histogram[v];
        }
    }
    // Auto-levels: the darkest 0.5% of the pixels become black ink, the lightest 2% become paper white, so a pale wash
    // still uses the whole range of 16 grays. Then a mild contrast curve around mid gray, as the demo images get.
    const int total = kWidth * kHeight;
    int lo = 0, hi = 255, acc = 0;
    for (int v = 0; v < 256; ++v) {
        acc += histogram[v];
        if (acc > total / 200) { lo = v; break; }
    }
    acc = 0;
    for (int v = 255; v >= 0; --v) {
        acc += histogram[v];
        if (acc > total / 50) { hi = v; break; }
    }
    if (hi - lo < 48) { lo = 0; hi = 255; }  // an almost flat picture: do not blow the noise up
    uint8_t curve[256];
    for (int v = 0; v < 256; ++v) {
        int s = (v - lo) * 255 / (hi - lo);
        s = std::min(255, std::max(0, s));
        s = 128 + (s - 128) * 125 / 100;
        curve[v] = static_cast<uint8_t>(std::min(255, std::max(0, s)));
    }

    // 16 levels with Floyd-Steinberg (serpentine): smooth washes stay smooth instead of banding.
    out->assign(kImageBytes, 0);
    std::vector<int16_t> row_a(kWidth + 2, 0), row_b(kWidth + 2, 0);
    int16_t* err_cur = row_a.data() + 1;
    int16_t* err_next = row_b.data() + 1;
    for (int y = 0; y < kHeight; ++y) {
        std::fill(err_next - 1, err_next - 1 + kWidth + 2, 0);
        const bool left_to_right = (y & 1) == 0;
        for (int i = 0; i < kWidth; ++i) {
            const int x = left_to_right ? i : kWidth - 1 - i;
            const int dir = left_to_right ? 1 : -1;
            const int want = curve[luma[static_cast<size_t>(y) * kWidth + x]] + err_cur[x];
            const int level = std::min(15, std::max(0, (want + 8) / 17));
            const int e = want - level * 17;
            err_cur[x + dir] += static_cast<int16_t>(e * 7 / 16);
            err_next[x - dir] += static_cast<int16_t>(e * 3 / 16);
            err_next[x] += static_cast<int16_t>(e * 5 / 16);
            err_next[x + dir] += static_cast<int16_t>(e * 1 / 16);
            const size_t pixel = static_cast<size_t>(y) * kWidth + x;
            (*out)[pixel >> 1] |= static_cast<uint8_t>((pixel & 1) ? level : level << 4);  // high nibble = left pixel
        }
        std::swap(err_cur, err_next);
    }
    return true;
}

// ---- service ----------------------------------------------------------------------------------

PaintService& PaintService::Get() {
    static PaintService instance;
    return instance;
}

void PaintService::Start(std::unique_ptr<ChatPlatform> platform) {
    std::lock_guard<std::mutex> lock(mu_);
    if (platform_) return;
    platform_ = std::move(platform);
    api_key_ = platform_->LoadApiKey();
}

bool PaintService::Started() {
    std::lock_guard<std::mutex> lock(mu_);
    return platform_ != nullptr;
}

void PaintService::SetOnChange(std::function<void()> on_change) {
    std::lock_guard<std::mutex> lock(mu_);
    on_change_ = std::move(on_change);
}

bool PaintService::HasApiKey() {
    std::lock_guard<std::mutex> lock(mu_);
    if (platform_) api_key_ = platform_->LoadApiKey();  // re-read: the key can be changed on the web page at any time
    return !api_key_.empty();
}

void PaintService::Notify() {
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cb = on_change_;
    }
    if (cb) cb();
}

bool PaintService::BeginLocked(Phase phase, std::string* error) {
    if (!platform_) {
        *error = "AI 服务未启动，请先连接 Wi-Fi";
        return false;
    }
    api_key_ = platform_->LoadApiKey();
    if (api_key_.empty()) {
        *error = "请先设置百炼 API Key";
        return false;
    }
    if (busy_) {
        *error = "正在绘制，请稍候";
        return false;
    }
    busy_ = true;
    status_.phase = phase;
    status_.working = true;
    status_.error.clear();
    return true;
}

bool PaintService::VoiceReady(std::string* error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!platform_) {
        *error = "AI 服务未启动，请先连接 Wi-Fi";
        return false;
    }
    api_key_ = platform_->LoadApiKey();
    if (api_key_.empty()) {
        *error = "请先设置百炼 API Key";
        return false;
    }
    if (busy_) {
        *error = "正在绘制，请稍候";
        return false;
    }
    return true;
}

void PaintService::SetError(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (busy_) return;
        status_.phase = Phase::Error;
        status_.error = message;
    }
    Notify();
}

void PaintService::SetPhase(Phase phase) {
    std::lock_guard<std::mutex> lock(mu_);
    status_.phase = phase;
}

void PaintService::Fail(const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        busy_ = false;
        status_.working = false;
        status_.phase = Phase::Error;
        status_.error = error;
    }
    Notify();
}

bool PaintService::AskVoice(std::string wav, std::string* error) {
    ChatPlatform* platform;
    ChatConfig config;
    std::string key;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!BeginLocked(Phase::Transcribing, error)) return false;
        platform = platform_.get();
        config = chat_config_;
        key = api_key_;
    }
    Notify();
    ChatPlatform::HttpRequest request;
    request.url = config.url;
    request.headers = {{"Content-Type", "application/json"}, {"Authorization", "Bearer " + key}};
    request.body_blob = std::move(wav);  // the platform encodes it on its own task
    SpeechBodyParts(config, &request.body_prefix, &request.body_suffix);
    platform->Send(std::move(request), [this](int status, std::string body, std::string transport_error) {
        OnTranscribed(status, std::move(body), std::move(transport_error));
    });
    return true;
}

void PaintService::OnTranscribed(int status, std::string body, std::string transport_error) {
    if (!transport_error.empty()) return Fail("语音识别失败: 网络错误 " + transport_error);
    std::string text, error;
    if (!ParseResponse(body, &text, &error)) {
        if (status == 401) error = "API Key 无效 (401)";
        else if (status != 200 && (error == "没有收到回答" || error == "响应格式错误"))
            error = "请求失败 (HTTP " + std::to_string(status) + ")";
        return Fail("语音识别失败: " + error);
    }
    const std::string subject = NormalizeSubject(text);
    if (!IsUsableTranscript(subject)) return Fail("没听清，请再说一次");
    StartPlan(subject);
}

bool PaintService::Draw(const std::string& raw_subject, std::string* error) {
    const std::string subject = NormalizeSubject(raw_subject);
    if (subject.empty()) {
        *error = "没有要画的内容";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!BeginLocked(Phase::Planning, error)) return false;
    }
    Notify();
    StartPlan(subject);
    return true;
}

bool PaintService::Regenerate(std::string* error) {
    std::string subject;
    {
        std::lock_guard<std::mutex> lock(mu_);
        subject = status_.subject;
    }
    if (subject.empty()) {
        *error = "还没有画过，请先按 OK 说出内容";
        return false;
    }
    return Draw(subject, error);
}

void PaintService::StartPlan(const std::string& subject) {
    ChatPlatform* platform;
    ChatConfig config;
    std::string key;
    {
        std::lock_guard<std::mutex> lock(mu_);
        status_.phase = Phase::Planning;
        platform = platform_.get();
        config = chat_config_;
        key = api_key_;
    }
    ChatPlatform::HttpRequest request;
    request.url = config.url;
    request.headers = {{"Content-Type", "application/json"}, {"Authorization", "Bearer " + key}};
    request.body = BuildPlanRequestJson(config, subject);
    platform->Send(std::move(request), [this, subject](int status, std::string body, std::string transport_error) {
        OnPlanned(subject, status, std::move(body), std::move(transport_error));
    });
}

// The planner only improves the prompt: if it fails for any reason the picture is still painted from the subject alone.
void PaintService::OnPlanned(const std::string& subject, int status, std::string body, std::string transport_error) {
    std::string raw, error, description;
    if (transport_error.empty() && status == 200 && ParseResponse(body, &raw, &error) && CleanPlan(raw, &description)) {
        StartGenerate(subject, ImagePrompt(description));
    } else {
        StartGenerate(subject, FallbackPrompt(subject));
    }
}

void PaintService::StartGenerate(const std::string& subject, const std::string& prompt) {
    ChatPlatform* platform;
    PaintConfig config;
    std::string key;
    {
        std::lock_guard<std::mutex> lock(mu_);
        status_.phase = Phase::Generating;
        platform = platform_.get();
        config = paint_config_;
        key = api_key_;
    }
    ChatPlatform::HttpRequest request;
    request.url = config.url;
    request.headers = {{"Content-Type", "application/json"}, {"Authorization", "Bearer " + key}};
    request.body = BuildImageRequestJson(config, prompt);
    (void)platform;
    // A handshake that the server drops (e.g. mbedtls -0x7280 on a flaky link) is worth one more try.
    SendWithRetry(std::move(request), 1, [this, subject](int status, std::string body, std::string transport_error) {
        OnGenerated(subject, status, std::move(body), std::move(transport_error));
    });
}

void PaintService::SendWithRetry(ChatPlatform::HttpRequest request, int retries, ChatPlatform::DoneCallback done) {
    ChatPlatform* platform;
    int delay_ms;
    {
        std::lock_guard<std::mutex> lock(mu_);
        platform = platform_.get();
        delay_ms = retry_delay_ms_;
    }
    ChatPlatform::HttpRequest again = request;  // kept for the retry (small: a JSON body or just a URL)
    platform->Send(std::move(request), [this, again = std::move(again), retries, done = std::move(done), delay_ms](
                                           int status, std::string body, std::string transport_error) mutable {
        if (!transport_error.empty() && retries > 0) {
            // Runs on the platform's request task, never the UI: sleeping here is harmless.
            if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            SendWithRetry(std::move(again), retries - 1, std::move(done));
            return;
        }
        done(status, std::move(body), std::move(transport_error));
    });
}

void PaintService::SetRetryDelayMs(int ms) {
    std::lock_guard<std::mutex> lock(mu_);
    retry_delay_ms_ = ms;
}

void PaintService::OnGenerated(const std::string& subject, int status, std::string body, std::string transport_error) {
    if (!transport_error.empty()) return Fail("生成失败: 网络错误 " + transport_error);
    std::string url, error;
    if (!ParseImageResponse(body, &url, &error)) {
        if (status == 401) error = "API Key 无效 (401)";
        else if (status != 200 && (error == "没有收到图片" || error == "响应格式错误"))
            error = "请求失败 (HTTP " + std::to_string(status) + ")";
        return Fail("生成失败: " + FriendlyError(error));
    }
    body = std::string();  // the download below needs the memory
    StartDownload(subject, url, false);
}

// The pre-signed URL is the only thing that grants access and the picture is not secret, so when the TLS connection to the
// storage host fails the same URL is fetched over plain HTTP (no handshake at all). HTTPS is not retried first: on the device
// the failure was "No matching trusted root certificate found", which is the same every time (a retry only costs seconds).
void PaintService::StartDownload(const std::string& subject, const std::string& url, bool plain_http) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        status_.phase = Phase::Downloading;
    }
    ChatPlatform::HttpRequest request;  // no Authorization header
    request.method = "GET";
    request.url = url;
    request.max_response = kMaxPngBytes;
    SendWithRetry(std::move(request), plain_http ? 1 : 0,  // only a plain-HTTP network hiccup is worth a retry
                  [this, subject, url, plain_http](int status, std::string png, std::string transport_error) {
                      if (!transport_error.empty() && !plain_http && url.rfind("https://", 0) == 0) {
                          StartDownload(subject, "http://" + url.substr(8), true);
                          return;
                      }
                      OnDownloaded(subject, status, std::move(png), std::move(transport_error));
                  });
}

void PaintService::OnDownloaded(const std::string& subject, int status, std::string body, std::string transport_error) {
    if (!transport_error.empty()) return Fail("下载图片失败: " + transport_error);
    if (status != 200) return Fail("下载图片失败 (HTTP " + std::to_string(status) + ")");
    SetPhase(Phase::Converting);
    std::vector<uint8_t> image;
    std::string error;
    if (!PngToGray16(body, &image, &error)) return Fail(error);
    body = std::string();
    {
        std::lock_guard<std::mutex> lock(mu_);
        image_ = std::move(image);
        busy_ = false;
        status_.working = false;
        status_.phase = Phase::Ready;
        status_.subject = subject;
        status_.has_image = true;
        status_.error.clear();
        ++status_.revision;
    }
    Notify();
}

Status PaintService::GetStatus() {
    std::lock_guard<std::mutex> lock(mu_);
    return status_;
}

bool PaintService::CopyImage(std::vector<uint8_t>* out) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!status_.has_image) return false;
    *out = image_;
    return true;
}

}  // namespace paint
}  // namespace ai
