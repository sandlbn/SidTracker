# SIDTracker desktop simulator: the portable app + the host HALs in sim/src.
#   make -C sim          -> build/sim/sidtracker-sim
#   make -C sim check    -> headless smoke run: screenshots and a WAV in build/sim/
CXX      ?= g++
ROOT     := ..
OUT      := $(ROOT)/build/sim
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)
CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra -Wno-format-truncation -MMD -I$(ROOT)/src -I$(ROOT)/src/platform \
            -I$(ROOT)/src/platform/core -Isrc $(SDL_CFLAGS)
LDLIBS   := $(SDL_LIBS) -lpthread

PORTABLE := $(shell find $(ROOT)/src/sid $(ROOT)/src/audio $(ROOT)/src/tracker $(ROOT)/src/ui $(ROOT)/src/app \
              $(ROOT)/src/compatibility $(ROOT)/src/platform/core -name '*.cpp' ! -name 'device_*' 2>/dev/null)
CORE_OBJS := $(patsubst $(ROOT)/src/%.cpp,$(OUT)/obj/%.o,$(PORTABLE))
SIM_SRCS  := $(filter-out src/host_midi.cpp src/sim_nvstore.cpp,$(wildcard src/*.cpp))
SIM_OBJS  := $(patsubst src/%.cpp,$(OUT)/obj/sim/%.o,$(SIM_SRCS))
BIN       := $(OUT)/sidtracker-sim

all: $(BIN)

$(OUT)/obj/%.o: $(ROOT)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/obj/sim/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN): $(CORE_OBJS) $(SIM_OBJS)
	$(CXX) $^ $(LDLIBS) -o $@

# Headless smoke test on a scratch SD folder: screenshots of every screen,
# a WAV of the played song, and the saved project.
check: $(BIN)
	rm -rf $(OUT)/check-sd
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy $(BIN) --sd $(OUT)/check-sd \
	    --script scripts/smoke.txt --wav $(OUT)/smoke.wav --screenshot $(OUT)/smoke-end.png
	python3 -I scripts/check_wav.py $(OUT)/smoke.wav
	test -f $(OUT)/check-sd/SIDTracker/Projects/SONG001.sidt
	test -f $(OUT)/check-sd/SIDTracker/GoatTracker/SONG001.sng

clean:
	rm -rf $(OUT)

-include $(CORE_OBJS:.o=.d) $(SIM_OBJS:.o=.d)
.PHONY: all clean check
