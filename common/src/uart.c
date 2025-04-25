#include <gpio.h>

#include <uart.h>



char uart_rx_buffer[UART_BUF_SIZE];
int uart_rx_head = 0;
int uart_rx_tail = 0;

char uart_tx_buffer[UART_BUF_SIZE];
int uart_tx_head = 0;
int uart_tx_tail = 0;

char shell_input_buffer[UART_BUF_SIZE] = {0};
int shell_input_tail = 0;

void uart_init() {
  // 1. Enable Mini UART (allow access to registers)
  *(volatile unsigned int*)AUX_ENABLES = 1;  

  // 2. Disable TX/RX, no sending/receiving during configuration
  *(volatile unsigned int*)AUX_MU_CNTL_REG = 0;  

  // 3. Disable interrupts (Polling mode)
  *(volatile unsigned int*)AUX_MU_IER_REG = 0;  

  // 4. Set 8-bit transmission format
  *(volatile unsigned int*)AUX_MU_LCR_REG = 3;  

  // 5. Disable flow control (Auto Flow Control)
  *(volatile unsigned int*)AUX_MU_MCR_REG = 0;  

  // 6. Set baud rate to 115200
  *(volatile unsigned int*)AUX_MU_BAUD_REG = 270;  // (250MHz / (8 * 115200)) - 1

  // 7. Set FIFO clear (AUX_MU_IIR_REG = 6)
  *(volatile unsigned int*)AUX_MU_IIR_REG = 6;  

  // 1. Set GPIO14 (TXD1) and GPIO15 (RXD1) to ALT5 mode
  unsigned int r = *(volatile unsigned int*)GPFSEL1;
  r &= ~((7 << 12) | (7 << 15));  // Clear GPIO14, GPIO15
  r |= (2 << 12) | (2 << 15);     // Set ALT5 (Mini UART)
  *(volatile unsigned int*)GPFSEL1 = r;

  // 2. Disable GPIO pull-up/down resistors (to avoid affecting UART transmission)
  *(volatile unsigned int*)GPPUD = 0;
  for (int i = 0; i < 150; i++) asm volatile("nop");
  *(volatile unsigned int*)GPPUDCLK0 = (1 << 14) | (1 << 15);
  for (int i = 0; i < 150; i++) asm volatile("nop");
  *(volatile unsigned int*)GPPUDCLK0 = 0;

  // 8. Enable TX and RX
  *(volatile unsigned int*)AUX_MU_CNTL_REG = 3;
}

void uart_send(unsigned char c) {
    // Wait until the transmitter is ready
    while (!(*(volatile unsigned int*)AUX_MU_LSR_REG & 0x20)); 
    // Send the character
    *(volatile unsigned int*)AUX_MU_IO_REG = c;
    if(c == '\n'){
      while(!((*(volatile unsigned int*)AUX_MU_LSR_REG) & 0x20) ){
          asm volatile("nop");
      }
      *(volatile unsigned int*)AUX_MU_IO_REG = '\r';
    }
  }


void uart_gets(char* buffer, unsigned int max_len) {
    unsigned int index = 0;

    while (1) {
        char c = uart_recv();
        
        if (c == '\r' || c == '\n') {
            buffer[index] = '\0';
            break;
        } 
        else if (c == '\b') {
            if (index > 0) {
                index--;
                buffer[index] = '\0';
            }
        } 
        else if (index < max_len - 1) {
            buffer[index++] = c;
        }
    }
}

