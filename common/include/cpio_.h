#ifndef CPIO__H
#define CPIO__H

#include <stdint.h>
// #define RAMDISK_BASE ((void*)(uintptr_t) 0x08000000) // in qemu
// char* RAMDISK_BASE;

#define NULL ((void*)0)
#define CPIO_MAGIC "070701"



struct cpio_newc_header {
    // uses 8-byte	hexadecimal fields for all numbers
    char c_magic[6];    //determine whether this archive is written with little-endian or big-endian integers.
    char c_ino[8];      //determine when two entries refer to the same file.
    char c_mode[8];     //specifies	both the regular permissions and the file type.
    char c_uid[8];      // numeric user id
    char c_gid[8];      // numeric group id
    char c_nlink[8];    // number of links to this file.
    char c_mtime[8];    // Modification time of the file
    char c_filesize[8]; // size of the file
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8]; // number of bytes in the pathname
    char c_check[8];    // always set to zero by writers and ignored by	readers.
};

struct cpio_file {
    char* filename;
    char* data;
    int size;
};

void ls_cpio_archive();
struct cpio_file parse_or_find_file_in_archive(const char* target, int is_find_file);
struct cpio_file find_file_in_archive(const char* target);
void cat_cpio_file(const char* target);
unsigned long cpio_get_size(const char *filename);

#endif