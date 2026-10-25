# SIDTracker firmware for the original Polyend Tracker hardware (MK66FX1M0).
#   make            -> build/sidtracker.hex and build/PolyendTracSIDTRv<ver>.ptf (24 chars)
#   make SELFTEST=1 -> build/selftest/PolyendTracker_STst<N>.ptf: hardware selftest only
#   make host-test  -> host unit tests (ASan/UBSan)
#   make sim        -> desktop simulator (SDL2)
#   make check      -> host tests + simulator smoke run + device image
#   make bench / bench-m4 -> engine benchmark on the host / under QEMU (Cortex-M4)
#   make release    -> build/release/SIDTracker-<ver>.zip (firmware + example songs)
VERSION   ?= 0.9.1
BUILD     := build
DIAG_N    ?= 1
VERNUM    := $(subst .,,$(VERSION))
ifeq ($(SELFTEST),1)
BUILD     := build/selftest
DEFS_EXTRA := -DOT_FORCE_SELFTEST=1
PTF_NAME  := PolyendTracker_STst$(DIAG_N).ptf
else
PTF_NAME  := PolyendTracSIDTRv$(VERNUM).ptf
endif
PTF       := $(BUILD)/$(PTF_NAME)
# The stock update bootloader ignores image names that are not exactly 24
# characters (and then installs a random file); it lists only
# Firmware/PolyendTrac*.ptf (docs/firmware-map.md).
ifneq ($(shell printf '%s' '$(PTF_NAME)' | wc -c | tr -d ' '),24)
$(error image name '$(PTF_NAME)' must be exactly 24 characters (docs/firmware-map.md))
endif
# USB identity (DECISIONS.md 4): pid.codes test PID during development,
# product string "SIDTracker" (src/platform/PROVENANCE.md local change 3).
# RELEASE BLOCKER: register a dedicated pid.codes PID before a public release.
USB_ID    := -DOT_USB_VID=0x1209 -DOT_USB_PID=0x0001 -DSIDT_USB_STRINGS
CORE      := third_party/teensy3
CROSS     ?= arm-none-eabi-
CC        := $(CROSS)gcc
CXX       := $(CROSS)g++
OBJCOPY   := $(CROSS)objcopy
SIZE      := $(CROSS)size

CPUFLAGS  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
DEFS      := -D__MK66FX1M0__ -DF_CPU=180000000 -DUSB_MIDI_SERIAL -DLAYOUT_US_ENGLISH \
             -DF_BUS=90000000 -DSERIAL5_TX_BUFFER_SIZE=512 -DSERIAL5_RX_BUFFER_SIZE=512 \
             -DTEENSYDUINO=159 -DARDUINO=10819 -DOT_VERSION=\"$(VERSION)\" -DOT_TARGET_DEVICE=1 \
             -DSIDT_VERSION=\"$(VERSION)\" $(USB_ID) $(DEFS_EXTRA)
INCS      := -I$(CORE) -Isrc -Isrc/platform -Isrc/platform/core -Isrc/platform/display -Isrc/platform/input \
             -Isrc/platform/audio -Isrc/platform/storage -Isrc/platform/timer -Ithird_party/fatfs
COMMON    := $(CPUFLAGS) $(DEFS) $(INCS) -O2 -g -ffunction-sections -fdata-sections -Wall -MMD
CFLAGS    := $(COMMON) -std=gnu11
CXXFLAGS  := $(COMMON) -std=gnu++17 -fno-exceptions -fno-rtti -felide-constructors -Wno-psabi
LDFLAGS   := $(CPUFLAGS) -O2 -Wl,--gc-sections,--defsym=__rtc_localtime=0 -T$(CORE)/mk66fx1m0.ld \
             --specs=nano.specs
LIBS      := -lm -lstdc++