char uart_recv() {
  char c;
  // Wait until the receiver has data
  while (!(*(volatile unsigned int*)AUX_MU_LSR_REG & 0x01)); 
  // Read the received character
  c = *(volatile unsigned int*)AUX_MU_IO_REG;

  // Handle ESC (`\x1B`) sequences
  if (c == 0x1B) {  
      while (!(*(volatile unsigned int*)AUX_MU_LSR_REG & 0x01));  
      char second = *(volatile unsigned int*)AUX_MU_IO_REG;
      
      if (second == '[') {  
          while (!(*(volatile unsigned int*)AUX_MU_LSR_REG & 0x01));  
          char third = *(volatile unsigned int*)AUX_MU_IO_REG;

          if (third == 'D') {  // Left Arrow `\x1B[D`
              uart_send('\b');  
              return 0; 
          } 
          else if (third == 'C') {  // Right Arrow `\x1B[C`
              uart_send(27);  // ESC
              uart_send('[');
              uart_send('C');  
              return 0; 
          }
      }
  }

  // Handle Backspace (0x08 or 0x7F)
  if (c == 0x08 || c == 0x7F) {  
      uart_send('\b');  
      uart_send(' ');   
      uart_send('\b');  
      return '\b';  
  }

  // Handle Enter (`\r`, `\n`)
  if (c == '\r' || c == '\n') {
      uart_send('\r');  
      uart_send('\n');  
      return '\n';  
  }

  // Echo the received character
//   uart_send(c);
  return c;
}

unsigned char uart_recv_img(){
    // char r;

    // do{asm volatile("nop");}while(!(*(volatile unsigned int*)AUX_MU_LSR_REG&0x01));
    // r = (char)(*(volatile unsigned int*)AUX_MU_IO_REG);
    // /* convert carriage return to newline */
    // return r=='\r'?'\n':r;
    
    char r;
    while(!((*(volatile unsigned int*)AUX_MU_LSR_REG) & 0x01) ){
        asm volatile("nop");
    }
    //  p.11
    //r =  (char)(mmio_read(AUX_MU_IO_REG));
    r = (char)(*(volatile unsigned int*)AUX_MU_IO_REG);
    /* convert carriage return to newline */
    return r;
}

void uart_puts(const char* str){
    // I thought this 'for' usage can't be in C
    //for(int i = 0; str[i] != '\0'; i++)
    int i;
    for(i = 0; str[i] != '\0'; i++){
        if(str[i] == '\n')
            uart_send('\r');
        uart_send((char)str[i]);
    }
}

void uart_putint(unsigned long long num) {
  char buffer[12]; 
  int i = 0;
  int is_negative = 0;

  if (num == 0) { 
      uart_puts("0");
      return;
  }

  if (num < 0) { 
      is_negative = 1;
      num = -num;
  }

  while (num > 0) { 
      buffer[i++] = '0' + (num % 10);
      num /= 10;
  }

  if (is_negative) {
      buffer[i++] = '-'; 
  }

  buffer[i] = '\0';

  for (int j = 0; j < i / 2; j++) {
      char temp = buffer[j];
      buffer[j] = buffer[i - j - 1];
      buffer[i - j - 1] = temp;
  }

  uart_puts(buffer); 
}


void uart_put_hex(unsigned int value) {
  char hex_digits[] = "0123456789ABCDEF";
  uart_puts("0x");

  // Send each nibble of the value in hexadecimal
  for (int i = 28; i >= 0; i -= 4) {
      uart_send(hex_digits[(value >> i) & 0xF]);
  }
}

void uart_putc(unsigned char c){
    while(!((*(volatile unsigned int*)AUX_MU_LSR_REG) & 0x20) ){
        asm volatile("nop");
    }
    *(volatile unsigned int*)AUX_MU_IO_REG = c;
    if(c == '\n'){
        while(!((*(volatile unsigned int*)AUX_MU_LSR_REG) & 0x20) ){
            asm volatile("nop");
        }
        *(volatile unsigned int*)AUX_MU_IO_REG = '\r';
    }
}

void uart_b2x_64(unsigned long long num) {
    char hex_string[17];  // 16 位十六進制數字 + 結尾 '\0'
    int i;
    
    for (i = 15; i >= 0; i--) {
        int digit = num & 0xF;  // 取得最低 4 bits（16 進制一位）
        if (digit < 10)
            hex_string[i] = '0' + digit;
        else
            hex_string[i] = 'A' + (digit - 10);
        num >>= 4;  // 右移 4 位，取得下一個 16 進制數字
    }

    hex_string[16] = '\0';  // 字串結尾
    uart_puts("0x");
    uart_puts(hex_string);
}

