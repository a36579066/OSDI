#include "mailbox.h"
#include "reboot.h"
#include "allocator.h"
#include <stdint.h>



#define GET_BOARD_REVISION  0x00010002
#define REQUEST_CODE        0x00000000
#define REQUEST_SUCCEED     0x80000000
#define REQUEST_FAILED      0x80000001
#define TAG_REQUEST_CODE    0x00000000
#define END_TAG             0x00000000
#define MAILBOX_TIMEOUT     10000000U


void mailbox_write(unsigned int channel, unsigned int data) {
    while (MAILBOX_STATUS & MAILBOX_FULL); // wait data from Mailbox
    MAILBOX_WRITE = (data & ~0xF) | (channel & 0xF); 
}

unsigned int mailbox_read(unsigned int channel) {
    unsigned int data;
    do {
        while (MAILBOX_STATUS & MAILBOX_EMPTY); // wait data from Mailbox
        data = MAILBOX_READ;
    } while ((data & 0xF) != channel); 
    return data & ~0xF;
}


int mailbox_call(unsigned char channel, volatile unsigned int *mailbox_buffer, unsigned int size) {
    if (!mailbox_buffer || size < 2) return 0;  // header + request code

    uintptr_t buffer_kva = (uintptr_t)mailbox_buffer;
    if (buffer_kva & 0xF)
        return 0;  // mailbox property buffers must be 16-byte aligned
    uintptr_t buffer_pa = buffer_kva < BUDDY_ROOT_END
        ? buffer_kva
        : KVA_TO_PHYS(buffer_kva);
    unsigned int r = ((unsigned int)(buffer_pa & ~0xFUL)) | (channel & 0xF);

    /* A mailbox request runs from an SVC handler, where IRQs are masked.
     * Waiting forever here would therefore freeze both timer scheduling and
     * UART input if QEMU/the GPU does not answer this request. */
    unsigned int timeout = MAILBOX_TIMEOUT;
    while ((MAILBOX_STATUS & MAILBOX_FULL) && timeout--)
        asm volatile("nop");
    if (MAILBOX_STATUS & MAILBOX_FULL)
        return 0;

    asm volatile("dmb ish" ::: "memory");
    MAILBOX_WRITE = r;

    timeout = MAILBOX_TIMEOUT;
    while (timeout--) {
        if (MAILBOX_STATUS & MAILBOX_EMPTY) {
            asm volatile("nop");
            continue;
        }

        unsigned int response = MAILBOX_READ;
        if ((response & 0xF) == (channel & 0xF)) {
            asm volatile("dmb ish" ::: "memory");
            return mailbox_buffer[1] == REQUEST_SUCCEED;
        }
    }

    return 0;
}




unsigned int get_board_revision(){
  unsigned int mailbox[7];
  mailbox[0] = 7 * 4; // buffer size in bytes
  mailbox[1] = REQUEST_CODE;
  // tags begin
  mailbox[2] = GET_BOARD_REVISION; // tag identifier
  mailbox[3] = 4; // maximum of request and response value buffer's length.
  mailbox[4] = TAG_REQUEST_CODE;
  mailbox[5] = 0; // value buffer
  // tags end
  mailbox[6] = END_TAG;

//   mailbox_write(MAILBOX_CHANNEL_PROPERTY, (uintptr_t)mailbox);
//   mailbox_read(MAILBOX_CHANNEL_PROPERTY); // message passing procedure call, you should implement it following the 6 steps provided above.

  mailbox_call(8, mailbox, 7);
  return mailbox[5];
}
/**
 * get ARM Memory Base and Size
 */
void get_arm_memory(unsigned int *base, unsigned int *size) {
    volatile unsigned int __attribute__((aligned(16))) mailbox[8];
    mailbox[0] = 8 * 4;   // Buffer size (bytes)
    mailbox[1] = 0;       // Request code (0 for request)
    mailbox[2] = 0x00010005; // Get ARM memory base and size tag
    mailbox[3] = 8;       // Response buffer size (bytes)
    mailbox[4] = 0;       // Request/Response code
    mailbox[5] = 0;       // ARM Memory Base (response will be stored here)
    mailbox[6] = 0;       // ARM Memory Size (response will be stored here)
    mailbox[7] = 0;       // End tag

    // mailbox_write(MAILBOX_CHANNEL_PROPERTY, (uintptr_t)mailbox_buffer);
    // mailbox_read(MAILBOX_CHANNEL_PROPERTY);

    mailbox_call(8, mailbox, 8);

    *base = mailbox[5]; // read Memory Base
    *size = mailbox[6]; // read Memory Size
}

// void reboot() {
//     volatile unsigned int __attribute__((aligned(16))) mailbox[8];

//     mailbox[0] = 8 * 4;  
//     mailbox[1] = 0;     
//     mailbox[2] = 0x00030008;  // Set Power State 
//     mailbox[3] = 8;   
//     mailbox[4] = 8; 
//     mailbox[5] = 0;      // Device ID
//     mailbox[6] = (1 << 0) | (1 << 1);  // ON | NO_WAIT
//     mailbox[7] = 0;


//     mailbox_call(8, mailbox, 8);  // Channel 8 (Tags)
// }
