# PortaDoom build
#
#   make            native static library + headless test host
#   make x11        playable X11 host (Linux)
#   make clean
#
# The engine is built freestanding against libc/include only: no system
# headers, no OS calls. Any C99 compiler for any CPU should work.

CC      ?= cc
AR      ?= ar
BUILD   ?= build/native

ENGINE_SRC := $(wildcard engine/*.c)
PD_SRC     := $(wildcard src/*.c) libc/pd_libc.c
LIB_SRC    := $(ENGINE_SRC) $(PD_SRC)
LIB_OBJ    := $(patsubst %.c,$(BUILD)/%.o,$(LIB_SRC))

# -fwrapv: Doom relies on signed overflow wrapping (determinism)
FREESTANDING := -ffreestanding -nostdinc -fno-stack-protector \
                -U_WIN32 -U__APPLE__ -U__MACOSX__ -U__DJGPP__ -U__linux__ -Ulinux
LIB_CFLAGS   := $(FREESTANDING) -std=gnu99 -O2 -g -fwrapv -fno-strict-aliasing \
                -fPIC -DFEATURE_SOUND \
                -Ilibc/include -Iengine -Isrc -Iinclude

LIB := $(BUILD)/libportadoom.a

all: $(LIB) $(BUILD)/pd_headless

$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/engine/%.o: engine/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(LIB_CFLAGS) -w -c $< -o $@

$(BUILD)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LIB_CFLAGS) -Wall -Wno-unused-function -c $< -o $@

$(BUILD)/libc/%.o: libc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LIB_CFLAGS) -Wall -c $< -o $@

$(BUILD)/pd_headless: hosts/headless/main.c $(LIB)
	$(CC) -O2 -g -Wall -Iinclude $< $(LIB) -lm -o $@

x11: $(BUILD)/pd_x11

$(BUILD)/pd_x11: hosts/x11/main.c $(LIB)
	$(CC) -O2 -g -Wall -Iinclude $< $(LIB) -lX11 -lpthread -o $@

clean:
	rm -rf build

.PHONY: all clean x11
