# SporeCoop build (Linux host, llvm-mingw cross toolchain, 32-bit Windows targets)
LLVM_MINGW ?= $(HOME)/.local/opt/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64
CC  := $(LLVM_MINGW)/bin/i686-w64-mingw32-clang
CXX := $(LLVM_MINGW)/bin/i686-w64-mingw32-clang++
HOSTCXX ?= clang++

B := build
COMMON  := -O2 -g -Wall -Wno-unused-function -Wno-missing-braces -D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE
CFLAGS  := $(COMMON)
CXXFLAGS:= $(COMMON) -std=c++20
LDDLL   := -shared -static -Wl,--kill-at -Wl,--enable-stdcall-fixup

MINHOOK := third_party/minhook
MH_SRC  := $(MINHOOK)/src/hook.c $(MINHOOK)/src/buffer.c $(MINHOOK)/src/trampoline.c $(MINHOOK)/src/hde/hde32.c
MH_OBJ  := $(patsubst %.c,$(B)/obj/%.o,$(MH_SRC))

KIT_SRC := $(wildcard sporekit/*.cpp)
KIT_OBJ := $(patsubst %.cpp,$(B)/obj/%.o,$(KIT_SRC))

COOP_SRC := $(wildcard mods/coop/*.cpp) $(wildcard mods/coop/net/*.cpp) $(wildcard updater/*.cpp)
COOP_OBJ := $(patsubst %.cpp,$(B)/obj/%.o,$(COOP_SRC))

.PHONY: all loader sporekit coop res setup clean test
all: loader sporekit coop res setup

# game resources (icons, ...) for SporeCoop: Data/SporeCoop.package
UI_PNG := $(wildcard data/ui/*.png)
res: $(B)/SporeCoop.package
$(B)/SporeCoop.package: tools/mkpackage.py $(UI_PNG)
	@mkdir -p $(B)
	python3 tools/mkpackage.py $@ $(foreach f,$(UI_PNG),sporecoop!$(basename $(notdir $(f))).png=$(f))

loader: $(B)/steam_api.dll
sporekit: $(B)/sporekit.dll
coop: $(B)/coop.dll

$(B)/steam_api.dll: loader/loader.c loader/steam_api.def
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -shared -o $@ loader/loader.c loader/steam_api.def -Wl,--kill-at

$(B)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(MINHOOK)/include -c $< -o $@

$(B)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(MINHOOK)/include -Isdk/include -c $< -o $@ -MMD -MP

$(B)/sporekit.dll: $(KIT_OBJ) $(MH_OBJ)
	$(CXX) $(LDDLL) -o $@ $^ -lws2_32 -luser32 -lshell32 -ld3d9 -Wl,--out-implib,$(B)/libsporekit.a

$(B)/coop.dll: $(COOP_OBJ)
	$(CXX) $(LDDLL) -o $@ $^ -lws2_32 -luser32 -liphlpapi -lwinhttp -lshell32

# SporeCoopSetup.exe: install / update / uninstall (installer/, updater/)
WINDRES := $(LLVM_MINGW)/bin/i686-w64-mingw32-windres
COOP_VERSION = $(shell sed -n 's/^#define COOP_VERSION "\(.*\)"/\1/p' mods/coop/coop.cpp)
setup: $(B)/SporeCoopSetup.exe
$(B)/setup.ico: data/ui/coop_icon.png tools/make_ico.py
	@mkdir -p $(B)
	python3 tools/make_ico.py $< $@
$(B)/obj/installer/setup_rc.o: installer/setup.rc installer/setup.manifest $(B)/setup.ico
	@mkdir -p $(dir $@)
	$(WINDRES) -i installer/setup.rc -o $@ --include-dir installer
$(B)/SporeCoopSetup.exe: installer/setup.cpp updater/winupdate.cpp updater/pack.cpp updater/sha256.cpp $(B)/obj/installer/setup_rc.o
	$(CXX) $(CXXFLAGS) -DSETUP_VERSION='"$(COOP_VERSION)"' -municode -mwindows -static -o $@ $^ \
		-lwinhttp -lcomctl32 -lshell32 -lole32 -ladvapi32 -luser32 -lgdi32

# native Linux unit tests for portable netcode
test:
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -o $(B)/host/net_tests mods/coop/net/*.cpp updater/pack.cpp updater/sha256.cpp tests/net_tests.cpp
	$(B)/host/net_tests
	python3 tools/re_lint.py
	python3 tools/docs_lint.py

clean:
	rm -rf $(B)

-include $(KIT_OBJ:.o=.d) $(COOP_OBJ:.o=.d)
