# Host simulator for the zos shell + apps. The firmware sources build
# unmodified against the ESP-IDF shims in shim/.
#   make -C sim            build build/sim_device
#   python3 sim/web/server.py   serve the sim on :8000 (device web page :8080)
ROOT   := $(abspath ..)
Z      := $(ROOT)/components/zos
BUILD  := build
CJSON  := $(ROOT)/managed_components/espressif__cjson/cJSON

INC := -Ishim -I$(ROOT)/main -I$(Z)/include -I$(Z) -I$(ROOT)/components/zectrix_demo_ui/include \
       -I$(ROOT)/components/zectrix_demo_ui/font -I$(CJSON) -I.
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wno-unused-function $(INC) -DASSET_DIR=\"$(ROOT)/main/assets\"
CFLAGS   := -O1 $(INC)

CXX_SRCS := $(addprefix $(Z)/,display.cc chrome.cc settings.cc shell.cc web_api.cc) \
            $(ROOT)/components/zectrix_demo_ui/zectrix_canvas.cc \
            $(wildcard $(ROOT)/main/apps/*.cc) $(ROOT)/main/apps/weather/weather.cc \
            $(ROOT)/main/apps/ai/ai_chat.cc $(ROOT)/main/apps/ai/voice_recorder.cc $(ROOT)/main/apps/token/token_board.cc \
            sim_platform.cc sim_assets.cc sim_http.cc sim_ai_platform.cc sim_device.cc
OBJS := $(patsubst %,$(BUILD)/%.o,$(notdir $(CXX_SRCS))) $(BUILD)/cJSON.c.o
vpath %.cc $(sort $(dir $(CXX_SRCS)))

all: $(BUILD)/sim_device
$(BUILD)/sim_device: $(OBJS)
	$(CXX) $^ -o $@ -lpthread
$(BUILD)/%.cc.o: %.cc | $(BUILD)
	@echo CXX $<
	@$(CXX) $(CXXFLAGS) -MMD -c $< -o $@
$(BUILD)/cJSON.c.o: $(CJSON)/cJSON.c | $(BUILD)
	@echo CC $<
	@$(CC) $(CFLAGS) -MMD -c $< -o $@
$(BUILD):
	mkdir -p $@
clean:
	rm -rf $(BUILD)
-include $(wildcard $(BUILD)/*.d)
