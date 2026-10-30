# Terminator - build with:  make   |  make debug  |  make test  |  make clean
# Requires: gcc, make, pkg-config, libx11-dev   (see scripts/setup_ubuntu.sh)

CC        ?= cc
OPT       ?= -O2 -g
STD       := -std=gnu11 -D_GNU_SOURCE
WARN      := -Wall -Wextra -Wformat=2 -Wno-unused-parameter
CFLAGS    := $(OPT) $(STD) $(WARN) -Iinclude -MMD -MP $(shell pkg-config --cflags x11 2>/dev/null) $(EXTRA_CFLAGS)
LDLIBS    := $(shell pkg-config --libs x11 2>/dev/null || echo -lX11) -lpthread
LDFLAGS   += $(EXTRA_LDFLAGS)
BUILD     ?= build
TARGET    ?= terminator

CORE_SRC  := src/config.c src/headless.c \
             $(wildcard src/utils/*.c src/concurrency/*.c src/process/*.c src/shell/*.c src/features/*.c)
GUI_SRC   := $(wildcard src/gui/*.c)
MAIN_SRC  := src/main.c
CORE_OBJ  := $(CORE_SRC:%.c=$(BUILD)/%.o)
GUI_OBJ   := $(GUI_SRC:%.c=$(BUILD)/%.o)
MAIN_OBJ  := $(MAIN_SRC:%.c=$(BUILD)/%.o)

UNIT_SRC  := $(wildcard tests/unit/test_*.c)
UNIT_BIN  := $(UNIT_SRC:tests/unit/%.c=$(BUILD)/tests/%)

all: $(TARGET)

$(TARGET): $(MAIN_OBJ) $(GUI_OBJ) $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ---- tests ----
$(BUILD)/tests/%: tests/unit/%.c $(CORE_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Itests/unit $< $(CORE_OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

unit: $(UNIT_BIN)
	@set -e; for t in $(UNIT_BIN); do echo "== $$t"; $$t; done

test: terminator $(UNIT_BIN)
	@./scripts/run_tests.sh

# ---- sanitizer build: AddressSanitizer + UBSan, separate objects and binary (./terminator-debug) ----
debug:
	$(MAKE) TARGET=terminator-debug BUILD=build-debug OPT="-O0 -g3 -fno-omit-frame-pointer" \
	    EXTRA_CFLAGS="-fsanitize=address,undefined" EXTRA_LDFLAGS="-fsanitize=address,undefined" terminator-debug

clean:
	rm -rf build build-debug terminator terminator-debug

.PHONY: all unit test debug clean
-include $(CORE_OBJ:.o=.d) $(GUI_OBJ:.o=.d) $(MAIN_OBJ:.o=.d)
