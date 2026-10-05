# Pulse for Windows 98 SE: cross build with mingw-w64 (i686).
#
#   make                 build pulse98.exe into $(BUILD)
#   make test            unit tests (host gcc), plus the Win32 test exe under Wine
#   make dist            copy the exe into dist/
#
# Build output goes to $(BUILD); override it, for example BUILD=/tmp/pulse98.

BUILD    ?= build
CC       ?= i686-w64-mingw32-gcc
WINCC    := i686-w64-mingw32-gcc
WINDRES  := i686-w64-mingw32-windres
HOSTCC   ?= gcc
PYTHON   ?= python3
WINE     ?= wine

LOCALES  := ../../locales
VECTORS  := ../../i18n/test/fixtures/vectors.json

WARN     := -std=c89 -pedantic -Wall -Wextra -Wno-long-long -Wdeclaration-after-statement -Wno-unused-parameter -Wno-cast-function-type
CFLAGS   := $(WARN) -Os -march=pentium2 -mno-sse -mno-sse2 -fno-asynchronous-unwind-tables \
            -fno-stack-protector -DPULSE_NOCRT -DWINVER=0x0410 -D_WIN32_WINDOWS=0x0410 -D_WIN32_WINNT=0x0400 \
            -Isrc -I$(BUILD)
# No CRT: only kernel32, user32, gdi32, advapi32 and shell32 are imported directly.
LDFLAGS  := -nostdlib -mwindows -s -Wl,--entry,_WinMainCRTStartup@0 \
            -Wl,--subsystem,windows:4.0 -Wl,--major-os-version,4 -Wl,--minor-os-version,0 \
            -Wl,--disable-dynamicbase -Wl,--disable-nxcompat
LIBS     := -lkernel32 -luser32 -lgdi32 -ladvapi32 -lshell32 -lgcc

SRC      := src/pulse.c src/telemetry.c src/text.c src/i18n.c src/plural.c src/nocrt.c
DEPS     := $(SRC) src/i18n.h src/plural.h src/text.h src/telemetry.h

all: $(BUILD)/pulse98.exe

$(BUILD)/catalog.c: tools/gen_catalog.py $(wildcard $(LOCALES)/*.json) $(wildcard locales-native/*.json)
	@mkdir -p $(BUILD)
	$(PYTHON) tools/gen_catalog.py $(LOCALES) locales-native $@

$(BUILD)/vectors.h: tools/gen_vectors.py $(VECTORS)
	@mkdir -p $(BUILD)
	$(PYTHON) tools/gen_vectors.py $(VECTORS) $@

$(BUILD)/pulse.ico: tools/gen_icon.py
	@mkdir -p $(BUILD)
	$(PYTHON) tools/gen_icon.py $@

$(BUILD)/pulse.res.o: src/pulse.rc $(BUILD)/pulse.ico
	$(WINDRES) -I$(BUILD) -O coff -o $@ src/pulse.rc

$(BUILD)/pulse98.exe: $(DEPS) $(BUILD)/catalog.c $(BUILD)/pulse.res.o
	$(WINCC) $(CFLAGS) -o $@ $(SRC) $(BUILD)/catalog.c $(BUILD)/pulse.res.o $(LDFLAGS) $(LIBS)

$(BUILD)/test_i18n: tests/test_i18n.c src/i18n.c src/plural.c $(BUILD)/catalog.c $(BUILD)/vectors.h
	$(HOSTCC) $(WARN) -Isrc -I$(BUILD) -o $@ tests/test_i18n.c src/i18n.c src/plural.c $(BUILD)/catalog.c

$(BUILD)/test_i18n.exe: tests/test_i18n.c src/i18n.c src/plural.c $(BUILD)/catalog.c $(BUILD)/vectors.h
	$(WINCC) $(WARN) -march=pentium2 -Isrc -I$(BUILD) -o $@ tests/test_i18n.c src/i18n.c src/plural.c $(BUILD)/catalog.c

test: $(BUILD)/test_i18n $(BUILD)/test_i18n.exe
	$(BUILD)/test_i18n
	$(WINE) $(BUILD)/test_i18n.exe

dist: $(BUILD)/pulse98.exe
	@mkdir -p dist
	cp $(BUILD)/pulse98.exe dist/

clean:
	rm -rf $(BUILD)

.PHONY: all test dist clean

# CPU load helper for the hog alert screenshot
$(BUILD)/cpuburn.exe: tests/burn.c
	$(WINCC) -O1 -o $@ tests/burn.c

# Memory figures for a running pulse98.exe (run under the same Wine prefix)
$(BUILD)/memprobe.exe: tests/memprobe.c
	$(WINCC) -O1 -o $@ tests/memprobe.c -lpsapi
