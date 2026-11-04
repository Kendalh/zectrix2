#ifndef APPS_AI_AI_PLATFORM_H_
#define APPS_AI_AI_PLATFORM_H_

#include <memory>

#include "apps/ai/ai_chat.h"
#include "apps/token/token_board.h"

/**
 * HTTPS + settings behind ai::ChatPlatform and board::Platform. The firmware implements them in
 * ai_platform_esp.cc (esp_http_client, one short task per request), the simulator in sim/sim_ai_platform.cc
 * (system curl). The keys live in the apps' settings: "aichat"/key and "token"/ak_id, ak_secret, ds_key, ...
 */
std::unique_ptr<ai::ChatPlatform> NewChatPlatform();
std::unique_ptr<board::Platform> NewBoardPlatform();

#endif  // APPS_AI_AI_PLATFORM_H_
