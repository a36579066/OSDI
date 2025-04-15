#include "cpio.h"
#include "uart.h"
#include "string.h"
#include "utils.h"
#include "ramdisk.h"


static const char* strip_dot_slash(const char* filename) {
    if (strncmp_(filename, "./", 2) == 0) {
        return filename + 2;
    }
    return filename;
}

struct cpio_file parse_or_find_file_in_archive(const char* target, int is_find_file) {
    struct cpio_file file = {0};
    char* ptr = (char*)RAMDISK_BASE;

    while (1) {
        struct cpio_newc_header* header = (struct cpio_newc_header*)ptr;

        if (memcmp_(header->c_magic, CPIO_MAGIC, 6) != 0) {
            uart_puts("not found\n");
            break;
        }

        unsigned long namesize = atoi_(header->c_namesize, (int)sizeof(header->c_namesize));
        unsigned long filesize = atoi_(header->c_filesize, (int)sizeof(header->c_namesize));

        char* filename = ptr + sizeof(struct cpio_newc_header);

        const char* normalized_name = strip_dot_slash(filename);
        if (strcmp_(normalized_name, "TRAILER!!!") == 0) {
            break;
        }

        if (is_find_file) {
            if (strcmp_(normalized_name, target) == 0) {
                // Found the target file, return the file details
                char* filedata = filename + namesize;
                filedata = (char*) (((uintptr_t)filedata + 3) & ~3);
                file.filename = filename;
                file.data = filedata;
                file.size = filesize;
                return file;  // Found the file, return it
            }
        } else {
            uart_puts(normalized_name);
            uart_puts("\n");
        }

        // Move to the next file in the archive
        unsigned long sizeof_header_path = sizeof(struct cpio_newc_header) + namesize;
        align(&sizeof_header_path, 4); 
        align(&filesize, 4);  
        ptr += sizeof_header_path + filesize;
    }

    // If not finding a file, return an empty file struct (meaning "not found")
    return file;
}


// For "ls" command
void ls_cpio_archive() {
    parse_or_find_file_in_archive(NULL, 0);
}


struct cpio_file find_file_in_archive(const char* target) {
    return parse_or_find_file_in_archive(target, 1);  // Pass 1 for is_find_file to search for target
}

// For "cat command"
void cat_cpio_file(const char* target) {
    struct cpio_file file = find_file_in_archive(target);
    if (file.data == NULL) {
        uart_puts("File not found: ");
        uart_puts(target);
        uart_puts("\n");
        return;
    }
    
    for (int i = 0; i < file.size; i++) {
        uart_send(file.data[i]);
    }
}

// void* load_user_program(const char* filename) {
//     struct cpio_file file = parse_or_find_file_in_archive(filename, 1);  // 搜尋檔案
//     if (file.size == 0) {
//         uart_puts("Error: File not found in initramfs.\n");
//         return 0;
//     }

//     void* dest = (void*)USER_PROGRAM_LOAD_ADDR;
//     memcpy(dest, file.data, file.size);  // 複製程式到記憶體

//     uart_puts("User program loaded successfully.\n");
//     return dest;
// }
