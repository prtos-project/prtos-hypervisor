/*
 * FILE: config.h
 *
 * Config file
 *
 * www.prtos.org
 */

#ifndef _PRTOS_CONFIG_H_
#define _PRTOS_CONFIG_H_

#include __PRTOS_INCFLD(autoconf.h)

#ifdef ASM
#define __ASSEMBLY__
#endif

// bits: (31..24)(23..16)(15..8)(7..0)
// Reserved.VERSION.SUBVERSION.REVISION
#define PRTOS_VERSION (((CONFIG_PRTOS_VERSION & 0xFF) << 16) | ((CONFIG_PRTOS_SUBVERSION & 0xFF) << 8) | (CONFIG_PRTOS_REVISION & 0xFF))

#define CONFIG_KSTACK_SIZE (CONFIG_KSTACK_KB * 1024)

#define CONFIG_MAX_NO_CUSTOMFILES 3

#ifndef CONFIG_NO_CPUS
#define CONFIG_NO_CPUS 1
#endif

#if (CONFIG_ID_STRING_LENGTH & 3)
#error CONFIG_ID_STRING_LENGTH must be a power of 4 (log2(32))
#endif

#ifdef CONFIG_x86
#if (CONFIG_PRTOS_LOAD_ADDR & 0x3FFFFF)
#error prtos must be aligned to a 4MB boundary for a x86 target
#endif
#endif

#if !defined(CONFIG_HWIRQ_PRIO_FBS) && !defined(CONFIG_HWIRQ_PRIO_LBS)
#error "Interrupt priority order must be defined"
#endif

#ifdef __BASE_FILE__
#define __PRTOS_FILE__ __BASE_FILE__
#else
#define __PRTOS_FILE__ __FILE__
#endif

#endif
