#include <device_tree.h>
#include <type.h>
#include <string.h>
#include <uart.h>
#include <utils.h>
#include <ramdisk.h>




uint32_t be32_to_cpu(const void *addr) {
    // uint32_t LE = 0;
    uint32_t x = *(const uint32_t *)addr; 
    return ((x >> 24) & 0x000000FF) | 
           ((x >> 8)  & 0x0000FF00) | 
           ((x << 8)  & 0x00FF0000) | 
           ((x << 24) & 0xFF000000);
}


void fdt_traverse(fdt_callback callback) {

  struct fdt_header *hdr = (struct fdt_header *)dtb_addr;
  uart_puts("\ndtb address: ");
  uart_put_hex((uintptr_t)dtb_addr);
  uart_puts("\n");
  
  if (be32_to_cpu(&hdr->magic) != FDT_MAGIC) {
      uart_puts("wrong header!\n");
      return;
  }
  
  uint32_t struct_offset = be32_to_cpu(&(hdr->off_dt_struct));
  uint32_t strings_offset = be32_to_cpu(&hdr->off_dt_strings);
  uint32_t struct_size = be32_to_cpu(&hdr->size_dt_struct);
  uintptr_t struct_ptr = (uintptr_t)dtb_addr + struct_offset;
  uintptr_t strings_ptr = (uintptr_t)dtb_addr + strings_offset;

  uintptr_t ptr = (uintptr_t)struct_ptr;
  uintptr_t end = (uintptr_t)struct_ptr + struct_size;
  
  // const char *node_name = NULL;
  
  while (ptr < end) {
      uint32_t token = be32_to_cpu((char*)ptr);
      ptr += 4;
      
      switch (token) {
          case FDT_BEGIN_NODE:
              // node_name = (const char *)ptr;

              // ptr += (strlen_(node_name) + 4) / 4;  
              // ptr = (uint32_t *)(((uintptr_t)ptr + 3) & ~3);  // 確保 4-byte 對齊
              // uart_put_hex(ptr);
              // uart_send('\n');
              ptr += strlen_((char*)ptr) + 1;
              align(&ptr, 4);
              
              // uart_put_hex(ptr);
              // uart_send('\n');
              // while(1);
              break;

          case FDT_PROP: {
              uint32_t len = be32_to_cpu((char*)ptr);
              ptr += 4;
              uint32_t name_offset = be32_to_cpu((char*)ptr);
              ptr += 4;
              const char *prop_name = (const char *)((uintptr_t)strings_ptr + name_offset);
              void *prop_data = (void*)ptr;
          
              callback(prop_name, prop_data, len);
              ptr += len;
              align(&ptr, 4);

              break;
          }

          case FDT_END_NODE:
          case FDT_NOP:
          case FDT_END:
              break;

          default:
              // uart_puts("Unknown token\n");
              return;
      }
  }
}

// Callback function to find initramfs address
void initramfs_callback(const char *prop_name, const void *prop_data, uint32_t prop_len) {
    // uart_puts(prop_name);
    // uart_puts("\n");
    if (strcmp_(prop_name, "linux,initrd-start") == 0) {
        // set_ramdisk_base((uintptr_t)be32_to_cpu((char*)prop_data));

        RAMDISK_BASE = (char*)(uintptr_t)be32_to_cpu((char*)prop_data);
        uart_puts("Found initramfs start address: ");
        uart_put_hex((uintptr_t)RAMDISK_BASE);
        uart_puts("\n");
    }

    if (strcmp_(prop_name, "linux,initrd-end") == 0) {
        // set_ramdisk_base((uintptr_t)be32_to_cpu((char*)prop_data));
       
        uart_puts("Found initramfs end address: ");
        uart_put_hex((uintptr_t) (char*)(uintptr_t)be32_to_cpu((char*)prop_data));
        uart_puts("\n");
    }
}