CORE_C    := $(wildcard $(CORE)/*.c)
CORE_CXX  := $(filter-out $(CORE)/main.cpp,$(wildcard $(CORE)/*.cpp))
APP_C     := third_party/fatfs/ff.c third_party/fatfs/ffunicode.c
APP_CXX   := $(shell find src -name '*.cpp')
OBJS      := $(patsubst %,$(BUILD)/%.o,$(CORE_C) $(CORE_CXX) $(APP_C) $(APP_CXX))

all: $(BUILD)/sidtracker.hex $(PTF)

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/sidtracker.elf: $(OBJS)
	$(CXX) $(LDFLAGS) $^ -L$(CORE) $(LIBS) -o $@ -Wl,-Map=$(BUILD)/sidtracker.map
	$(SIZE) $@

$(BUILD)/sidtracker.hex: $(BUILD)/sidtracker.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

# Every image passes the bootloader checks (tools/check_image.py) before it
# gets its .ptf name.
$(PTF): $(BUILD)/sidtracker.hex
	python3 -I tools/check_image.py $<
	cp $< $@

host-test:
	$(MAKE) -C tests

sim:
	$(MAKE) -C sim

bench:
	$(MAKE) -C tests bench

bench-m4:
	$(MAKE) -C tests bench-m4

check: host-test all sidplayer-check
	$(MAKE) -C sim check
	tools/sync_hal.sh

# The C64 play routine of the .sid export (M9): src/compatibility/sid/player.s
# -> player_bin.h (checked in, so the firmware build needs no host tool).
SIDPLAYER_H := src/compatibility/sid/player_bin.h
build/tools/asm6502: tools/asm6502/asm6502.cpp
	@mkdir -p build/tools
	$(HOSTCXX) -std=c++17 -O2 -Wall -Wextra $< -o $@
sidplayer: build/tools/asm6502
	build/tools/asm6502 src/compatibility/sid/player.s --header $(SIDPLAYER_H) --name kSidPlayer
sidplayer-check: build/tools/asm6502
	@build/tools/asm6502 src/compatibility/sid/player.s --header build/tools/player_bin.h --name kSidPlayer >/dev/null
	@cmp -s build/tools/player_bin.h $(SIDPLAYER_H) || { echo "$(SIDPLAYER_H) is stale: make sidplayer"; exit 1; }

# Original dual-SID demo song (tools/demo/make_demo.cpp) -> build/DUALDEMO.sidt
demo:
	@mkdir -p build
	$(HOSTCXX) -std=c++17 -O2 -Isrc -Isrc/platform -Isrc/platform/core tools/demo/make_demo.cpp \
	    $(wildcard src/tracker/*/*.cpp) -o build/make_demo
	build/make_demo build/DUALDEMO.sidt

# Original example songs (tools/demo/make_examples.cpp) -> build/examples/
examples:
	@mkdir -p build/examples
	$(HOSTCXX) -std=c++17 -O2 -Isrc -Isrc/platform -Isrc/platform/core -Itools/demo tools/demo/make_examples.cpp \
	    $(wildcard src/tracker/*/*.cpp) -o build/make_examples
	build/make_examples build/examples

# Stock instrument library (tools/demo/make_instruments.cpp) -> build/instruments/
instruments:
	rm -rf build/instruments
	$(HOSTCXX) -std=c++17 -O2 -Isrc -Isrc/platform -Isrc/platform/core tools/demo/make_instruments.cpp \
	    $(wildcard src/tracker/*/*.cpp) $(wildcard src/compatibility/goat/*.cpp) -o build/make_instruments
	build/make_instruments build/instruments

# Blast Grid 64 songs from the game's sources (docs/blastgrid-import.md).
GAME ?= $(HOME)/Projects/BlastGridC64
build/tools/text2sidt: tools/text2sidt.cpp $(wildcard src/tracker/*/*.cpp)
	@mkdir -p build/tools
	$(HOSTCXX) -std=c++17 -O2 -Wall -Wextra -Isrc -Isrc/platform -Isrc/platform/core tools/text2sidt.cpp \
	    $(wildcard src/tracker/*/*.cpp) -o $@
blastgrid: build/tools/text2sidt
	python3 -B tools/blastgrid/bg2text.py --game $(GAME) build/blastgrid/txt
	@mkdir -p build/blastgrid/sidt
	for f in build/blastgrid/txt/*.txt; do build/tools/text2sidt $$f build/blastgrid/sidt/$$(basename $$f .txt).sidt; done

# Release zip (tools/release): firmware + example songs and their .sid files.
release: all demo examples instruments
	$(MAKE) -C tests oracle
	tools/release/make_release.sh $(VERSION) $(PTF_NAME)

clean:
	rm -rf build

-include $(OBJS:.o=.d)
# README screenshots from the simulator (sim/scripts/readme_shots.txt).
screenshots: examples
	$(MAKE) -C sim
	rm -rf build/shots-sd && mkdir -p build/shots-sd/SIDTracker/Projects
	cp build/examples/*.sidt build/shots-sd/SIDTracker/Projects/
	cp build/examples/PCMGROOVE.sidt build/shots-sd/SIDTracker/autosave.sidt
	cd sim && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ../build/sim/sidtracker-sim --sd ../build/shots-sd \
	    --script scripts/readme_shots.txt

.PHONY: instruments screenshots all clean host-test sim bench bench-m4 check demo examples blastgrid release sidplayer sidplayer-check
HOSTCXX ?= g++
