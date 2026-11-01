CC ?= cc
PKG_CONFIG ?= pkg-config
BUILD_DIR ?= build
TARGET ?= $(BUILD_DIR)/codboz-pc

SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2)
UC_CFLAGS := $(shell $(PKG_CONFIG) --cflags unicorn)
UC_LIBS := $(shell $(PKG_CONFIG) --libs unicorn)

CFLAGS ?= -O3 -g -march=x86-64-v2 -flto=auto
LTO_LDFLAGS := -flto=auto -O3
PROJECT_CFLAGS := -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE
# Guest pointers are identity-mapped into the low 4 GiB, so keep the host image there too.
LDFLAGS ?=
PROJECT_LDFLAGS := -no-pie -Wl,-rpath,$(shell $(PKG_CONFIG) --variable=libdir unicorn)

SRC := \
  src/assets.c \
  src/audio.c \
  src/bindings.c \
  src/config.c \
  src/emu.c \
  src/file.c \
  src/gheap.c \
  src/gl.c \
  src/gl_state.c \
  src/gl_thunks.c \
  src/imports.c \
  src/input.c \
  src/install.c \
  src/launcher.c \
  src/loader.c \
  src/main.c \
  src/mdns_wire.c \
  src/memory.c \
  src/menus.c \
  src/overlay.c \
  src/pvrtc.c \
  src/reimagined.c \
  src/runtime.c \
  src/screens.c \
  src/socket.c \
  src/timer.c \
  src/ui.c \
  src/zeroconf.c \
  src/zeroconf_platform_posix.c \
  third_party/cJSON.c \
  third_party/LzmaDec.c
OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o)

all: $(TARGET)

src/gl_thunks.c: tools/gen_gl.py
	python3 -I tools/gen_gl.py $@

$(BUILD_DIR)/%.o: %.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(PROJECT_CFLAGS) $(CFLAGS) $(SDL_CFLAGS) $(UC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/third_party/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -D_7ZIP_ST -c $< -o $@

$(TARGET): $(OBJ)
	$(CC) $(PROJECT_LDFLAGS) $(LTO_LDFLAGS) -march=x86-64-v2 $(LDFLAGS) -o $@ $^ $(SDL_LIBS) $(UC_LIBS) -lm

# Only compiled files: $(BUILD_DIR)/game holds the installed game data and saves.
clean:
	rm -rf $(BUILD_DIR)/src $(BUILD_DIR)/third_party $(TARGET)

.PHONY: all clean
