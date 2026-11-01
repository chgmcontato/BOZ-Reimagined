CC ?= cc
PKG_CONFIG ?= pkg-config
# CPU backend: dynarmic (default, JIT, needs the third_party/dynarmic submodule and Boost headers)
# or unicorn (`make EMU=unicorn`, builds into build-unicorn/; run it with
# `build-unicorn/codboz-pc --data build/game`).
EMU ?= dynarmic
.DEFAULT_GOAL := all
ifeq ($(EMU),unicorn)
BUILD_DIR ?= build-unicorn
endif
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

ifeq ($(EMU),dynarmic)
CXX ?= c++
CMAKE ?= cmake
# Boost headers (header-only use). Override with BOOST_INCLUDE_DIR=... if yours live elsewhere.
BOOST_INCLUDE_DIR ?= $(HOME)/Documents/Codex/2026-11-01/o-p/work/experimental-dynarmic/vendor/boost/include
DYN_SRC := third_party/dynarmic
DYN_BUILD := $(BUILD_DIR)/dynarmic
DYN_LIBS := $(DYN_BUILD)/src/dynarmic/libdynarmic.a $(DYN_BUILD)/externals/mcl/src/libmcl.a \
  $(DYN_BUILD)/externals/fmt/libfmt.a $(DYN_BUILD)/externals/zydis/libZydis.a \
  $(DYN_BUILD)/externals/zydis/zycore/libZycore.a
UC_CFLAGS :=
UC_LIBS :=
PROJECT_LDFLAGS := -no-pie
OBJ := $(filter-out $(BUILD_DIR)/src/emu.o,$(OBJ)) $(BUILD_DIR)/src/emu_dynarmic.o
LINK := $(CXX)

$(DYN_LIBS) &:
	$(CMAKE) -S $(DYN_SRC) -B $(DYN_BUILD) -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_C_COMPILER=$(CC) -DCMAKE_CXX_COMPILER=$(CXX) -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
	  -DDYNARMIC_FRONTENDS=A32 -DDYNARMIC_TESTS=OFF -DDYNARMIC_USE_BUNDLED_EXTERNALS=ON \
	  -DDYNARMIC_WARNINGS_AS_ERRORS=OFF -DBoost_INCLUDE_DIR=$(BOOST_INCLUDE_DIR)
	$(CMAKE) --build $(DYN_BUILD) --target dynarmic -j$$(nproc)

$(BUILD_DIR)/src/emu_dynarmic.o: src/emu_dynarmic.cpp src/*.h $(DYN_LIBS)
	@mkdir -p $(dir $@)
	$(CXX) -std=c++20 -O3 -g -march=x86-64-v2 -flto=auto -Wall -Wextra -D_GNU_SOURCE \
	  -Isrc -I$(DYN_SRC)/src -c $< -o $@
else
LINK := $(CC)
DYN_LIBS :=
endif

all: $(TARGET)

src/gl_thunks.c: tools/gen_gl.py
	python3 -I tools/gen_gl.py $@

$(BUILD_DIR)/%.o: %.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(PROJECT_CFLAGS) $(CFLAGS) $(SDL_CFLAGS) $(UC_CFLAGS) -c $< -o $@

$(BUILD_DIR)/third_party/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -D_7ZIP_ST -c $< -o $@

$(TARGET): $(OBJ) $(DYN_LIBS)
	$(LINK) $(PROJECT_LDFLAGS) $(LTO_LDFLAGS) -march=x86-64-v2 $(LDFLAGS) -o $@ $(OBJ) $(DYN_LIBS) $(SDL_LIBS) $(UC_LIBS) -lm

# Only compiled files: $(BUILD_DIR)/game holds the installed game data and saves.
clean:
	rm -rf $(BUILD_DIR)/src $(BUILD_DIR)/third_party $(TARGET)

.PHONY: all clean
