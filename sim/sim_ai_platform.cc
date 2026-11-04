// Simulator side of ai::ChatPlatform / board::Platform: the system curl for real requests, canned answers with
// SIM_AI_FAKE=1 (chat, speech) and SIM_BOARD_FAKE=1 (balances, bills). Keys come from the apps' settings first, then
// the environment (DASHSCOPE_API_KEY, ALIBABA_CLOUD_ACCESS_KEY_ID/_SECRET, DEEPSEEK_API_KEY), then "sim-fake" in fake mode.
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "apps/ai/ai_platform.h"
#include "cJSON.h"
#include "zos/settings.h"

namespace {

bool EnvFlag(const char* name) {
    const char* v = getenv(name);
    return v && *v && *v != '0';
}

std::string CannedChat(const std::string& request) {
    int turns = 0;
    std::string last_question;
    if (cJSON* root = cJSON_Parse(request.c_str())) {
        cJSON* m = nullptr;
        cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(root, "messages")) {
            cJSON* role = cJSON_GetObjectItemCaseSensitive(m, "role");
            cJSON* content = cJSON_GetObjectItemCaseSensitive(m, "content");
            if (!cJSON_IsString(role) || !cJSON_IsString(content) || std::string(role->valuestring) == "system") continue;
            ++turns;
            if (std::string(role->valuestring) == "user") last_question = content->valuestring;
        }
        cJSON_Delete(root);
    }
    const std::string text = "（模拟回答）你问的是：" + last_question + "\n本次共发送 " + std::to_string(turns) +
                             " 条上下文消息。这是一段用来检查换行和翻页的较长回答：电子墨水屏刷新较慢，所以整段回答一次性显示。";
    cJSON* reply = cJSON_CreateObject();
    cJSON* choice = cJSON_CreateObject();
    cJSON_AddStringToObject(cJSON_AddObjectToObject(choice, "message"), "content", text.c_str());
    cJSON_AddItemToArray(cJSON_AddArrayToObject(reply, "choices"), choice);
    char* out = cJSON_PrintUnformatted(reply);
    std::string json = out;
    cJSON_free(out);
    cJSON_Delete(reply);
    return json;
}

std::string CannedTranscript() {
    const char* spoken = getenv("SIM_AI_SPEECH_TEXT");
    cJSON* reply = cJSON_CreateObject();
    cJSON* choice = cJSON_CreateObject();
    cJSON_AddStringToObject(cJSON_AddObjectToObject(choice, "message"), "content", spoken ? spoken : "（模拟语音）今天天气怎么样");
    cJSON_AddItemToArray(cJSON_AddArrayToObject(reply, "choices"), choice);
    char* out = cJSON_PrintUnformatted(reply);
    std::string json = out;
    cJSON_free(out);
    cJSON_Delete(reply);
    return json;
}

// curl reads URL, headers (with the key) and body from files, so secrets never show up in `ps`.
void RealRequest(ai::ChatPlatform::HttpRequest req, const ai::ChatPlatform::DoneCallback& done) {
    if (!req.body_blob.empty() && !ai::BuildBlobBody(&req)) {
        done(0, "", "录音数据为空");
        return;
    }
    char body_path[] = "/tmp/sim_ai_body_XXXXXX";
    char cfg_path[] = "/tmp/sim_ai_cfg_XXXXXX";
    const int body_fd = mkstemp(body_path);
    const int cfg_fd = mkstemp(cfg_path);
    if (body_fd < 0 || cfg_fd < 0) {
        done(0, "", "cannot create temp files");
        return;
    }
    auto write_all = [](int fd, const std::string& s) {
        (void)!write(fd, s.data(), s.size());
        close(fd);
    };
    write_all(body_fd, req.body);
    std::string cfg = "url = \"" + req.url + "\"\n";
    for (const auto& h : req.headers) cfg += "header = \"" + h.first + ": " + h.second + "\"\n";
    if (req.method == "GET") cfg += "request = \"GET\"\n";
    else cfg += std::string("data-binary = \"@") + body_path + "\"\n";
    write_all(cfg_fd, cfg);

    const std::string cmd = std::string("curl -q -sS -m 90 -K ") + cfg_path + " -w '\\n%{http_code}' 2>&1";
    std::string output;
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), p)) > 0) output.append(buf, n);
        pclose(p);
    }
    unlink(body_path);
    unlink(cfg_path);
    const size_t nl = output.rfind('\n');
    const int status = nl == std::string::npos ? 0 : atoi(output.c_str() + nl + 1);
    if (status == 0) done(0, "", output.empty() ? "curl failed" : output.substr(0, 120));
    else done(status, output.substr(0, nl), "");
}

