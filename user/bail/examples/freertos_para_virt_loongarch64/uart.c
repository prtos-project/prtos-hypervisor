/*
 * uart.c - UART output for FreeRTOS on PRTOS LoongArch64 (para-virt)
 *
 * Uses PRTOS write_object hypercall (nr=12) with console object descriptor.
 * OBJDESC_BUILD(OBJ_CLASS_CONSOLE=1, partition_id=0, id=0) = 0x01000000
 */

#include "uart.h"

/* PRTOS hypercall: write_object (nr=12)
 * a0 = 12 (write_object_nr)
 * a1 = obj_desc (0x01000000 = console of partition 0)
 * a2 = buffer pointer
 * a3 = length
 * a4 = flags (0)
 * a7 = 0 (para-virt indicator)
 */
#define WRITE_OBJECT_NR   12
#define CONSOLE_OBJ_DESC  0x01000000UL  /* OBJ_CLASS_CONSOLE=1, partId=0, id=0 */

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
	/* No initialization needed for hypercall-based console */
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

void uart_puthex(unsigned long v)
{
	char buf[19]; /* "0x" + 16 hex digits + NUL */
	int i;
	buf[0] = '0';
	buf[1] = 'x';
	for (i = 0; i < 16; i++) {
		int nib = (v >> (60 - i*4)) & 0xF;
		buf[2+i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
	}
	buf[18] = '\0';
	uart_puts(buf);
}
