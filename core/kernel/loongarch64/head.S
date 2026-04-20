/*
 * FILE: head.S
 *
 * LoongArch 64-bit early DMW (Direct Mapped Window) setup
 *
 * LoongArch uses DMW for direct physical-to-virtual address mapping,
 * which is simpler than page table setup. DMW configuration is done
 * in start.S; this file provides helper routines.
 *
 * http://www.prtos.org/
 */

#include <linkage.h>
#include <arch/paging.h>
#include <arch/prtos_def.h>

.section .boot.text, "ax"

/*
 * setup_dmw - Configure Direct Mapped Windows
 *
 * Called from start.S. DMW0 provides uncached access,
 * DMW1 provides cached access.
 */
.globl setup_dmw
.type setup_dmw, @function
setup_dmw:
    /* DMW0: 0x8000000000000000 → PA (uncached, PLV0 only) */
    li.d    $t0, 0x8000000000000001
    csrwr   $t0, 0x180

    /* DMW1: 0x9000000000000000 → PA (cached coherent, PLV0 only) */
    li.d    $t0, 0x9000000000000011
    csrwr   $t0, 0x181

    jirl    $zero, $ra, 0
