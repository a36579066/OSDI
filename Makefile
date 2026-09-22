.PHONY: all clean kernel common debug demo debug-table

all: common kernel


# Directories
COMMON_DIR_NAME = common
COMMON_SRC_DIR = $(COMMON_DIR_NAME)/src
COMMON_INCLUDE_DIR = $(COMMON_DIR_NAME)/include
COMMON_BUILD_DIR = $(COMMON_DIR_NAME)/build


COMMON_SRCS := $(wildcard $(COMMON_SRC_DIR)/*.c)
COMMON_OBJS := $(patsubst $(COMMON_SRC_DIR)/%.c, $(COMMON_BUILD_DIR)/%.o, $(COMMON_SRCS))

all: common kernel

common: $(COMMON_OBJS)

common/build/%.o: $(COMMON_SRC_DIR)/%.c | $(COMMON_BUILD_DIR)
	aarch64-linux-gnu-gcc -Wall -nostdlib -ffreestanding -I$(COMMON_INCLUDE_DIR) -c $< -o $@

common/build:
	mkdir -p common/build

kernel: common
	$(MAKE) -C kernel

clean:
	rm -rf common/build/*.o
	$(MAKE) -C kernel clean

# run:
# 	$(MAKE) -C bootloader run
# 	# qemu-system-aarch64 -M raspi3b -kernel kernel/build/kernel8.img -display none -serial null -serial stdio 

run: common
	$(MAKE) -C kernel run

debug: common
	$(MAKE) -C kernel debug

demo: common
	$(MAKE) -C kernel demo

debug-table: common
	$(MAKE) -C kernel debug-table
