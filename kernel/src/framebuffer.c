#include "framebuffer.h"
#include "mailbox.h"
#include "uart.h"
#include <stdint.h>



unsigned int fb_width, fb_height, fb_pitch;
unsigned int fb;


int fb_init() {
    volatile unsigned int __attribute__((aligned(16))) fb_mailbox[35];
    fb_mailbox[0] = 35 * 4;   //  Mailbox Buffer 大小
    fb_mailbox[1] = 0;        // Request
    fb_mailbox[2] = 0x00048003; // 螢幕解析度
    fb_mailbox[3] = 8;
    fb_mailbox[4] = 8;
    fb_mailbox[5] = 800;      // 寬 800px
    fb_mailbox[6] = 600;      // 高 600px
    fb_mailbox[7] = 0x00048004; // 虛擬解析度
    fb_mailbox[8] = 8;
    fb_mailbox[9] = 8;
    fb_mailbox[10] = 800;
    fb_mailbox[11] = 600;
    fb_mailbox[12] = 0x00048009; // 色彩深度 (32-bit)
    fb_mailbox[13] = 8;
    fb_mailbox[14] = 8;
    fb_mailbox[15] = 32;
    fb_mailbox[16] = 0x00040001; //  Framebuffer address
    fb_mailbox[17] = 8;
    fb_mailbox[18] = 8;
    fb_mailbox[19] = 0;
    fb_mailbox[20] = 0;
    fb_mailbox[21] = 0; // End Tag

    
    if (mailbox_call(1, fb_mailbox, 35) && fb_mailbox[19] != 0) {
        fb = (fb_mailbox[19] & 0x3FFFFFFF);  // 移除高 2-bit，確保是正確的地址
        uart_puts("Framebuffer initialized successfully at: ");
        uart_put_hex(fb);
        uart_puts("\n");
    } else {
        uart_puts("Framebuffer initialization failed!\n");
        return 0;
    }
    
    return 0;
}

// // for testing
// void fb_put_pixel(int x, int y, uint32_t color) {
//     if (x >= 0 && x < fb_width && y >= 0 && y < fb_height) {
//         fb[y * (fb_pitch / 4) + x] = color;
//     }
// }