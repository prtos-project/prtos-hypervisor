#define PL011BASE   0x09000000
#define REG(reg)    (volatile unsigned int *)(PL011BASE + reg)

/* PL011 UART Register Definitions */
#define PL011DR     0x00  // Data Register (Read/Write)
#define PL011FR     0x18  // Flag Register (Read-Only)
#define PL011IBRD   0x24  // Integer Baud Rate Register
#define PL011FBRD   0x28  // Fractional Baud Rate Register
#define PL011LCRH   0x2C  // Line Control Register
#define PL011CR     0x30  // Control Register
#define PL011IMSC   0x38  // Interrupt Mask Set/Clear Register
#define PL011MIS    0x40  // Masked Interrupt Status Register
#define PL011ICR    0x44  // Interrupt Clear Register

/* PL011 Flag Register (FR) Bits */
#define PL011_FR_RXFE (1 << 4) // Receive FIFO empty
#define PL011_FR_TXFF (1 << 5) // Transmit FIFO full
#define PL011_FR_RXFF (1 << 6) // Receive FIFO full
#define PL011_FR_TXFE (1 << 7) // Transmit FIFO empty

/* PL011 Line Control Register (LCRH) Bits */
#define PL011_LCRH_FEN       (1 << 4) // Enable FIFOs
#define PL011_LCRH_WLEN_8BIT (3 << 5) // Set word length to 8 bits

/* PL011 Interrupt Mask Set/Clear (IMSC) Bits */
#define PL011_INTRX_ENABLED  (1 << 4) // Enable receive interrupt
#define PL011_INTTX_ENABLED  (1 << 5) // Enable transmit interrupt

void pl011_putc(char c)
{
    /* Transmit fifo is full */
    while(*REG(PL011FR) & PL011_FR_TXFF) {
        /* release the core */
        asm volatile("yield" ::: "memory");
    }
    *REG(PL011DR) = c;
}

void pl011_init()
{
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