std::string Row(const char* instance_id, double amount) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"ProductCode\":\"sfm\",\"ProductName\":\"大模型服务平台百炼\",\"ProductDetail\":\"百炼大模型推理\","
             "\"InstanceID\":\"%s\",\"PretaxAmount\":%g}", instance_id, amount);
    return buf;
}

std::string Bill(const std::string& rows) {
    return "{\"Code\":\"Success\",\"Message\":\"Successful!\",\"Success\":true,\"Data\":{\"Items\":{\"Item\":[" + rows + "]}}}";
}

std::string QueryParam(const std::string& url, const std::string& key) {
    const size_t pos = url.find(key + "=");
    if (pos == std::string::npos) return "";
    const size_t start = pos + key.size() + 1;
    return url.substr(start, url.find('&', start) - start);
}

std::string CannedBoard(const std::string& url, int balance_calls) {
    if (url.find("api.deepseek.com/user/balance") != std::string::npos) {
        char buf[300];
        snprintf(buf, sizeof(buf),
                 "{\"is_available\":true,\"balance_infos\":[{\"currency\":\"CNY\",\"total_balance\":\"%.2f\","
                 "\"granted_balance\":\"10.00\",\"topped_up_balance\":\"%.2f\"}]}",
                 110.0 - 0.05 * balance_calls, 100.0 - 0.05 * balance_calls);
        return buf;
    }
    if (url.find("Action=QueryAccountBalance") != std::string::npos) {
        const double balance = 1234.50 - 0.05 * balance_calls;
        const int thousands = static_cast<int>(balance) / 1000;
        char buf[200];
        snprintf(buf, sizeof(buf),
                 "{\"Code\":\"200\",\"Message\":\"successful\",\"Success\":true,\"Data\":{\"AvailableAmount\":\"%d,%06.2f\","
                 "\"Currency\":\"CNY\"}}", thousands, balance - thousands * 1000);
        return buf;
    }
    const board::DateParts t = board::BeijingParts(time(nullptr));
    char today[16];
    snprintf(today, sizeof(today), "%04d-%02d-%02d", t.year, t.month, t.day);
    if (QueryParam(url, "BillingDate") == today) {  // today's daily bill lags: only the first rows are in
        return Bill(Row("1;k;qwen-flash;context_0-128k_input_token;;0", 0.0156) + "," +
                    Row("1;k;qwen-flash;context_0-128k_output_token;;0", 0.0062));
    }
    return Bill(Row("1;k;qwen-image-2.0-pro;images;;0", 16.0) + "," + Row("1;k;wan2.2-t2i-flash;images;;0", 0.14) + "," +
                Row("1;k;qwen3-asr-flash;duration;;0", 0.02046) + "," +
                Row("cn-beijing;sfm_plugin;;web-search-turbo", 0.018) + "," +
                Row("1;k;qwen-flash;context_0-128k_input_token;;0", 0.0053454));
}

