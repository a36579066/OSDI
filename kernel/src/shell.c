#include <uart.h>
#include <string.h>
#include <mailbox.h>
#include <reboot.h>
#include <cpio_.h>
#include <allocator.h>
#include <user_prog.h>
#include <exception_.h>
#include <timer.h>
#include <utils.h>
#include <thread.h>

#define MAX_CMD_LEN 128 
extern void core_timer_enable();

static inline void delay(int count) {
  while (count--) {
      asm volatile("nop");
  }
}

void shell() {
    char command_buffer[MAX_CMD_LEN]; 
    // int pos = 0;
    uart_puts("Welcome to Simple Shell!\n");
    uart_puts("Type 'help' to see available commands.\n");
    

    while (1) {
        uart_puts("# "); 
        uart_gets(command_buffer, MAX_CMD_LEN);
        char* ptr = command_buffer;
        char* token = get_token(&ptr);
        if(token == 0) continue;

        // if (command_buffer[0] == '\0') continue; 

        if (strcmp_(token, "help") == 0) {
            uart_puts("help\t: print all available commands\n");
            uart_puts("hello\t: print Hello World!\n");
            uart_puts("reboot\t: reboot the device\n");
            uart_puts("hwinfo\t: check the hardware information\n");
            uart_puts("ls : list all files in the directory\n");
            uart_puts("memAlloc : allocate some memory\n");

        } else if (strcmp_(token, "hello") == 0) {
            uart_puts("Hello World!\n");
        } else if (strcmp_(token, "reboot") == 0){
            uart_puts("Rebooting system...\n");
            reboot();
        }else if (strcmp_(token, "hwinfo") == 0){
              // Board Revision
              unsigned int board_revision = get_board_revision();
              uart_puts("Board Revision: ");
              uart_put_hex(board_revision);
              uart_puts("\n");
              // ARM Memory Base and Size
              unsigned int mem_base, mem_size;
              get_arm_memory(&mem_base, &mem_size);

              uart_puts("ARM Memory Base: ");
              uart_put_hex(mem_base);
              uart_puts("\n");
              uart_puts("ARM Memory Size: ");
              uart_put_hex(mem_size);
              uart_puts("\n");
        }else if (strcmp_(token, "cat") == 0){
            uart_puts("Filename: ");
            char filename_buffer[MAX_CMD_LEN]; 
            uart_gets(filename_buffer, MAX_CMD_LEN);
            cat_cpio_file(filename_buffer);


        }else if (strcmp_(token, "ls") == 0){
            ls_cpio_archive();
        }else if(strcmp_(token, "memAlloc") == 0){
            char* num_str = get_token(&ptr);
            if (num_str == 0) {
                uart_puts("Error: no allocation size provided.\n");
                continue;
            }
            
            unsigned int alloc_size = (unsigned int)atoi_(num_str, strlen_(num_str));
            if (alloc_size == 0) {
                uart_puts("Error: invalid allocation size.\n");
            }
            
            char* allocated = (char*)simple_malloc((size_t)alloc_size);
            if (allocated == 0) {
                uart_puts("Allocation failed.\n");
            } else {
                uart_puts("Allocation successful at address: ");
                uart_put_hex((uint32_t)(uintptr_t)allocated);
                uart_puts("\n");
            }
                
        }
        else if(strcmp_(token, "exec") == 0){
        //else if(1){
            char* file_name = get_token(&ptr);
            //char* file_name = "program.bin";
            if (file_name == 0) {
                uart_puts("Error: no executed program file provided.\n");
                continue;
            }
            execute_user_program(file_name);

        }
        else {
            uart_puts("Unknown command: ");
            uart_puts(command_buffer);
            uart_puts("\n");
        }
    }
}

