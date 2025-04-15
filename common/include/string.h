#ifndef STRING_H
#define STRING_H

#include <stdint.h>
#include "type.h"




char* skip_whitespace(char* s);
char* get_token(char** s_ptr);

int memcmp_(const void *s1, const void *s2, size_t n);
int strcmp_(const char *s1, const char *s2);
int strncmp_(const char *str1, const char *str2, int len);
unsigned long strtol_(const char* str, char** endptr, int base);
unsigned long atoi_(const char *s, int char_size);
void msnprintf_(char *buffer, int size, const char *format, const char *str);
unsigned long strlen_(const char* s);
char* strcpy_(char* dest, const char* src);



#endif
