#include "cpio_.h"
#include "uart.h"
#include "string.h"
#include "utils.h"
#include "ramdisk.h"
#include "type.h"

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
                // uart_puts("found file: ");
                // uart_puts(file.filename);NL;
                // uart_putint(file.size);NL;
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
}

unsigned long cpio_get_size(const char *filename) {
    const char *target = strip_dot_slash(filename);
    char *ptr = (char *)RAMDISK_BASE;

    while (1) {
        struct cpio_newc_header *hdr = (struct cpio_newc_header *)ptr;
        // magic check
        if (memcmp_(hdr->c_magic, CPIO_MAGIC, 6) != 0) {
            break;
        }

        // 解析 namesize/filesize（都是 ASCII hex）
        unsigned long namesz = atoi_(hdr->c_namesize, 8);
        unsigned long filesz = atoi_(hdr->c_filesize, 8);

        // 文件名紧跟 header 之后
        char *name = ptr + sizeof(*hdr);
        const char *nm = strip_dot_slash(name);

        // 如果到尾标就退出
        if (strcmp_(nm, "TRAILER!!!") == 0) {
            break;
        }

        // 匹配成功，返回 size
        if (strcmp_(nm, target) == 0) {
            return filesz;
        }

        // 否则跳到下一个 entry
        unsigned long offset = sizeof(*hdr) + namesz;
        align(&offset, 4);  // 4‐byte 对齐
        align(&filesz, 4);
        ptr += offset + filesz;
    }

    // 没找到
    return 0;
}