void async_shell() {
    // uart_puts("in shell");
    int flag = 1; // 判斷是否輸出 #
    disable_interrupts();
    char* ptr = uart_rx_buffer;
    char* token = get_token(&ptr);
    
    // enable_interrupts();

    if (!token){
        uart_puts("\n# ");
        disable_interrupts();
        uart_rx_tail = 0;
        uart_tx_tail = 0;
        uart_tx_head = 0;
        enable_interrupts();
        // memset_(uart_rx_buffer, 0, UART_BUF_SIZE);
        return;
    };
    // int pos = 0;
    // uart_puts("# "); 
    // uart_gets(command_buffer, MAX_CMD_LEN);
    // char* ptr = command_buffer;
    // char* token = get_token(&ptr);
    if (strcmp_(token, "help") == 0) {
        uart_puts("help\t\t: print all available commands\n");
        uart_puts("hello\t\t: print Hello World!\n");
        uart_puts("reboot\t\t: reboot the device\n");
        uart_puts("hwinfo\t\t: check the hardware information\n");
        uart_puts("ls\t\t: list all files in the directory\n");
        uart_puts("cat\t\t: display contents of a file\n");
        uart_puts("memAlloc <size>\t: allocate memory of given size\n");
        uart_puts("exec <file>\t: execute user program\n");
        uart_puts("timer\t\t: test setTimeout with message\n");
        uart_puts("test\t\t: test Nested Interrupt and Preemption\n");
    } else if (strcmp_(token, "hello") == 0) {
        uart_puts("Hello World!\n");
    } else if (strcmp_(token, "reboot") == 0){
        uart_puts("Rebooting system...\n");
        reboot();
    }else if (strcmp_(token, "hwinfo") == 0){
            // Board Revision
            unsigned int board_revision = get_board_revision();
            uart_puts("Board Revision: ");
            uart_put_hex(board_revision);
            uart_puts("\n");
            // ARM Memory Base and Size
            unsigned int mem_base, mem_size;
            get_arm_memory(&mem_base, &mem_size);

            uart_puts("ARM Memory Base: ");
            uart_put_hex(mem_base);
            uart_puts("\n");
            uart_puts("ARM Memory Size: ");
            uart_put_hex(mem_size);
            uart_puts("\n");
    }else if (strcmp_(token, "cat") == 0){
        
        uart_puts("\nFilename: ");
        char filename_buffer[MAX_CMD_LEN]; 
        uart_gets(filename_buffer, MAX_CMD_LEN);
        cat_cpio_file(filename_buffer);
        uart_puts("\n");


    }else if (strcmp_(token, "ls") == 0){
        ls_cpio_archive();
    }else if(strcmp_(token, "memAlloc") == 0){
        char* num_str = get_token(&ptr);
        if (num_str == 0) {
            uart_puts("Error: no allocation size provided.\n");
        }
        
        unsigned int alloc_size = (unsigned int)atoi_(num_str, strlen_(num_str));
        if (alloc_size == 0) {
            uart_puts("Error: invalid allocation size.\n");
        }
        
        char* allocated = (char*)simple_malloc((size_t)alloc_size);
        if (allocated == 0) {
            uart_puts("Allocation failed.\n");
        } else {
            uart_puts("Allocation successful at address: ");
            uart_put_hex((uint32_t)(uintptr_t)allocated);
            uart_puts("\n");
        }
            
    }
    else if(strcmp_(token, "exec") == 0){
    //else if(1){
        
        char* file_name = get_token(&ptr);
        //char* file_name = "program.bin";
        if (file_name == 0) {
            uart_puts("Error: no executed program file provided.\n");
        }
        execute_user_program(file_name);
        

    }else if (strcmp_(token, "timer") == 0) {
        flag = 0;
        setTimeout("mesg 1",1);
        setTimeout("mesg 3",2);
        setTimeout("mesg 5",3);
        setTimeout("mesg 4",5);
        
        

    }else if(strcmp_(token, "test") == 0){
        // char* type = get_token(&ptr);
        // uart_puts("\ntest case: ");
        // char test_buffer[MAX_CMD_LEN]; 
        // uart_gets(test_buffer, MAX_CMD_LEN);
        
        // if (test_buffer && test_buffer[0] == '1') 
        test_type = 1;
        uart_puts("[Press any bottom...]");
        flag = 0;
        
        // uart_putint(test_type);
    }else if(strcmp_(token, "malloc_") == 0){
        char* num_str = get_token(&ptr);
        if (num_str == 0) {
            uart_puts("Error: no allocation size provided.\n");
            disable_interrupts();
            uart_rx_tail = 0;
            uart_tx_tail = 0;
            uart_tx_head = 0;
            enable_interrupts();
            if(flag) uart_puts("# ");
            return;
        }
        
        unsigned int alloc_size = (unsigned int)atoi_(num_str, strlen_(num_str));
        void* add = kmalloc(alloc_size);
        uart_puts("Allocated at address: ");
        uart_put_hex((uintptr_t)add);NL;

    }else if(strcmp_(token, "free_") == 0){
        char* num_str = get_token(&ptr);
        
        unsigned int free_addr = (unsigned int)atoi_(num_str, strlen_(num_str));
        kfree((void*)(uintptr_t)free_addr);
        // uart_puts("Allocated at address: ");
        // uart_put_hex((uintptr_t)add);NL;

    }
    else if(strcmp_(token, "table") == 0){ // for buddy system demo
        print_pool_state();
        print_free_list();
    }
    else if(strcmp_(token, "thread") == 0){
        DEMO = 0;
        
        thread_init();
        
        // struct cpio_file file = parse_or_find_file_in_archive("test_exec.bin", 1);
        // char* user_program_addr = kmalloc(file.size+8);
        // // // 分配記憶體來放 user program
        // void* user_space = (void*) user_program_addr;
        // memcpy_(user_space, file.data, file.size);
        // char* user_space_addr = kmalloc(THREAD_STK_SIZE);
        // void* user_sp = (void*) user_space_addr + THREAD_STK_SIZE;
        
        // uart_puts("add thread ");
        for(int i=0;i<2;++i)
            thread_create(foo, 0);
        // thread_t* t = process_create(user_program_addr, user_sp);
        // uart_putint(t->pid);NL;
        
        // start scheduling
        // asm volatile("msr tpidr_el1, %0" ::"r" (kmalloc(sizeof(thread_t))));
        
        idle_task();
        
        // schedule();
        // schedule();
        DEMO = 0;
        
    }
    else if(strcmp_(token, "process") == 0){
        disable_interrupts();
        uart_rx_tail = 0;
        uart_tx_tail = 0;
        uart_tx_head = 0;
        enable_interrupts();
        // 1. 建 kernel thread (idle, foo, ...)
        thread_init();

        // // 2. 建第一個 user process
        //    entry_point = user_main, user_sp = USER_STACK_TOP
        
        // thread_t *u = process_create((uint64_t)user_space, USER_STACK_TOP+10000);
        thread_t *u = thread_create(init_user_prog, 0);
        
        // asm volatile("mov x0, 0  \n");
        // asm volatile("msr spsr_el1, x0   \n"); 
        // asm volatile("msr elr_el1,%0" ::"r"(u->context.lr));
        // asm volatile("msr sp_el0,%0" :: "r"(u->context.sp));
        // asm volatile("eret");
        u->status = THREAD_READY;  // process_create 沒 enqueue 的話要補

        // 3. 一進 scheduler 就會選到 user process
        // schedule();
        idle_task();
        
        // 不會回到這裡
        while (1) { /* safe‐spin */ }
    }
    else {
        uart_puts("Unknown command: ");
        uart_puts(token);
        uart_puts("\n");
    }
    
    // 清空 buffer，準備下一次輸入
    disable_interrupts();
    uart_rx_tail = 0;
    uart_tx_tail = 0;
    uart_tx_head = 0;
    enable_interrupts();
    // memset_(uart_rx_buffer, 0, UART_BUF_SIZE);
    if(flag) uart_puts("# ");
    
}
