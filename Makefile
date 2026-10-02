# VGA Font Editor - MinGW-w64 build
#
#   Windows (MSYS2 / MinGW-w64):   make
#   Cross compile from Linux:      make CROSS=x86_64-w64-mingw32-
#   Core unit tests (host cc):     make test

CROSS   ?=
CC      := $(CROSS)gcc
WINDRES := $(CROSS)windres
HOSTCC  ?= cc

CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra -Isrc -Ires \
           -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -DWINVER=0x0601
LDFLAGS += -mwindows -static
LDLIBS  := -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -luser32 -lkernel32

TARGET  := vgafontedit.exe
SRCS    := $(wildcard src/*.c)
OBJS    := $(patsubst src/%.c,build/%.o,$(SRCS)) build/app_res.o
CORE    := src/font.c src/undo.c src/cp437.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

build/app_res.o: res/app.rc res/resource.h res/app.ico res/app.manifest | build
	$(WINDRES) -Ires -O coff -i $< -o $@

build:
	mkdir -p build

test: | build
	$(HOSTCC) -std=c99 -Wall -Wextra -Isrc -o build/test_core tests/test_core.c $(CORE)
	./build/test_core

clean:
	rm -rf build $(TARGET)

-include $(OBJS:.o=.d)
