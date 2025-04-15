ENTRY(_start)

SECTIONS {
    

    . = 0x80000;
    _start = .;
    .text : {
        *(.text.boot) *(.text) 
    }

    .bss : {
        . = ALIGN(16);
        _bss_start = .;
        *(.bss)
        *(COMMON)
        _bss_end = .;
    }

    .data : {
        . = ALIGN(16);
        *(.data)
    }
    
    . = ALIGN(16);
    .heap : {
        _heap_start = .;
        . = . + 0x4000; /* 16KB heap */
        . = ALIGN(16);   
        _heap_end = .;
    }
    . = ALIGN(16);

    .stack (NOLOAD) : {
        . = . + 0x4000;  /* 16KB stack */
    }
    _stack_top = .;
    _end = .;
}
