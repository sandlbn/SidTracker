# Host unit tests: every tests/*_test.cpp is its own binary, linked against the
# portable sources (everything except the device HAL and device entry point).
CXX      ?= g++
ROOT     := ..
OUT      := $(ROOT)/build/host-tests
INCS     := -I$(ROOT)/src -I$(ROOT)/src/platform -I$(ROOT)/src/platform/core -I.
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined \
            -fno-sanitize-recover=undefined $(INCS) -MMD
LDFLAGS  := -fsanitize=address,undefined

PORTABLE := $(shell find $(ROOT)/src/sid $(ROOT)/src/audio $(ROOT)/src/tracker $(ROOT)/src/ui \
              $(ROOT)/src/app $(ROOT)/src/compatibility $(ROOT)/src/c64 $(ROOT)/src/platform/core -name '*.cpp' \
              ! -name 'device_*' 2>/dev/null)
OBJS     := $(patsubst $(ROOT)/src/%.cpp,$(OUT)/obj/%.o,$(PORTABLE))
TESTS    := $(wildcard *_test.cpp)
BINS     := $(patsubst %.cpp,$(OUT)/%,$(TESTS))

all: run

$(OUT)/obj/%.o: $(ROOT)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/libsidt.a: $(OBJS)
	@rm -f $@
	ar rcs $@ $^

$(OUT)/%_test: %_test.cpp $(OUT)/libsidt.a
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(OUT)/libsidt.a $(LDFLAGS) -o $@

build: $(BINS)

run: $(BINS)
	@set -e; for t in $(BINS); do SIDT_ROOT=$(abspath $(ROOT)) $$t; done

# Engine benchmark on the host (-O2, no sanitizers): cycles are host time.
BENCH_OUT   := $(ROOT)/build/bench
BENCH_FLAGS := -std=c++17 -O2 -g $(INCS)
ENGINE_SRCS := $(wildcard $(ROOT)/src/sid/*.cpp) $(wildcard $(ROOT)/src/audio/*.cpp) \
               $(wildcard $(ROOT)/src/tracker/*/*.cpp) $(ROOT)/src/app/engine.cpp $(ROOT)/src/app/midi.cpp
ENGINE_HDRS := $(wildcard $(ROOT)/src/sid/*.h $(ROOT)/src/audio/*.h $(ROOT)/src/tracker/*/*.h $(ROOT)/src/app/*.h)
$(BENCH_OUT)/engine_bench: engine_bench.cpp $(ENGINE_SRCS) $(ENGINE_HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(BENCH_FLAGS) engine_bench.cpp $(ENGINE_SRCS) -o $@

bench: $(BENCH_OUT)/engine_bench
	$<

# Same benchmark built with the device compiler flags for Cortex-M4F and run
# under QEMU mps2-an386 with -icount: executed M4 instructions per block
# (instructions, not cycles: no flash wait states or SDRAM latency).
M4_CXX   := arm-none-eabi-g++
M4_CC    := arm-none-eabi-gcc
M4_FLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O2 -ffunction-sections -fdata-sections
$(BENCH_OUT)/engine_bench_m4.elf: engine_bench.cpp $(ENGINE_SRCS) $(ENGINE_HDRS) bench_m4/startup.c bench_m4/mps2.ld
	@mkdir -p $(dir $@)
	$(M4_CC) $(M4_FLAGS) -c bench_m4/startup.c -o $(BENCH_OUT)/startup_m4.o
	$(M4_CXX) $(M4_FLAGS) -std=gnu++17 -fno-exceptions -fno-rtti -DSIDT_BENCH_M4 $(INCS) \
	    engine_bench.cpp $(ENGINE_SRCS) $(BENCH_OUT)/startup_m4.o -T bench_m4/mps2.ld -nostartfiles \
	    --specs=nano.specs --specs=rdimon.specs -u _printf_float -Wl,--gc-sections -lm -o $@

bench-m4: $(BENCH_OUT)/engine_bench_m4.elf
	qemu-system-arm -M mps2-an386 -nographic -icount shift=0 -semihosting-config enable=on,target=native \
	    -kernel $<

# GoatTracker oracle tools (tools/oracle/run.sh): host only.
ORACLE_OUT  := $(ROOT)/build/oracle
GOAT_SRCS   := $(wildcard $(ROOT)/src/compatibility/goat/*.cpp)
SIDX_SRCS   := $(wildcard $(ROOT)/src/compatibility/sid/*.cpp)
$(ORACLE_OUT)/sidtrace: $(ROOT)/tools/oracle/sidtrace.cpp $(ROOT)/tools/oracle/cpu6502.h $(ROOT)/src/c64/mos6502.h
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -I$(ROOT)/src $< -o $@
$(ORACLE_OUT)/gtcompare: $(ROOT)/tools/oracle/gtcompare.cpp $(ENGINE_SRCS) $(GOAT_SRCS) $(ENGINE_HDRS) $(wildcard $(ROOT)/src/compatibility/goat/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(BENCH_FLAGS) $(ROOT)/tools/oracle/gtcompare.cpp $(ENGINE_SRCS) $(GOAT_SRCS) -o $@
$(ORACLE_OUT)/sidt2sng: $(ROOT)/tools/oracle/sidt2sng.cpp $(ENGINE_SRCS) $(GOAT_SRCS) $(ENGINE_HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(BENCH_FLAGS) $(ROOT)/tools/oracle/sidt2sng.cpp $(ENGINE_SRCS) $(GOAT_SRCS) -o $@
$(ORACLE_OUT)/sng2sidt: $(ROOT)/tools/oracle/sng2sidt.cpp $(ENGINE_SRCS) $(GOAT_SRCS) $(ENGINE_HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(BENCH_FLAGS) $(ROOT)/tools/oracle/sng2sidt.cpp $(ENGINE_SRCS) $(GOAT_SRCS) -o $@
$(ORACLE_OUT)/sidcompare: $(ROOT)/tools/oracle/sidcompare.cpp $(ROOT)/tools/oracle/cpu6502.h $(ROOT)/src/c64/mos6502.h $(ENGINE_SRCS) $(GOAT_SRCS) $(SIDX_SRCS) $(ENGINE_HDRS) $(ROOT)/src/compatibility/sid/player_bin.h
	@mkdir -p $(dir $@)
	$(CXX) $(BENCH_FLAGS) -I$(ROOT)/tools/oracle $(ROOT)/tools/oracle/sidcompare.cpp $(ENGINE_SRCS) $(GOAT_SRCS) $(SIDX_SRCS) -o $@
oracle: $(ORACLE_OUT)/sidtrace $(ORACLE_OUT)/gtcompare $(ORACLE_OUT)/sidt2sng $(ORACLE_OUT)/sng2sidt $(ORACLE_OUT)/sidcompare

clean:
	rm -rf $(OUT) $(BENCH_OUT) $(ORACLE_OUT)

-include $(OBJS:.o=.d) $(BINS:=.d)
.PHONY: all build run bench bench-m4 oracle clean
