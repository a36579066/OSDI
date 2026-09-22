# 指定編譯器
AS = aarch64-linux-gnu-as
LD = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy

.PHONY: all cpio signal-test clean

# 預設檔案名稱 (可用 make target=yourfile 來指定)
target ?= program2

# 輸出檔案名稱
ASM_SRC = $(target).s
OBJ = $(target).o
ELF = $(target).elf
BIN = $(target).bin

# 設定 User Program 載入的起始位址
LOAD_ADDR = 0x80000

# 編譯規則
all: $(BIN)

$(BIN): $(ASM_SRC)
	$(AS) $(ASM_SRC) -o $(OBJ)
	$(LD) $(OBJ) -o $(ELF) --section-start=.text=$(LOAD_ADDR)
	$(OBJCOPY) -O binary $(ELF) $(BIN)

cpio:
	cd ../rootfs && find . | cpio -o -H newc > ../initramfs.cpio

signal-test:
	$(MAKE) target=signal_test
	cp signal_test.bin ../rootfs/signal_test.img
	$(MAKE) cpio

clean:
	rm -f $(OBJ) $(ELF) $(BIN)
	rm -f *bin *elf *o
