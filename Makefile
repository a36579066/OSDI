.PHONY: all common kernel run clean debug demo debug-table

COMMON_DIR := common
COMMON_SRC_DIR := $(COMMON_DIR)/src
COMMON_INCLUDE_DIR := $(COMMON_DIR)/include
COMMON_BUILD_DIR := $(COMMON_DIR)/build

COMMON_SRCS := $(wildcard $(COMMON_SRC_DIR)/*.c)
COMMON_OBJS := $(patsubst $(COMMON_SRC_DIR)/%.c,$(COMMON_BUILD_DIR)/%.o,$(COMMON_SRCS))

all: common kernel

common: $(COMMON_OBJS)

$(COMMON_BUILD_DIR)/%.o: $(COMMON_SRC_DIR)/%.c | $(COMMON_BUILD_DIR)
	aarch64-linux-gnu-gcc -Wall -nostdlib -ffreestanding -I$(COMMON_INCLUDE_DIR) -c $< -o $@

$(COMMON_BUILD_DIR):
	mkdir -p $@

kernel: common
	$(MAKE) -C kernel

# Build user payloads and the initramfs before starting the standard QEMU flow.
run: common
	$(MAKE) -C user rootfs-programs
	$(MAKE) -C user cpio
	$(MAKE) -C kernel run

debug: common
	$(MAKE) -C kernel debug

demo: common
	$(MAKE) -C kernel demo

debug-table: common
	$(MAKE) -C kernel debug-table

clean:
	rm -rf $(COMMON_BUILD_DIR)
	$(MAKE) -C kernel clean
	$(MAKE) -C user clean
	rm -f initramfs.cpio
