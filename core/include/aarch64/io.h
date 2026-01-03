/*
 * FILE: io.h
 *
 * Port's related functions
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_IO_H_
#define _PRTOS_ARCH_IO_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#define io_delay() // __asm__ __volatile__("pushl %eax; inb $0x80,%al; inb $0x80,%al; popl %eax")

#define out_byte(val, port) // __asm__ __volatile__("outb %0, %%dx\n\t" ::"a"((prtos_u8_t)(val)), "d"((prtos_u16_t)(port)))

#define out_word(val, port) // __asm__ __volatile__("outw %0, %%dx\n\t" ::"a"((prtos_u16_t)(val)), "d"((prtos_u16_t)(port)))

#define out_line(val, port) // __asm__ __volatile__("outl %0, %%dx\n\t" ::"a"((prtos_u32_t)(val)), "d"((prtos_u16_t)(port)))

#define out_byte_port(val, port) \
    ({                           \
        out_byte(val, port);     \
        io_delay();              \
        io_delay();              \
        io_delay();              \
    })

#define in_byte(port)  

#define in_word(port)  

#define in_line(port)   

#define in_byte_port(port)  
#endif
