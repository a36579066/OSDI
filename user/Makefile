# 指定編譯器
AS = aarch64-linux-gnu-as
LD = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy

# 預設檔案名稱 (可用 make target=yourfile 來指定)
TARGET ?= program

# 輸出檔案名稱
ASM_SRC = $(TARGET).s
OBJ = $(TARGET).o
ELF = $(TARGET).elf
BIN = $(TARGET).bin

# 設定 User Program 載入的起始位址
LOAD_ADDR = 0x80000

# 編譯規則
all: $(BIN)

$(BIN): $(ASM_SRC)
	$(AS) $(ASM_SRC) -o $(OBJ)
	$(LD) $(OBJ) -o $(ELF) --section-start=.text=$(LOAD_ADDR)
	$(OBJCOPY) -O binary $(ELF) $(BIN)

clean:
	rm -f $(OBJ) $(ELF) $(BIN)
