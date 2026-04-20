/*
 * uart.c - PRTOS hypercall-based UART output for FreeRTOS in mixed-OS demo.
 *
 * In the mixed-OS config the physical UART is assigned to the Linux
 * partition, so FreeRTOS uses the PRTOS write_object hypercall to write
 * to the console through the hypervisor.
 */

#include "uart.h"

/* PRTOS hypercall: write_object (nr=12)
 * $a0 = 12 (write_object_nr)
 * $a1 = obj_desc (0x01000400 = console of partition 1)
 * $a2 = buffer pointer
 * $a3 = length
 * $a4 = flags (0)
 * $a7 = 0 (para-virt indicator)
 */
#define WRITE_OBJECT_NR  12

/* Object descriptor: OBJ_CLASS_CONSOLE=1, partition_id=1, id=0
 * Format: [31:valid][30:24:class][23:20:vcpuid][19:10:partid][9:0:id]
 * = (1 << 24) | (0 << 20) | (1 << 10) | 0 = 0x01000400 */
#define CONSOLE_OBJ_DESC  0x01000400UL

static int prtos_write_object(const char *buf, unsigned long len)
{
        register unsigned long r_a0 __asm__("$a0") = WRITE_OBJECT_NR;
        register unsigned long r_a1 __asm__("$a1") = CONSOLE_OBJ_DESC;
        register unsigned long r_a2 __asm__("$a2") = (unsigned long)buf;
        register unsigned long r_a3 __asm__("$a3") = len;
        register unsigned long r_a4 __asm__("$a4") = 0;
        register unsigned long r_a7 __asm__("$a7") = 0;
        __asm__ volatile("syscall 0"
                : "+r"(r_a0)
                : "r"(r_a1), "r"(r_a2), "r"(r_a3), "r"(r_a4), "r"(r_a7)
                : "memory");
        return (int)r_a0;
}

void uart_init(void)
{
        /* No init needed for PRTOS console */
}

void uart_putc(char c)
{
        prtos_write_object(&c, 1);
}

void uart_puts(const char *s)
{
        unsigned long len = 0;
        const char *p = s;
        while (*p++) len++;
        if (len > 0)
                prtos_write_object(s, len);
}

void uart_puthex(unsigned long n)
{
        char buf[19]; /* "0x" + 16 hex digits + NUL */
        int i;
        buf[0] = '0';
        buf[1] = 'x';
        for (i = 0; i < 16; i++) {
                int nib = (n >> (60 - i*4)) & 0xF;
                buf[2+i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
        }
        buf[18] = '\0';
        uart_puts(buf);
}
