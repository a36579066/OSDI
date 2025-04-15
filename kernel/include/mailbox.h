#ifndef MAILBOX_H
#define MAILBOX_H


#include <type.h>
#include <gpio.h>

#define MAILBOX_BASE    (PERIPHERAL_BASE + 0xB880)

#define MAILBOX_READ    (*(volatile unsigned int*)(MAILBOX_BASE + 0x00))
#define MAILBOX_STATUS  (*(volatile unsigned int*)(MAILBOX_BASE + 0x18))
#define MAILBOX_WRITE   (*(volatile unsigned int*)(MAILBOX_BASE + 0x20))

#define MAILBOX_FULL    0x80000000
#define MAILBOX_EMPTY   0x40000000

#define MAILBOX_CHANNEL_PROPERTY 8 // use Channel 8 to send property messages


void mailbox_write(unsigned int channel, unsigned int data);

unsigned int mailbox_read(unsigned int channel);

int mailbox_call(unsigned char channel, volatile unsigned int *mailbox_buffer, unsigned int size);

unsigned int get_board_revision();

void get_arm_memory(unsigned int *base, unsigned int *size);


// void reboot();



#endif // MAILBOX_H