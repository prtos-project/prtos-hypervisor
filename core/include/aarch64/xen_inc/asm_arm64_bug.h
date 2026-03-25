#ifndef __ARM_ARM64_BUG_H__
#define __ARM_ARM64_BUG_H__

#include <xen_stringify.h>
#include <asm_arm64_brk.h>

#define BUG_INSTR "brk " __stringify(BRK_BUG_FRAME_IMM)

#define BUG_FN_REG x0

#endif /* __ARM_ARM64_BUG_H__ */
