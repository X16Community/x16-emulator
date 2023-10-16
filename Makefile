##################################################################################################
#
# COMMANDER X16 EMULATOR MAKEFILE
#
##################################################################################################

ifeq ($(CROSS_COMPILE_WINDOWS),1)
	SDL2CONFIG?=$(WIN_SDL2)/bin/sdl2-config --prefix=$(WIN_SDL2)
else
	SDL2CONFIG=sdl2-config
endif

CFLAGS=-std=c99 -O3 -Wall -Werror -g $(shell $(SDL2CONFIG) --cflags) -Isrc/extern/include
CXXFLAGS=-std=c++17 -O3 -Wall -Werror -Isrc/extern/ymfm/src
LDFLAGS=$(shell $(SDL2CONFIG) --libs) -lm -lz

# build with link time optimization
ifndef NOLTO
	CFLAGS+=-flto
	LDFLAGS+=-flto
endif

X16_ODIR = build/x16emu
X16_SDIR = src

MAKECART_ODIR = build/makecart
MAKECART_SDIR = src

ifdef TRACE
	CFLAGS+=-D TRACE
endif

X16_OUTPUT=x16emu
MAKECART_OUTPUT=makecart

GIT_REV=$(shell git diff --quiet && /bin/echo -n $$(git rev-parse --short=8 HEAD || /bin/echo "00000000") || /bin/echo -n $$( /bin/echo -n $$(git rev-parse --short=7 HEAD || /bin/echo "0000000"); /bin/echo -n '+'))

CFLAGS+=-D GIT_REV='"$(GIT_REV)"'

ifeq ($(MAC_STATIC),1)
	LDFLAGS=$(LIBSDL_FILE) -lm -liconv -lz -Wl,-framework,CoreAudio -Wl,-framework,AudioToolbox -Wl,-framework,ForceFeedback -lobjc -Wl,-framework,CoreVideo -Wl,-framework,Cocoa -Wl,-framework,Carbon -Wl,-framework,IOKit -Wl,-weak_framework,QuartzCore -Wl,-weak_framework,Metal -Wl,-weak_framework,CoreHaptics -Wl,-weak_framework,GameController
endif

ifeq ($(CROSS_COMPILE_WINDOWS),1)
	LDFLAGS+=-L$(MINGW32)/lib
	# this enables printf() to show, but also forces a console window
	LDFLAGS+=-Wl,--subsystem,console
	LDFLAGS+=-static-libstdc++ -static-libgcc
ifeq ($(TARGET_CPU),x86)
	CC=i686-w64-mingw32-gcc
	CXX=i686-w64-mingw32-g++
else
	CC=x86_64-w64-mingw32-gcc
	CXX=x86_64-w64-mingw32-g++
endif
endif

ifdef EMSCRIPTEN
	LDFLAGS+=--shell-file webassembly/x16emu-template.html --preload-file rom.bin -s TOTAL_MEMORY=32MB -s ASSERTIONS=1 -s DISABLE_DEPRECATED_FIND_EVENT_TARGET_BEHAVIOR=1
	# To the Javascript runtime exported functions
	LDFLAGS+=-s EXPORTED_FUNCTIONS='["_j2c_reset", "_j2c_paste", "_j2c_start_audio", _main]' -s EXTRA_EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]' -s USE_ZLIB=1 -s EXIT_RUNTIME=1
	CFLAGS+=-s USE_ZLIB=1
	X16_OUTPUT=x16emu.html
	MAKECART_OUTPUT=makecart.html
endif

_X16_OBJS = cpu/fake6502.o memory.o disasm.o video.o i2c.o smc.o rtc.o via.o serial.o ieee.o vera_spi.o audio.o vera_pcm.o vera_psg.o sdcard.o main.o debugger.o javascript_interface.o joystick.o rendertext.o keyboard.o icon.o timing.o wav_recorder.o testbench.o files.o cartridge.o iso_8859_15.o ymglue.o
_X16_OBJS += extern/ymfm/src/ymfm_opm.o
X16_OBJS = $(patsubst %,$(X16_ODIR)/%,$(_X16_OBJS))
X16_DEPS := $(X16_OBJS:.o=.d)

_MAKECART_OBJS = makecart.o files.o cartridge.o makecart_javascript_interface.o

MAKECART_OBJS = $(patsubst %,$(X16_ODIR)/%,$(_MAKECART_OBJS))
MAKECART_DEPS := $(MAKECART_OBJS:.o=.d)

.PHONY: all clean wasm
all: x16emu makecart

x16emu: $(X16_OBJS)
	$(CXX) -o $(X16_OUTPUT) $(X16_OBJS) $(LDFLAGS)

$(X16_ODIR)/%.o: $(X16_SDIR)/%.c
	@mkdir -p $$(dirname $@)
	$(CC) $(CFLAGS) -c $< -MD -MT $@ -MF $(@:%o=%d) -o $@

$(X16_ODIR)/%.o: $(X16_SDIR)/%.cpp
	@mkdir -p $$(dirname $@)
	$(CXX) $(CXXFLAGS) -c $< -MD -MT $@ -MF $(@:%o=%d) -o $@

makecart: $(MAKECART_OBJS)
	$(CC) -o $(MAKECART_OUTPUT) $(MAKECART_OBJS) $(LDFLAGS)

$(MAKECART_ODIR)/%.o: $(MAKECART_SDIR)/%.c
	@mkdir -p $$(dirname $@)
	$(CC) $(CFLAGS) -c $< -MD -MT $@ -MF $(@:%o=%d) -o $@

cpu/tables.h cpu/mnemonics.h: cpu/buildtables.py cpu/6502.opcodes cpu/65c02.opcodes
	cd cpu && python buildtables.py

# Empty rules so that renames of header files do not trigger a failure to compile
$(X16_SDIR)/%.h:;
$(MAKECART_SDIR)/%.h:;

# WebAssembly/emscripten target
#
# See webassembly/WebAssembly.md
wasm:
	emmake make

clean:
	rm -rf $(X16_ODIR) $(MAKECART_ODIR) x16emu x16emu.exe x16emu.js x16emu.wasm x16emu.data x16emu.worker.js x16emu.html x16emu.html.mem makecart makecart.exe makecart.js makecart.wasm makecart.data makecart.worker.js makecart.html makecart.html.mem

ifeq ($(filter $(MAKECMDGOALS), clean),)
-include $(X16_DEPS)
-include $(MAKECART_DEPS)
endif
