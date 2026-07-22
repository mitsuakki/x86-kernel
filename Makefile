ASM      := nasm
CC       := gcc
LD       := ld
OBJCOPY  := objcopy
QEMU     := qemu-system-x86_64

BOOT_DIR   := boot
KERNEL_DIR := kernel
OUT_DIR    := out

BOOT_BIN   := $(OUT_DIR)/bootloader.bin
KERNEL_ELF := $(OUT_DIR)/kernel.elf
KERNEL_BIN := $(OUT_DIR)/kernel.bin
OS_IMG     := $(OUT_DIR)/os.img

ASMFLAGS := -f bin
CFLAGS   := -ffreestanding -nostdlib -m32 -Wall -Wextra -O2 -std=c11 -fno-asynchronous-unwind-tables -fno-pic
LDFLAGS  := -melf_i386 -nostdlib -Ttext 0x1000

.PHONY: all build run clean

all: build

build: $(OS_IMG)

$(OUT_DIR):
	mkdir -p $@

$(BOOT_BIN): $(BOOT_DIR)/bootloader.asm | $(OUT_DIR)
	$(ASM) $(ASMFLAGS) -o $@ $<

$(KERNEL_ELF): $(KERNEL_DIR)/kernel.c | $(OUT_DIR)
	$(CC) $(CFLAGS) -c -o $(OUT_DIR)/kernel.o $<
	$(LD) $(LDFLAGS) -e kernel_main -o $@ $(OUT_DIR)/kernel.o

$(KERNEL_BIN): $(KERNEL_ELF) | $(OUT_DIR)
	$(OBJCOPY) -O binary $< $@

$(OS_IMG): $(BOOT_BIN) $(KERNEL_BIN) | $(OUT_DIR)
	cat $(BOOT_BIN) $(KERNEL_BIN) > $@
	dd if=/dev/zero bs=1024 count=1440 >> $@ 2>/dev/null || true

run: $(OS_IMG)
	$(QEMU) -fda $<

clean:
	rm -rf $(OUT_DIR)
