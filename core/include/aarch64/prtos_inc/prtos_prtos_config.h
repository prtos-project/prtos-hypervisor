/******************************************************************************
 * config.h
 * 
 * A Linux-style configuration list.
 */

#ifndef __PRTOS_CONFIG_H__
#define __PRTOS_CONFIG_H__

#ifdef CPPCHECK
#include <generated/compiler-def.h>
#endif

#include <prtos_kconfig.h>

#ifndef __ASSEMBLY__
#include <prtos_compiler.h>

#if defined(CONFIG_ENFORCE_UNIQUE_SYMBOLS) || defined(__clang__)
# define EMIT_FILE asm ( "" )
#else
# define EMIT_FILE asm ( ".file \"" __FILE__ "\"" )
#endif

#endif

#include <asm_prtos_config.h>

#define EXPORT_SYMBOL(var)

/*
 * The following log levels are as follows:
 *
 *   PRTOSLOG_ERR: Fatal errors, either PRTOS, Guest or Dom0
 *               is about to crash.
 *
 *   PRTOSLOG_WARNING: Something bad happened, but we can recover.
 *
 *   PRTOSLOG_INFO: Interesting stuff, but not too noisy.
 *
 *   PRTOSLOG_DEBUG: Use where ever you like. Lots of noise.
 *
 *
 * Since we don't trust the guest operating system, we don't want
 * it to allow for DoS by causing the HV to print out a lot of
 * info, so where ever the guest has control of what is printed
 * we use the PRTOSLOG_GUEST to distinguish that the output is
 * controlled by the guest.
 *
 * To make it easier on the typing, the above log levels all
 * have a corresponding _G_ equivalent that appends the
 * PRTOSLOG_GUEST. (see the defines below).
 *
 */
#define PRTOSLOG_ERR     "<0>"
#define PRTOSLOG_WARNING "<1>"
#define PRTOSLOG_INFO    "<2>"
#define PRTOSLOG_DEBUG   "<3>"

#define PRTOSLOG_GUEST   "<G>"

#define PRTOSLOG_G_ERR     PRTOSLOG_GUEST PRTOSLOG_ERR
#define PRTOSLOG_G_WARNING PRTOSLOG_GUEST PRTOSLOG_WARNING
#define PRTOSLOG_G_INFO    PRTOSLOG_GUEST PRTOSLOG_INFO
#define PRTOSLOG_G_DEBUG   PRTOSLOG_GUEST PRTOSLOG_DEBUG

/*
 * Some code is copied directly from Linux.
 * Match some of the Linux log levels to PRTOS.
 */
#define KERN_ERR       PRTOSLOG_ERR
#define KERN_CRIT      PRTOSLOG_ERR
#define KERN_EMERG     PRTOSLOG_ERR
#define KERN_WARNING   PRTOSLOG_WARNING
#define KERN_NOTICE    PRTOSLOG_INFO
#define KERN_INFO      PRTOSLOG_INFO
#define KERN_DEBUG     PRTOSLOG_DEBUG

/* Linux 'checker' project. */
#define __iomem
#define __user
#define __force
#define __bitwise

#define KB(_kb)     (_AC(_kb, ULL) << 10)
#define MB(_mb)     (_AC(_mb, ULL) << 20)
#define GB(_gb)     (_AC(_gb, ULL) << 30)

/* allow existing code to work with Kconfig variable */
#define NR_CPUS CONFIG_NR_CPUS

#ifndef CONFIG_DEBUG
#define NDEBUG
#endif

#ifndef ZERO_BLOCK_PTR
/* Return value for zero-size allocation, distinguished from NULL. */
#define ZERO_BLOCK_PTR ((void *)-1L)
#endif

#endif /* __PRTOS_CONFIG_H__ */
