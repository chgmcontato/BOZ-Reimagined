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
# Release version, e.g. `make VERSION=0.5.2` (the tag is v0.5.2). Only builds made with a
# VERSION update themselves from GitHub releases (src/updater.c); without it the build is
# "dev" and never replaces itself.
VERSION ?=

ifeq ($(PLATFORM),windows)
SDL_CFLAGS := -I$(WIN_SDL)/include/SDL2 -I$(WIN_SDL)/include
SDL_LIBS := -L$(WIN_SDL)/lib -lmingw32 -lSDL2main -lSDL2 -lopengl32 -lws2_32 -liphlpapi \
  -lcomdlg32 -lole32 -loleaut32 -lshell32 -lsetupapi -limm32 -lversion -lwinmm -lgdi32 \
  -luuid -lcfgmgr32 -lwinhttp -mwindows
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
  src/aim_probe.c \
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
  src/updater.c \
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

# The version is compiled into updater.o only; the stamp rebuilds it when VERSION changes.
$(BUILD_DIR)/version.stamp: FORCE
	@mkdir -p $(dir $@)
	@echo '$(VERSION)' | cmp -s - $@ || echo '$(VERSION)' > $@
$(BUILD_DIR)/src/updater.o: $(BUILD_DIR)/version.stamp
$(BUILD_DIR)/src/updater.o: PROJECT_CFLAGS += -DCODBOZ_VERSION='"$(VERSION)"'

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

# Release archives. Their names and layout are what the updater downloads and unpacks, so
# keep them: codboz-pc-windows-x64.zip with codboz-pc.exe, codboz-pc-linux-x86_64.tar.gz
# with codboz-pc/codboz-pc, plus SHA256SUMS.txt (sha256sum format) in the same release.
# The license notices travel next to the executable; the updater only takes the executable
# (it looks the entry up by name).
NOTICES := LICENSE LICENSE-cod-boz-port LICENSE-boz-redux THIRD-PARTY.md

check-version:
	@case '$(VERSION)' in [0-9]*.[0-9]*) ;; *) echo "make: release builds need VERSION=x.y.z (the release tag without the v)" >&2; exit 1;; esac

# A folder to zip and hand out: the stripped .exe (SDL2 is linked in statically).
win-dist: check-version all
	mkdir -p $(BUILD_DIR)/dist
	x86_64-w64-mingw32-strip -o $(BUILD_DIR)/dist/codboz-pc.exe $(TARGET)
	cp $(NOTICES) $(BUILD_DIR)/dist/
	cd $(BUILD_DIR)/dist && rm -f ../codboz-pc-windows-x64.zip && zip -q ../codboz-pc-windows-x64.zip codboz-pc.exe $(NOTICES)

linux-dist: check-version all
	rm -rf $(BUILD_DIR)/dist && mkdir -p $(BUILD_DIR)/dist/codboz-pc
	strip -o $(BUILD_DIR)/dist/codboz-pc/codboz-pc $(TARGET)
	cp $(NOTICES) $(BUILD_DIR)/dist/codboz-pc/
	tar -C $(BUILD_DIR)/dist -czf $(BUILD_DIR)/codboz-pc-linux-x86_64.tar.gz codboz-pc

