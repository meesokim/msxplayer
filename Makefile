
CC = gcc
CXX = g++
STRIP = strip

BLUE_MSX_SRC = /home/msx/blueberryMSX/Src

INCLUDES = -I$(BLUE_MSX_SRC)/Common \
           -I$(BLUE_MSX_SRC)/Z80 \
           -I$(BLUE_MSX_SRC)/VideoChips \
           -I$(BLUE_MSX_SRC)/SoundChips \
           -I$(BLUE_MSX_SRC)/Emulator \
           -I$(BLUE_MSX_SRC)/Memory \
           -I$(BLUE_MSX_SRC)/IoDevice \
           -I$(BLUE_MSX_SRC)/Utils \
           -I$(BLUE_MSX_SRC)/Sdl \
           -I$(BLUE_MSX_SRC)/Board \
           -I$(BLUE_MSX_SRC)/Arch \
           -I$(BLUE_MSX_SRC)/Media \
           -I$(BLUE_MSX_SRC)/Bios \
           -I$(BLUE_MSX_SRC)/Debugger \
           -I$(BLUE_MSX_SRC)/Language \
           -I$(BLUE_MSX_SRC)/Pi \
           -I$(BLUE_MSX_SRC)/TinyXML \
           -I$(BLUE_MSX_SRC)/Unzip \
           -I$(BLUE_MSX_SRC)/VideoRender \
           -Isrc

COMMON_FLAGS = -O2 -g -Wall $(INCLUDES) -DPIXEL_WIDTH=16
CFLAGS = $(COMMON_FLAGS)
CXXFLAGS = $(COMMON_FLAGS) -std=c++11

LDFLAGS = -lSDL2 -lz

# Source files from blueMSX
VDP_SRC = $(BLUE_MSX_SRC)/VideoChips/VDP.c
Z80_SRC = $(BLUE_MSX_SRC)/Z80/R800.cpp
PSG_SRC = $(BLUE_MSX_SRC)/SoundChips/AY8910.c
SCC_SRC = $(BLUE_MSX_SRC)/SoundChips/SCC.c
FB_SRC  = $(BLUE_MSX_SRC)/VideoChips/FrameBuffer.c

# Our source files
MY_OBJS = main.o video.o memory.o io.o sound.o stubs.o vram_viewer.o vdp_test.o bios_data.o bios_loader.o hash_util.o mapper_db.o
BLUE_OBJS = VDP.o R800.o AY8910.o SCC.o FrameBuffer.o Zip.o IoApi.o Adler32.o Crc32.o InfFast.o Inflate.o InfTrees.o Zutil.o
OBJS = $(MY_OBJS) $(BLUE_OBJS)

all: msxplay msxplay.exe verify verify-tools

msxplay: $(OBJS)
	$(CXX) -o msxplay $(OBJS) $(LDFLAGS)

verify: src/verify_core.cpp
	$(CXX) -o verify_core src/verify_core.cpp
	./verify_core

verify-tools: src/verify_tools.cpp src/hash_util.cpp src/mapper_db.cpp
	$(CXX) -o verify_tools src/verify_tools.cpp src/hash_util.cpp src/mapper_db.cpp $(CXXFLAGS)

main_verify.o: src/main.cpp
	$(CXX) -c src/main.cpp -o main_verify.o $(CXXFLAGS) -DVERIFY_CORE

verify_core.o: src/verify_core.cpp
	$(CXX) -c src/verify_core.cpp -o verify_core.o $(CXXFLAGS)
%.o: src/%.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

VDP.o: $(VDP_SRC)
	$(CC) -c $< -o $@ $(CFLAGS)

R800.o: $(Z80_SRC)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

AY8910.o: $(PSG_SRC)
	$(CC) -c $< -o $@ $(CFLAGS)

SCC.o: $(SCC_SRC)
	$(CC) -c $< -o $@ $(CFLAGS)

FrameBuffer.o: $(FB_SRC)
	$(CC) -c $< -o $@ $(CFLAGS)

Zip.o: $(BLUE_MSX_SRC)/Unzip/unzip.c
	$(CC) -c $< -o $@ $(CFLAGS)

IoApi.o: $(BLUE_MSX_SRC)/Unzip/ioapi.c
	$(CC) -c $< -o $@ $(CFLAGS)

Adler32.o: $(BLUE_MSX_SRC)/Unzip/adler32.c
	$(CC) -c $< -o $@ $(CFLAGS)
Crc32.o: $(BLUE_MSX_SRC)/Unzip/crc32.c
	$(CC) -c $< -o $@ $(CFLAGS)
InfFast.o: $(BLUE_MSX_SRC)/Unzip/inffast.c
	$(CC) -c $< -o $@ $(CFLAGS)
