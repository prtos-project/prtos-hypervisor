/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * xen-compat.h
 *
 * Guest OS interface to PRTOS.  Compatibility layer.
 *
 * Copyright (c) 2006, Christian Limpach
 */

#ifndef __PRTOS_PUBLIC_XEN_COMPAT_H__
#define __PRTOS_PUBLIC_XEN_COMPAT_H__

#define __XEN_LATEST_INTERFACE_VERSION__ 0x00041300

#if defined(__PRTOS_AARCH64__) || defined(__PRTOS_TOOLS__)
/* PRTOS is built with matching headers and implements the latest interface. */
#define __XEN_INTERFACE_VERSION__ __XEN_LATEST_INTERFACE_VERSION__
#elif !defined(__XEN_INTERFACE_VERSION__)
/* Guests which do not specify a version get the legacy interface. */
#define __XEN_INTERFACE_VERSION__ 0x00000000
#endif

#if __XEN_INTERFACE_VERSION__ > __XEN_LATEST_INTERFACE_VERSION__
#error "These header files do not support the requested interface version."
#endif

#define COMPAT_FLEX_ARRAY_DIM PRTOS_FLEX_ARRAY_DIM

#endif /* __PRTOS_PUBLIC_XEN_COMPAT_H__ */