# ---- Tests (Linux, no game data): `make test`; `make test SANITIZE=1` adds ASan and UBSan. ----
# Most tests #include the source they check and replace some of its neighbours with fakes. The
# ones that need the rest of the runtime link it as an archive with --allow-multiple-definition:
# the test's own definitions come first and win. test_gles_shaders exits 77 (skipped) when no
# OpenGL ES driver is available. test_guest_memory maps the guest space at 0x40000000-0x90000000,
# where ASan keeps its shadow memory, so the sanitizer run leaves it out.
comma := ,
TEST_DIR := $(BUILD_DIR)/tests$(if $(SANITIZE),-san)
TEST_SAN := $(if $(SANITIZE),-fsanitize=address$(comma)undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
TEST_CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -D_7ZIP_ST -Isrc \
  $(SDL_CFLAGS) $(TEST_SAN)
TEST_LIBS := $(SDL_LIBS) -lm -ldl -lpthread
TEST_RUNTIME_OBJ := $(patsubst %.c,$(TEST_DIR)/obj/%.o,$(filter-out src/main.c,$(SRC))) \
  $(TEST_DIR)/obj/src/emu_dynarmic.o
TEST_WITH_RUNTIME := boz_zeroconf coop_pause menu_input menu_pointer_routing
TEST_NAMES := pvrtc timer_cache updater gl_state gles_shaders player_name install_hash \
  $(if $(SANITIZE),,guest_memory) $(TEST_WITH_RUNTIME)
TEST_BINS := $(TEST_NAMES:%=$(TEST_DIR)/test_%)

test: $(TEST_BINS)
	@pass=0; skip=0; fail=0; \
	for t in $(TEST_BINS); do \
	  name=$$(basename $$t); \
	  if ASAN_OPTIONS=$${ASAN_OPTIONS:-detect_leaks=0} $$t > $$t.log 2>&1; then \
	    echo "PASS $$name"; pass=$$((pass + 1)); \
	  elif [ $$? -eq 77 ]; then \
	    echo "SKIP $$name ($$(tail -1 $$t.log))"; skip=$$((skip + 1)); \
	  else \
	    echo "FAIL $$name ($$t.log)"; tail -20 $$t.log; fail=$$((fail + 1)); \
	  fi; \
	done; \
	echo "$$pass passed, $$skip skipped, $$fail failed"; [ $$fail -eq 0 ]

$(TEST_DIR)/obj/src/%.o: src/%.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) -c $< -o $@
$(TEST_DIR)/obj/tools/%.o: tools/%.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) -c $< -o $@
$(TEST_DIR)/obj/third_party/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g -D_7ZIP_ST $(TEST_SAN) -c $< -o $@
$(TEST_DIR)/obj/src/emu_dynarmic.o: src/emu_dynarmic.cpp src/*.h $(DYN_LIBS)
	@mkdir -p $(dir $@)
	$(CXX) -std=c++20 -O2 -g -D_GNU_SOURCE -Isrc -I$(DYN_SRC)/src $(TEST_SAN) -c $< -o $@
$(TEST_DIR)/libruntime.a: $(TEST_RUNTIME_OBJ)
	rm -f $@ && ar rcs $@ $^

$(TEST_DIR)/test_pvrtc: tools/test_pvrtc.c src/pvrtc.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) $(filter %.c,$^) -o $@ -lm
$(TEST_DIR)/test_gl_state: tools/test_gl_state.c src/gl_state.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) $(filter %.c,$^) -o $@
$(TEST_DIR)/test_gles_shaders: tools/test_gles_shaders.c src/gles_shader.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) $(filter %.c,$^) -o $@ -ldl
$(TEST_DIR)/test_timer_cache $(TEST_DIR)/test_player_name: $(TEST_DIR)/test_%: tools/test_%.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) $< -o $@ $(TEST_LIBS)
$(TEST_DIR)/test_updater: tools/test_updater.c src/updater.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) $(filter %.c,$^) -o $@ $(TEST_LIBS)
$(TEST_DIR)/test_install_hash: tools/test_install_hash.c src/updater.c third_party/LzmaDec.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) $(filter %.c,$^) -o $@ $(TEST_LIBS)
$(TEST_DIR)/test_guest_memory: $(TEST_DIR)/obj/tools/test_guest_memory.o \
  $(TEST_DIR)/obj/src/emu_dynarmic.o $(TEST_DIR)/obj/src/gheap.o $(DYN_LIBS)
	$(CXX) -no-pie $(TEST_SAN) -o $@ $(filter %.o,$^) $(DYN_LIBS) -lpthread
$(TEST_WITH_RUNTIME:%=$(TEST_DIR)/test_%): $(TEST_DIR)/test_%: $(TEST_DIR)/obj/tools/test_%.o \
  $(TEST_DIR)/obj/tools/test_globals.o $(TEST_DIR)/libruntime.a $(DYN_LIBS)
	$(CXX) -no-pie $(TEST_SAN) -o $@ $(filter %.o,$^) $(TEST_DIR)/libruntime.a $(DYN_LIBS) \
	  $(TEST_LIBS) -Wl,--allow-multiple-definition

FORCE:
.PHONY: all clean test win-deps win-dist linux-dist check-version FORCE

# Only compiled files: $(BUILD_DIR)/game holds the installed game data and saves.
clean:
	rm -rf $(BUILD_DIR)/src $(BUILD_DIR)/third_party $(BUILD_DIR)/tests $(BUILD_DIR)/tests-san $(TARGET)

.PHONY: all clean
