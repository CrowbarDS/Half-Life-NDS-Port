# The ARM11 payload put into TwlBg (feed.c). Needs devkitARM.
DEVKITARM ?= /opt/devkitpro/devkitARM
CC := $(DEVKITARM)/bin/arm-none-eabi-gcc
OBJCOPY := $(DEVKITARM)/bin/arm-none-eabi-objcopy
CFLAGS := -march=armv6k -mtune=mpcore -mthumb -Os -Wall -Wextra -ffreestanding -fno-builtin \
	-fno-common -nostdlib -fomit-frame-pointer -mno-unaligned-access

all: feed.bin pass.bin

feed.elf: feed.c feed.h feed.ld
	$(CC) $(CFLAGS) -T feed.ld -Wl,--gc-sections,-Map,feed.map -o $@ feed.c

# pass.c: a test that only calls TwlBg's own routine.
pass.elf: pass.c feed.ld
	$(CC) $(CFLAGS) -T feed.ld -Wl,--gc-sections -o $@ pass.c

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(stat -c %s $@) of 488 bytes"

.PHONY: all
