ENTRY(_start)

KERNEL_VA_BASE = 0xffff000000000000;

SECTIONS {
    . = 0x80000;
    _start = .;
    __kernel_phys_start = .;

    /* This small entry stub executes at its physical load address until it
     * has installed the bootstrap page tables and enabled the MMU. */
    .text.boot : {
        *(.text.boot)
    }

    . = ALIGN(0x1000);
    . += KERNEL_VA_BASE;
    _kernel_start = .;

    /* Every normal kernel section has a high virtual address but retains a
     * physical load address in the raw kernel image QEMU places at 0x80000. */
    .text : AT(ADDR(.text) - KERNEL_VA_BASE) {
        *(.text)
    }

    .rodata : AT(ADDR(.rodata) - KERNEL_VA_BASE) {
        *(.rodata)
    }

    .bss : AT(ADDR(.bss) - KERNEL_VA_BASE) {
        . = ALIGN(16);
        _bss_start = .;
        *(.bss)
        *(COMMON)
        _bss_end = .;
    }

    .data : AT(ADDR(.data) - KERNEL_VA_BASE) {
        . = ALIGN(16);
        *(.data)
    }
    
    . = ALIGN(16);
    .heap : AT(ADDR(.heap) - KERNEL_VA_BASE) {
        _heap_start = .;
        . = . + 0x4000; /* 16KB heap */
        . = ALIGN(16);   
        _heap_end = .;
    }
    . = ALIGN(16);
    
    .stack (NOLOAD) : AT(ADDR(.stack) - KERNEL_VA_BASE) {
        . = . + 0x400000;  /* 4MB stack */
    }
    _stack_top = .;
    _end = .;
    __kernel_phys_end = _end - KERNEL_VA_BASE;
}