Inflate.o: $(BLUE_MSX_SRC)/Unzip/inflate.c
	$(CC) -c $< -o $@ $(CFLAGS)
InfTrees.o: $(BLUE_MSX_SRC)/Unzip/inftrees.c
	$(CC) -c $< -o $@ $(CFLAGS)

Zutil.o: $(BLUE_MSX_SRC)/Unzip/zutil.c
	$(CC) -c $< -o $@ $(CFLAGS)
LDFLAGS = -lSDL2 -lSDL2_ttf -lz -lGL

# ... (middle parts unchanged)

# Windows cross-compilation
WIN_CC = x86_64-w64-mingw32-gcc
WIN_CXX = x86_64-w64-mingw32-g++
LOCAL_WIN ?= $(CURDIR)/deps/local_win
MSXPLAY_DEPS = $(CURDIR)/deps
SDL2_MINGW_VER = 2.30.9
SDL2_MINGW_URL = https://github.com/libsdl-org/SDL/releases/download/release-$(SDL2_MINGW_VER)/SDL2-devel-$(SDL2_MINGW_VER)-mingw.tar.gz
SDL2_TTF_MINGW_VER = 2.24.0
SDL2_TTF_MINGW_URL = https://github.com/libsdl-org/SDL_ttf/releases/download/release-$(SDL2_TTF_MINGW_VER)/SDL2_ttf-devel-$(SDL2_TTF_MINGW_VER)-mingw.tar.gz
# MSYS2 MinGW64: FreeType import lib (libfreetype-6.dll at runtime) required to link static libSDL2_ttf.a
FREETYPE_MSYS2_PKG = mingw-w64-x86_64-freetype-2.13.3-1-any.pkg.tar.zst
FREETYPE_MSYS2_URL = https://mirror.msys2.org/mingw/mingw64/$(FREETYPE_MSYS2_PKG)
WIN_MINGW_STAMP = $(LOCAL_WIN)/.stamp-mingw-deps

WIN_INCLUDES = $(INCLUDES) -I$(LOCAL_WIN)/include
WIN_COMMON_FLAGS = -O2 -Wall $(WIN_INCLUDES) -DPIXEL_WIDTH=16 -fpermissive -DSDL_MAIN_HANDLED
WIN_CFLAGS = $(WIN_COMMON_FLAGS)
WIN_CXXFLAGS = $(WIN_COMMON_FLAGS) -std=c++11
# SDL2_ttf: static archive; FreeType via MSYS2 import lib (ship deps/local_win/bin/libfreetype-6.dll, not SDL2_ttf.dll).
WIN_LDFLAGS = -L$(LOCAL_WIN)/lib -static -lmingw32 -lSDL2main -lSDL2 \
	$(LOCAL_WIN)/lib/libSDL2_ttf.a $(LOCAL_WIN)/lib/libfreetype.dll.a \
	-mconsole -lopengl32 -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 \
	-lversion -luuid -ladvapi32 -lsetupapi -lshell32 -lrpcrt4 -lusp10

