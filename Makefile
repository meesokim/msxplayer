
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

COMMON_FLAGS = -O2 -g -Wall $(INCLUDES) -DPIXEL_WIDTH=16 -DVIDEO_COLOR_TYPE_RGB565
CFLAGS = $(COMMON_FLAGS)
CXXFLAGS = $(COMMON_FLAGS) -std=c++11

LDFLAGS = -lSDL2 -lz

# Source files from blueMSX
VDP_SRC = $(BLUE_MSX_SRC)/VideoChips/VDP.c
Z80_SRC = $(BLUE_MSX_SRC)/Z80/R800.cpp
PSG_SRC = $(BLUE_MSX_SRC)/SoundChips/AY8910.c
FB_SRC  = $(BLUE_MSX_SRC)/VideoChips/FrameBuffer.c

# Our source files
MY_OBJS = main.o video.o memory.o io.o sound.o stubs.o vram_viewer.o vdp_test.o bios_data.o
BLUE_OBJS = VDP.o R800.o AY8910.o FrameBuffer.o Zip.o IoApi.o Adler32.o Crc32.o InfFast.o Inflate.o InfTrees.o Zutil.o
OBJS = $(MY_OBJS) $(BLUE_OBJS)

all: msxplay msxplay.exe

msxplay: $(OBJS)
	$(CXX) -o msxplay $(OBJS) $(LDFLAGS)

%.o: src/%.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

VDP.o: $(VDP_SRC)
	$(CC) -c $< -o $@ $(CFLAGS)

R800.o: $(Z80_SRC)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

AY8910.o: $(PSG_SRC)
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
LDFLAGS = -lSDL2 -lz -lGL

# ... (middle parts unchanged)

# Windows cross-compilation
WIN_CC = x86_64-w64-mingw32-gcc
WIN_CXX = x86_64-w64-mingw32-g++
LOCAL_WIN = /home/msx/msxplay/deps/local_win
WIN_INCLUDES = $(INCLUDES) -I$(LOCAL_WIN)/include
WIN_COMMON_FLAGS = -O2 -Wall $(WIN_INCLUDES) -DPIXEL_WIDTH=16 -DVIDEO_COLOR_TYPE_RGB565 -fpermissive -DSDL_MAIN_HANDLED
WIN_CFLAGS = $(WIN_COMMON_FLAGS)
WIN_CXXFLAGS = $(WIN_COMMON_FLAGS) -std=c++11
WIN_LDFLAGS = -L$(LOCAL_WIN)/lib -static -lmingw32 -lSDL2main -lSDL2 -mconsole -lopengl32 -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32

win_%.o: src/%.cpp
	$(WIN_CXX) -c $< -o $@ $(WIN_CXXFLAGS)

win_VDP.o: $(VDP_SRC)
	$(WIN_CC) -c $< -o $@ $(WIN_CFLAGS)

win_R800.o: $(Z80_SRC)
	$(WIN_CXX) -c $< -o $@ $(WIN_CXXFLAGS)

win_AY8910.o: $(PSG_SRC)
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
