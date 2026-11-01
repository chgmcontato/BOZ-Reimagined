CC ?= cc
PKG_CONFIG ?= pkg-config
# CPU: the Dynarmic A32 JIT (third_party/dynarmic submodule; needs Boost headers).
# The Unicorn backend was removed after tag last-unicorn.
.DEFAULT_GOAL := all
# Windows: `make PLATFORM=windows` cross-builds build-win/codboz-pc.exe with MinGW-w64
# (Dynarmic only). SDL2's MinGW package is unpacked in build-win/deps (see `make win-deps`).
ifeq ($(PLATFORM),windows)
CC := x86_64-w64-mingw32-gcc
CXX := x86_64-w64-mingw32-g++
BUILD_DIR ?= build-win
SDL_VERSION ?= 2.30.12
WIN_SDL ?= $(BUILD_DIR)/deps/SDL2-$(SDL_VERSION)/x86_64-w64-mingw32
TARGET ?= $(BUILD_DIR)/codboz-pc.exe
endif
BUILD_DIR ?= build
TARGET ?= $(BUILD_DIR)/codboz-pc

ifeq ($(PLATFORM),windows)
SDL_CFLAGS := -I$(WIN_SDL)/include/SDL2 -I$(WIN_SDL)/include
SDL_LIBS := -L$(WIN_SDL)/lib -lmingw32 -lSDL2main -lSDL2 -lopengl32 -lws2_32 -liphlpapi \
  -lcomdlg32 -lole32 -loleaut32 -lshell32 -lsetupapi -limm32 -lversion -lwinmm -lgdi32 \
  -luuid -lcfgmgr32 -mwindows
else
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2)
endif

CFLAGS ?= -O3 -g -march=x86-64-v2 -flto=auto
LTO_LDFLAGS := -flto=auto -O3
PROJECT_CFLAGS := -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE
# Guest pointers are identity-mapped into the low 4 GiB, so keep the host image there too.
LDFLAGS ?=
PROJECT_LDFLAGS := -no-pie
DYN_CMAKE_SYSTEM :=
ifeq ($(PLATFORM),windows)
# Same as -no-pie on Linux: the image at a fixed low address, no high-entropy ASLR.
PROJECT_LDFLAGS := -Wl,--image-base=0x400000 -Wl,--disable-dynamicbase \
  -Wl,--disable-high-entropy-va -static-libgcc -static-libstdc++ -static
DYN_CMAKE_SYSTEM := -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64 -G "Unix Makefiles"
endif

SRC := \
  src/assets.c \
  src/audio.c \
  src/bindings.c \
  src/config.c \
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
  src/coop_stats.c \
  third_party/cJSON.c \
  third_party/LzmaDec.c
ifeq ($(PLATFORM),windows)
SRC := $(patsubst src/zeroconf_platform_posix.c,src/zeroconf_platform_win32.c,$(SRC))
endif
OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o)

CXX ?= c++
CMAKE ?= cmake
# Boost headers (header-only use). Override with BOOST_INCLUDE_DIR=... if yours live elsewhere.
BOOST_INCLUDE_DIR ?= $(HOME)/Documents/Codex/2026-11-01/o-p/work/experimental-dynarmic/vendor/boost/include
DYN_SRC := third_party/dynarmic
DYN_BUILD := $(BUILD_DIR)/dynarmic
DYN_LIBS := $(DYN_BUILD)/src/dynarmic/libdynarmic.a $(DYN_BUILD)/externals/mcl/src/libmcl.a \
  $(DYN_BUILD)/externals/fmt/libfmt.a $(DYN_BUILD)/externals/zydis/libZydis.a \
  $(DYN_BUILD)/externals/zydis/zycore/libZycore.a
OBJ += $(BUILD_DIR)/src/emu_dynarmic.o
LINK := $(CXX)

$(DYN_LIBS) &:
	$(CMAKE) -S $(DYN_SRC) -B $(DYN_BUILD) $(DYN_CMAKE_SYSTEM) -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_C_COMPILER=$(CC) -DCMAKE_CXX_COMPILER=$(CXX) -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
	  -DDYNARMIC_FRONTENDS=A32 -DDYNARMIC_TESTS=OFF -DDYNARMIC_USE_BUNDLED_EXTERNALS=ON \
	  -DDYNARMIC_WARNINGS_AS_ERRORS=OFF -DBoost_INCLUDE_DIR=$(BOOST_INCLUDE_DIR)
	$(CMAKE) --build $(DYN_BUILD) --target dynarmic -j$$(nproc)

$(BUILD_DIR)/src/emu_dynarmic.o: src/emu_dynarmic.cpp src/*.h $(DYN_LIBS)
	@mkdir -p $(dir $@)
	$(CXX) -std=c++20 -O3 -g -march=x86-64-v2 -flto=auto -Wall -Wextra -D_GNU_SOURCE \
	  -Isrc -I$(DYN_SRC)/src -c $< -o $@

all: $(TARGET)

src/gl_thunks.c: tools/gen_gl.py
	python3 -I tools/gen_gl.py $@

$(BUILD_DIR)/%.o: %.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(PROJECT_CFLAGS) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/third_party/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -D_7ZIP_ST -c $< -o $@

$(TARGET): $(OBJ) $(DYN_LIBS)
	@if [ -f $@ ]; then cp -f $@ $@.bak; fi # exactly one backup: the previous build
	$(LINK) $(PROJECT_LDFLAGS) $(LTO_LDFLAGS) -march=x86-64-v2 $(LDFLAGS) -o $@ $(OBJ) $(DYN_LIBS) $(SDL_LIBS) -lm

# SDL2's official MinGW development package, for PLATFORM=windows.
win-deps:
	mkdir -p build-win/deps
	curl -sSLf -o build-win/deps/sdl2.tar.gz \
	  https://github.com/libsdl-org/SDL/releases/download/release-$(or $(SDL_VERSION),2.30.12)/SDL2-devel-$(or $(SDL_VERSION),2.30.12)-mingw.tar.gz
	tar -C build-win/deps -xzf build-win/deps/sdl2.tar.gz

# A folder to zip and hand out: the stripped .exe (SDL2 is linked in statically).
win-dist: all
	mkdir -p $(BUILD_DIR)/dist
	x86_64-w64-mingw32-strip -o $(BUILD_DIR)/dist/codboz-pc.exe $(TARGET)
	cd $(BUILD_DIR)/dist && rm -f ../codboz-pc-windows.zip && zip -q ../codboz-pc-windows.zip codboz-pc.exe

.PHONY: all clean win-deps win-dist

# Only compiled files: $(BUILD_DIR)/game holds the installed game data and saves.
clean:
	rm -rf $(BUILD_DIR)/src $(BUILD_DIR)/third_party $(TARGET)

.PHONY: all clean
