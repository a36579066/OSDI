#include "string.h"

/*  memcmp：compare two memory chuncks */
int memcmp_(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char*) s1;
    const unsigned char *p2 = (const unsigned char*) s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i])
            return (int)p1[i] - (int)p2[i];
    }
    return 0;
}

/* strcmp：compare two string */
int strcmp_(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
}

int strncmp_(const char *str1, const char *str2, int len){
    int i;
        
    for(i = 0; i < len; i++){
        if(str1[i] > str2[i])return 1;
        else if(str1[i] < str2[i]) return -1;
    }

    return 0;
}

/* strtol：string to long, mainly support heximal */
unsigned long strtol_(const char* str, char** endptr, int base) {
    unsigned long result = 0;
    const char* ptr = str;

    while (*ptr == ' ' || *ptr == '\t') {
        ptr++;
    }

    int sign = 1;
    if (*ptr == '-') {
        sign = -1;
        ptr++;
    } else if (*ptr == '+') {
        ptr++;
    }

    if (base == 16) {
        if (ptr[0] == '0' && (ptr[1] == 'x' || ptr[1] == 'X')) {
            ptr += 2;
        }
    }

    while (*ptr) {
        int digit;
        char c = *ptr;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'z') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'Z') {
            digit = c - 'A' + 10;
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        result = result * base + digit;
        ptr++;
    }

    if (endptr) {
        *endptr = (char*)ptr;
    }
    return sign * result;
}


unsigned long atoi_(const char *s, int char_size) {
	unsigned long num = 0;
	for (int i = 0; i < char_size; i++) {
			num = num * 16;
			if (*s >= '0' && *s <= '9') {
					num += (*s - '0');
			} else if (*s >= 'A' && *s <= 'F') {
					num += (*s - 'A' + 10);
			} else if (*s >= 'a' && *s <= 'f') {
					num += (*s - 'a' + 10);
			}
			s++;
	}
	return num;
}

void msnprintf_(char *buffer, int size, const char *format, const char *str) {
    if (size <= 0) return;
    int i = 0, j = 0;

    // 遍歷 format 字串
    while (format[i] != '\0' && j < size - 1) {
        if (format[i] == '%' && format[i + 1] == 's') {
            // 插入 str 字串
            int k = 0;
            while (str[k] != '\0' && j < size - 1) {
                buffer[j++] = str[k++];
            }
            i += 2;  // 跳過 "%s"
        } else {
            buffer[j++] = format[i++];
        }
    }
    buffer[j] = '\0';
}


char* skip_whitespace(char* s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

char* get_token(char** s_ptr) {
    char* s = *s_ptr;
    s = skip_whitespace(s);
    if (*s == '\0') return 0;
    char* token = s;
    while (*s != '\0' && *s != ' ' && *s != '\t') {
        s++;
    }
    if (*s != '\0') {
        *s = '\0';
        s++;
    }
    *s_ptr = s;
    return token;
}

unsigned long strlen_(const char* s) {
    int len = 0;
    while(s[len] != '\0') {
        len++;
    }
    return len;
}

char* strcpy_(char* dest, const char* src) {
    char* original = dest;
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';  // 記得補上 null terminator
    return original;
}


// unsigned long utils_strlen(const char *s) {
//     size_t i = 0;
// 	while (s[i]) i++;
// 	return i+1;
// }




