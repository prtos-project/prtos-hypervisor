#ifndef __LAYOUT_H__
#define __LAYOUT_H__
//#include <asm/xen_autoconf.h>

#define NCPU 4
/* 4K size */
#define SZ_4K 0x00001000
#define PAGESIZE SZ_4K
/* 虚拟管理器的物理内存加载地址 */
#define HIMAGE_VADDR 0x40200000

#define PHYBASE 0x40000000
#define PHYSIZE (128 * 1024 * 1024)
#define PHYEND (PHYBASE + PHYSIZE)

/* PL011设备寄存器的基地址 */
#define PL011BASE 0xA0000A00000  // 0x09000000  FIXME: here just a WA for PL011 virtual addr, will refine later

#define GICD_BASE 0x08000000
#define GICD_SIZE 0x10000
#define GICR_BASE 0x080a0000
#define GICR_SIZE 0x80000 /* 0x2000 for one core */
#define GICR_STRIDE 0x20000

#endif
