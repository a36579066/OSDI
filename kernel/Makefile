# Toolchain
.PHONY: all clean run debug demo debug-table

CC = aarch64-linux-gnu-gcc
LD = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy

# Debug switches can be overridden from the command line, for example:
#   make -C kernel -B ALLOC_DEBUG=1 run
ALLOC_DEBUG ?= 0
ALLOC_DEMO ?= 0
ALLOC_DEBUG_ALL ?= 0

# Compilation flags
CFLAGS = -Wall -nostdlib -ffreestanding -Iinclude -I../common/include -g \
	-DALLOC_DEBUG=$(ALLOC_DEBUG) -DALLOC_DEMO=$(ALLOC_DEMO) \
	-DALLOC_DEBUG_ALL=$(ALLOC_DEBUG_ALL)
LDFLAGS = -T src/linker.ld

# Directories
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
ASMS = $(wildcard $(SRC_DIR)/*.S)

# Object files (Kernel)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS)) \
       $(patsubst $(SRC_DIR)/%.S, $(BUILD_DIR)/%.o, $(ASMS))

# Object files (Common)
COMMON_OBJS = $(wildcard ../common/build/*.o)

# Output files
ELF = $(BUILD_DIR)/kernel8.elf
IMG = $(BUILD_DIR)/kernel8.img

# Default target
all: $(IMG)

# Compile C source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile Assembly source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link object files (Kernel + Common)
$(ELF): $(OBJS) $(COMMON_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# Convert ELF to binary
$(IMG): $(ELF)
	$(OBJCOPY) -O binary $< $@

# Create build directory if not exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Clean target
clean:
	rm -rf $(BUILD_DIR)

# Run in QEMU
run: $(IMG)
	qemu-system-aarch64 -M raspi3b -kernel $(IMG) -dtb bcm2710-rpi-3-b-plus.dtb -initrd ../initramfs.cpio -serial null -serial stdio

# Rebuild is intentional: Make does not track changes to compiler variables.
debug:
	$(MAKE) -B ALLOC_DEBUG=1 run

demo:
	$(MAKE) -B ALLOC_DEBUG=1 ALLOC_DEMO=1 run

debug-table:
	$(MAKE) -B ALLOC_DEBUG=1 ALLOC_DEBUG_ALL=1 run
