ASM      := nasm
CC       := gcc
LD       := ld
OBJCOPY  := objcopy
QEMU     := qemu-system-x86_64

BOOT_DIR   := boot
KERNEL_DIR := kernel
OUT_DIR    := out

STAGE1_DIR := $(BOOT_DIR)/stage1
STAGE2_DIR := $(BOOT_DIR)/stage2
ARCH_DIR   := $(KERNEL_DIR)/arch/x86_64
DRIVERS_DIR := $(KERNEL_DIR)/drivers
LIB_DIR    := $(KERNEL_DIR)/lib

BOOT_BIN   := $(OUT_DIR)/boot.bin
LOADER_BIN := $(OUT_DIR)/loader.bin
KERNEL_ELF := $(OUT_DIR)/kernel.elf
OS_IMG     := $(OUT_DIR)/os.img

ASMFLAGS := -f bin
CFLAGS   := -ffreestanding -nostdlib -m64 -mno-red-zone -mcmodel=large \
            -Wall -Wextra -O2 -std=c11 \
            -fno-asynchronous-unwind-tables -fno-pic -fno-stack-protector \
            -fno-tree-vectorize \
            -I $(LIB_DIR) -I $(DRIVERS_DIR) -I $(ARCH_DIR)
LDFLAGS  := -melf_x86_64 -nostdlib -T $(KERNEL_DIR)/linker.ld

.PHONY: all build run debug gdb clean

all: build

build: $(OS_IMG)

$(OUT_DIR):
	mkdir -p $@

# Stage 1 — 512-byte bootsector (LBA 0)
$(BOOT_BIN): $(STAGE1_DIR)/boot.s | $(OUT_DIR)
	$(ASM) $(ASMFLAGS) -o $@ $<

# Stage 2 — loads kernel, A20, GDT, protected mode, paging, ELF parser (LBA 1)
$(LOADER_BIN): $(STAGE2_DIR)/loader.s $(STAGE2_DIR)/a20.s \
               $(STAGE2_DIR)/gdt.s $(STAGE2_DIR)/cpuid.s \
               $(STAGE2_DIR)/longmode.s | $(OUT_DIR)
	$(ASM) $(ASMFLAGS) -i $(STAGE2_DIR) -o $@ $<

KERNEL_C := $(shell find $(KERNEL_DIR) -name '*.c')
KERNEL_O := $(patsubst $(KERNEL_DIR)/%.c, $(OUT_DIR)/%.o, $(KERNEL_C))

# Kernel ELF — linked at 0x100000 (1 MiB)
$(KERNEL_ELF): $(KERNEL_O) $(OUT_DIR)/arch/x86_64/isr_stubs.o $(KERNEL_DIR)/linker.ld | $(OUT_DIR)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_O) $(OUT_DIR)/arch/x86_64/isr_stubs.o

$(OUT_DIR)/%.o: $(KERNEL_DIR)/%.c | $(OUT_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# IDT asm stubs — 32 ISR entry points + common handler
$(OUT_DIR)/arch/x86_64/isr_stubs.o: $(ARCH_DIR)/isr_stubs.s | $(OUT_DIR)
	@mkdir -p $(dir $@)
	$(ASM) -f elf64 -o $@ $<

# Disk image — 1.44 MiB floppy
$(OS_IMG): $(BOOT_BIN) $(LOADER_BIN) $(KERNEL_ELF) | $(OUT_DIR)
	dd if=/dev/zero of=$@ bs=512 count=2880
	dd if=$(BOOT_BIN) of=$@ bs=512 count=1 conv=notrunc
	dd if=$(LOADER_BIN) of=$@ bs=512 seek=1 conv=notrunc
	dd if=$(KERNEL_ELF) of=$@ bs=512 seek=9 conv=notrunc

run: $(OS_IMG)
	$(QEMU) -fda $< -serial stdio

debug: $(OS_IMG)
	$(QEMU) -fda $< -serial stdio -s -S

gdb:
	gdb -ex "target remote localhost:1234" -ex "symbol-file $(KERNEL_ELF)"

clean:
	rm -rf $(OUT_DIR)
