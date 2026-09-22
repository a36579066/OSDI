#ifndef DEVICE_TREE_H
#define DEVICE_TREE_H

#include <stdint.h>
#include <type.h>

// 5.2
#define FDT_MAGIC 0xd00dfeed
#define FDT_BEGIN_NODE 0x1
#define FDT_END_NODE 0x2
#define FDT_PROP 0x3
#define FDT_NOP 0x4
#define FDT_END 0x9

extern uintptr_t dtb_addr;

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

uint32_t be32_to_cpu(const void *addr);
void initramfs_callback(const char *prop_name, const void *prop_data, uint32_t prop_len);
typedef void (*fdt_callback)(const char* name,const void *data,uint32_t size);
void fdt_traverse(fdt_callback cb);



#endif