class SimChatPlatform : public ai::ChatPlatform {
public:
    void Send(HttpRequest request, DoneCallback done) override {
        std::thread([request = std::move(request), done = std::move(done)]() mutable {
            if (!EnvFlag("SIM_AI_FAKE")) {
                RealRequest(std::move(request), done);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (!request.body_blob.empty()) {
                if (!ai::BuildBlobBody(&request)) done(0, "", "录音数据为空");
                else done(200, CannedTranscript(), "");
            } else {
                done(200, CannedChat(request.body), "");
            }
        }).detach();
    }
    std::string LoadApiKey() override {
        const std::string saved = zos::Settings("aichat").GetString("key", "");
        if (!saved.empty()) return saved;
        const char* env = getenv("DASHSCOPE_API_KEY");
        return env ? env : (EnvFlag("SIM_AI_FAKE") ? "sim-fake-key" : "");
    }
    void SaveApiKey(const std::string& key) override { zos::Settings("aichat").SetString("key", key.c_str()); }
};

class SimBoardPlatform : public board::Platform {
public:
    void Send(ai::ChatPlatform::HttpRequest request, ai::ChatPlatform::DoneCallback done) override {
        if (!EnvFlag("SIM_BOARD_FAKE")) {
            http_.Send(std::move(request), std::move(done));
            return;
        }
        const bool is_balance = request.url.find("Action=QueryAccountBalance") != std::string::npos ||
                                request.url.find("/user/balance") != std::string::npos;
        const int calls = is_balance ? ++balance_calls_ : balance_calls_;
        std::thread([url = request.url, calls, done = std::move(done)]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            done(200, CannedBoard(url, calls), "");
        }).detach();
    }
    std::string Load(const std::string& key) override {
        const std::string saved = zos::Settings("token").GetString(key.c_str(), "");
        if (!saved.empty()) return saved;
        const char* env = key == "ak_id"       ? getenv("ALIBABA_CLOUD_ACCESS_KEY_ID")
                          : key == "ak_secret" ? getenv("ALIBABA_CLOUD_ACCESS_KEY_SECRET")
                          : key == "ds_key"    ? getenv("DEEPSEEK_API_KEY")
                                               : nullptr;
        if (env) return env;
        if (EnvFlag("SIM_BOARD_FAKE") && (key == "ak_id" || key == "ak_secret" || key == "ds_key")) return "sim-fake";
        return "";
    }
    void Save(const std::string& key, const std::string& value) override {
        zos::Settings("token").SetString(key.c_str(), value.c_str());
    }

private:
    SimChatPlatform http_;
    int balance_calls_ = 0;
};

}  // namespace

std::unique_ptr<ai::ChatPlatform> NewChatPlatform() { return std::make_unique<SimChatPlatform>(); }
std::unique_ptr<board::Platform> NewBoardPlatform() { return std::make_unique<SimBoardPlatform>(); }

// ---- microphone: nothing to capture on the PC, so Stop() returns one second of silence (the fake speech
// recognition answers with a canned sentence, a real one with an empty transcript).
#include "apps/ai/voice_recorder.h"

namespace {
class SimRecorder : public ai::VoiceRecorder {
public:
    bool Start() override {
        started_ = std::chrono::steady_clock::now();
        recording_ = true;
        return true;
    }
    bool recording() const override {
        return recording_ && elapsed_ms() < ai::kMaxRecordSeconds * 1000;
    }
    int elapsed_ms() const override {
        if (!recording_) return 0;
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - started_).count());
    }
    std::string Stop() override {
        const bool long_enough = recording_ && elapsed_ms() >= 500;
        recording_ = false;
        if (!long_enough) return "";
        const std::vector<int16_t> silence(ai::kRecordSampleRate, 0);
        return ai::BuildWav(silence.data(), silence.size(), ai::kRecordSampleRate);
    }

private:
    bool recording_ = false;
    std::chrono::steady_clock::time_point started_;
};
}  // namespace

namespace ai {
std::unique_ptr<VoiceRecorder> NewVoiceRecorder(ZectrixBoard&) { return std::make_unique<SimRecorder>(); }
}  // namespace ai
