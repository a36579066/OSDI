AS := aarch64-linux-gnu-as
LD := aarch64-linux-gnu-ld
OBJCOPY := aarch64-linux-gnu-objcopy

target ?= program2
ASM_SRC = $(firstword $(wildcard $(target).s $(target).S))
OBJ = $(target).o
ELF = $(target).elf
BIN = $(target).bin
LOAD_ADDR := 0x80000
ENTRY_SYMBOL = $(if $(filter hello_exec program2 test_exec,$(target)),user_main,_start)

.PHONY: all cpio rootfs-programs signal-test mmap-test clean

all: $(BIN)

$(BIN): $(ASM_SRC)
	$(AS) $(ASM_SRC) -o $(OBJ)
	$(LD) -e $(ENTRY_SYMBOL) $(OBJ) -o $(ELF) --section-start=.text=$(LOAD_ADDR)
	$(OBJCOPY) -O binary $(ELF) $(BIN)

rootfs-programs:
	$(MAKE) target=hello_exec
	cp hello_exec.bin ../rootfs/hello_exec.bin
	cp hello_exec.bin ../rootfs/hello.bin
	$(MAKE) target=program
	cp program.bin ../rootfs/program.bin
	$(MAKE) target=program2
	cp program2.bin ../rootfs/program2.bin
	$(MAKE) target=test_exec
	cp test_exec.bin ../rootfs/test_exec.bin
	$(MAKE) target=signal_test
	cp signal_test.bin ../rootfs/signal_test.img
	$(MAKE) target=mmap_test
	cp mmap_test.bin ../rootfs/mmap_test.img
	$(MAKE) target=vm_adv_test
	cp vm_adv_test.bin ../rootfs/vm_adv_test.img
	$(MAKE) target=vm_fault_test
	cp vm_fault_test.bin ../rootfs/vm_fault_test.img
	$(MAKE) target=vm_unmapped_test
	cp vm_unmapped_test.bin ../rootfs/vm_unmapped_test.img

cpio:
	cd ../rootfs && find . | cpio -o -H newc > ../initramfs.cpio

signal-test:
	$(MAKE) target=signal_test
	cp signal_test.bin ../rootfs/signal_test.img
	$(MAKE) cpio

mmap-test:
	$(MAKE) target=mmap_test
	cp mmap_test.bin ../rootfs/mmap_test.img
	$(MAKE) cpio

clean:
	rm -f *.o *.elf *.bin