# Fetch MinGW SDL2 + SDL2_ttf into LOCAL_WIN if headers/libs are missing (needs curl or wget).
# If libs are missing but this stamp exists, run: rm -f $(WIN_MINGW_STAMP)
$(WIN_MINGW_STAMP):
	@mkdir -p "$(LOCAL_WIN)/include/SDL2" "$(LOCAL_WIN)/lib" "$(LOCAL_WIN)/bin"
	@if [ ! -f "$(LOCAL_WIN)/lib/libSDL2.a" ] || [ ! -f "$(LOCAL_WIN)/lib/libSDL2main.a" ]; then \
		echo "msxplay: fetching SDL2 $(SDL2_MINGW_VER) MinGW development bundle..."; \
		set -e; cd "$(MSXPLAY_DEPS)" && rm -rf .fetch-sdl2-mingw && mkdir .fetch-sdl2-mingw && cd .fetch-sdl2-mingw; \
		if command -v curl >/dev/null 2>&1; then curl -fsSL -o sdl2.tgz "$(SDL2_MINGW_URL)"; \
		elif command -v wget >/dev/null 2>&1; then wget -q -O sdl2.tgz "$(SDL2_MINGW_URL)"; \
		else echo "msxplay: need curl or wget to download MinGW SDL2"; exit 1; fi; \
		tar xzf sdl2.tgz; \
		D=$$(ls -d SDL2-* | head -1); test -n "$$D"; \
		cp -r "$$D/x86_64-w64-mingw32/include/SDL2/"* "$(LOCAL_WIN)/include/SDL2/"; \
		cp "$$D/x86_64-w64-mingw32/lib/libSDL2.a" "$$D/x86_64-w64-mingw32/lib/libSDL2main.a" "$(LOCAL_WIN)/lib/"; \
		cp "$$D/x86_64-w64-mingw32/bin/SDL2.dll" "$(LOCAL_WIN)/bin/"; \
		cd .. && rm -rf .fetch-sdl2-mingw; \
	fi
	@if [ ! -f "$(LOCAL_WIN)/include/SDL2/SDL_ttf.h" ] || [ ! -f "$(LOCAL_WIN)/lib/libSDL2_ttf.a" ]; then \
		echo "msxplay: fetching SDL2_ttf $(SDL2_TTF_MINGW_VER) MinGW development bundle..."; \
		set -e; cd "$(MSXPLAY_DEPS)" && rm -rf .fetch-sdl2-ttf-mingw && mkdir .fetch-sdl2-ttf-mingw && cd .fetch-sdl2-ttf-mingw; \
		if command -v curl >/dev/null 2>&1; then curl -fsSL -o sdl2ttf.tgz "$(SDL2_TTF_MINGW_URL)"; \
		elif command -v wget >/dev/null 2>&1; then wget -q -O sdl2ttf.tgz "$(SDL2_TTF_MINGW_URL)"; \
		else echo "msxplay: need curl or wget to download MinGW SDL2_ttf"; exit 1; fi; \
		tar xzf sdl2ttf.tgz; \
		D=$$(ls -d SDL2_ttf-* | head -1); test -n "$$D"; \
		cp "$$D/x86_64-w64-mingw32/include/SDL2/SDL_ttf.h" "$(LOCAL_WIN)/include/SDL2/"; \
		cp "$$D/x86_64-w64-mingw32/lib/libSDL2_ttf.a" "$$D/x86_64-w64-mingw32/lib/libSDL2_ttf.dll.a" "$(LOCAL_WIN)/lib/"; \
		cd .. && rm -rf .fetch-sdl2-ttf-mingw; \
	fi
	@if [ ! -f "$(LOCAL_WIN)/lib/libfreetype.dll.a" ] || [ ! -f "$(LOCAL_WIN)/bin/libfreetype-6.dll" ]; then \
		echo "msxplay: fetching FreeType (MSYS2) for static SDL2_ttf link..."; \
		set -e; cd "$(MSXPLAY_DEPS)" && rm -rf .fetch-freetype-msys2 && mkdir .fetch-freetype-msys2 && cd .fetch-freetype-msys2; \
		if command -v curl >/dev/null 2>&1; then curl -fsSL -o ft.pkg.tar.zst "$(FREETYPE_MSYS2_URL)"; \
		elif command -v wget >/dev/null 2>&1; then wget -q -O ft.pkg.tar.zst "$(FREETYPE_MSYS2_URL)"; \
		else echo "msxplay: need curl or wget to download FreeType"; exit 1; fi; \
		tar -xf ft.pkg.tar.zst; \
		cp mingw64/lib/libfreetype.dll.a mingw64/lib/libfreetype.a "$(LOCAL_WIN)/lib/"; \
		cp mingw64/bin/libfreetype-6.dll "$(LOCAL_WIN)/bin/"; \
		cd .. && rm -rf .fetch-freetype-msys2; \
	fi
	@touch "$(WIN_MINGW_STAMP)"

# All Windows objects need headers/libs present before compile (parallel -j safe).
$(OBJS:%.o=win_%.o): $(WIN_MINGW_STAMP)

win_%.o: src/%.cpp
	$(WIN_CXX) -c $< -o $@ $(WIN_CXXFLAGS)

win_VDP.o: $(VDP_SRC)
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)

win_R800.o: $(Z80_SRC)
	$(WIN_CXX) -c $< -o $@ $(WIN_CXXFLAGS)

win_AY8910.o: $(PSG_SRC)
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)

win_SCC.o: $(SCC_SRC)
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)

win_FrameBuffer.o: $(FB_SRC)
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)

win_Zip.o: $(BLUE_MSX_SRC)/Unzip/unzip.c
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)

win_IoApi.o: $(BLUE_MSX_SRC)/Unzip/ioapi.c
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)

win_Adler32.o: $(BLUE_MSX_SRC)/Unzip/adler32.c
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)
win_Crc32.o: $(BLUE_MSX_SRC)/Unzip/crc32.c
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)
win_InfFast.o: $(BLUE_MSX_SRC)/Unzip/inffast.c
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)
win_Inflate.o: $(BLUE_MSX_SRC)/Unzip/inflate.c
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)
win_InfTrees.o: $(BLUE_MSX_SRC)/Unzip/inftrees.c
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)
win_Zutil.o: $(BLUE_MSX_SRC)/Unzip/zutil.c
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)

msxplay.exe: $(OBJS:%.o=win_%.o)
	$(WIN_CXX) -o msxplay.exe $(OBJS:%.o=win_%.o) $(WIN_LDFLAGS)

clean:
	rm -f msxplay msxplay.exe *.o
