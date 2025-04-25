#include "utils.h"


void align(void *size, unsigned int s) {
	unsigned long* x = (unsigned long*) size;
	unsigned long mask = s-1;
	*x = ((*x) + mask) & (~mask);
}

uintptr_t align_up(uintptr_t x, unsigned int s) {
	uintptr_t mask = s - 1;
	return (x + mask) & ~mask;
}

uintptr_t align_down(uintptr_t x, unsigned int s) {
	uintptr_t mask = s - 1;
	return x & ~mask;
}


int align_mem_offset(void* i, unsigned int align){
	// unsigned long long seemed not work properly?
	uintptr_t l = (uintptr_t)i;
	return ((align - (l % align) ) % align);
}

void* memcpy_(void *dest, const void *src, uintptr_t n) {
	char *d = (char *)dest;
	const char *s = (const char *)src;
	while (n--) {
			*d++ = *s++;
	}
	return dest;
}

void memset_(void *dest, int c, unsigned int count) {
	unsigned char *ptr = dest;
	while (count--) {
			*ptr++ = (unsigned char)c;
	}
	return;
}

void memzero(uint64_t *dest, uint64_t size) {
	unsigned char *ptr = (unsigned char *)dest;
	while (size >= 8) {
			*(uint64_t *)ptr = 0; // 清零 8 個位元組
			ptr += 8;
			size -= 8;
	}
	while (size > 0) {
			*ptr++ = 0; // 清零剩餘的位元組
			size--;
	}
}