#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>


extern unsigned int fb_width, fb_height, fb_pitch;
extern unsigned int fb;


int fb_init();


void fb_put_pixel(int x, int y, uint32_t color);

#endif 