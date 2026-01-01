#include <drivers/pl011.h>
#include <arch/layout.h>

#define REG(reg) (volatile unsigned int *)(PL011BASE + reg)

/* PL011 register offsets from base address */
#define PL011DR 0x00    // Data Register
#define PL011FR 0x18    // Flag Register
#define PL011IBRD 0x24  // Integer Baud Rate Register
#define PL011FBRD 0x28  // Fractional Baud Rate Register
#define PL011LCRH 0x2c  // Line Control Register
#define PL011CR 0x30    // Control Register
#define PL011IMSC 0x38  // Interrupt Mask Set/Clear Register,
#define PL011MIS 0x40   // Masked Interrupt Status Register
#define PL011ICR 0x44   // Interrupt Clear Register

#define PL011_FR_RXFE (1 << 4)  // Recieve fifo empty
#define PL011_FR_TXFF (1 << 5)  // Transmit fifo full
#define PL011_FR_RXFF (1 << 6)  // Recieve fifl fulll
#define PL011_FR_TXFE (1 << 7)  // Transmit fifo empty

#define PL011_LCRH_FEN (1 << 4)        // Enable/Disable Fifos
#define PL011_LCRH_WLEN_8BIT (3 << 5)  // Word length - 8Bit

#define PL011_INTRX_ENABLED (1 << 4)  // Receive interrupt status
#define PL011_INTTX_ENABLED (1 << 5)  // Transmit interrupt status

void pl011_putc(char c) {
    /* Transmit fifo is full */
    while (*REG(PL011FR) & PL011_FR_TXFF) {
        /* release the core */
        asm volatile("yield" ::: "memory");
    }
    *REG(PL011DR) = c;
}

void pl011_puts(char *s) {
    char c;
    while ((c = *s++)) {
        pl011_putc(c);
    }
}

int pl011_getc() {
    /* Recieve buffer is not empty */
    if (*REG(PL011FR) & PL011_FR_RXFE) {
        return -1;
    } else {
        return *REG(PL011DR);
    }
}

void pl011_irq_handler() {
    int status = *REG(PL011MIS);
    if (status & (1 << 4)) {
        for (;;) {
            int c = pl011_getc();
            if (c < 0) break;
            pl011_putc(c);
        }
    }
    *REG(PL011ICR) = (1 << 4);
}

void pl011_init() {
    /* Disable the Uart */
    *REG(PL011CR) = 0;
    /* Clear all interrupt mask */
    *REG(PL011IMSC) = 0;
    /* Eanble Fifos and set 8 data bits transmitted or received in a frame */
    *REG(PL011IMSC) = PL011_LCRH_FEN | PL011_LCRH_WLEN_8BIT;
    /* Enable Receive/Transmit */
    *REG(PL011CR) = 0x301;
    /* Receive interrupt mask */
    *REG(PL011IMSC) = (1 << 4);
}
