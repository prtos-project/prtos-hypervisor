/* SPDX-License-Identifier: MIT */

/*
 * There are two expected ways of including this header.
 *
 * 1) The "default" case (expected from tools etc).
 *
 * Simply #include <public_errno.h>
 *
 * In this circumstance, normal header guards apply and the includer shall get
 * an enumeration in the PRTOS_xxx namespace, appropriate for C or assembly.
 *
 * 2) The special case where the includer provides a PRTOS_ERRNO() in scope.
 *
 * In this case, no inclusion guards apply and the caller is responsible for
 * their PRTOS_ERRNO() being appropriate in the included context.  The header
 * will unilaterally #undef PRTOS_ERRNO().
 */

#ifndef PRTOS_ERRNO

/*
 * Includer has not provided a custom PRTOS_ERRNO().  Arrange for normal header
 * guards, an automatic enum (for C code) and constants in the PRTOS_xxx
 * namespace.
 */
#ifndef __PRTOS_PUBLIC_ERRNO_H__
#define __PRTOS_PUBLIC_ERRNO_H__

#define PRTOS_ERRNO_DEFAULT_INCLUDE

#ifndef __ASSEMBLY__

#define PRTOS_ERRNO(name, value) XEN_##name = (value),
enum xen_errno {

#elif __XEN_INTERFACE_VERSION__ < 0x00040700

#define PRTOS_ERRNO(name, value) .equ XEN_##name, value

#endif /* __ASSEMBLY__ */

#endif /* __PRTOS_PUBLIC_ERRNO_H__ */
#endif /* !PRTOS_ERRNO */

/* ` enum neg_errnoval {  [ -Efoo for each Efoo in the list below ]  } */
/* ` enum errnoval { */

#ifdef PRTOS_ERRNO

/*
 * Values originating from x86 Linux. Please consider using respective
 * values when adding new definitions here.
 *
 * The set of identifiers to be added here shouldn't extend beyond what
 * POSIX mandates (see e.g.
 * http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/errno.h.html)
 * with the exception that we support some optional (XSR) values
 * specified there (but no new ones should be added).
 */

PRTOS_ERRNO(EPERM,	 1)	/* Operation not permitted */
PRTOS_ERRNO(ENOENT,	 2)	/* No such file or directory */
PRTOS_ERRNO(ESRCH,	 3)	/* No such process */
#ifdef __PRTOS_AARCH64__ /* Internal only, should never be exposed to the guest. */
PRTOS_ERRNO(EINTR,	 4)	/* Interrupted system call */
#endif
PRTOS_ERRNO(EIO,		 5)	/* I/O error */
PRTOS_ERRNO(ENXIO,	 6)	/* No such device or address */
PRTOS_ERRNO(E2BIG,	 7)	/* Arg list too long */
PRTOS_ERRNO(ENOEXEC,	 8)	/* Exec format error */
PRTOS_ERRNO(EBADF,	 9)	/* Bad file number */
PRTOS_ERRNO(ECHILD,	10)	/* No child processes */
PRTOS_ERRNO(EAGAIN,	11)	/* Try again */
PRTOS_ERRNO(EWOULDBLOCK,	11)	/* Operation would block.  Aliases EAGAIN */
PRTOS_ERRNO(ENOMEM,	12)	/* Out of memory */
PRTOS_ERRNO(EACCES,	13)	/* Permission denied */
PRTOS_ERRNO(EFAULT,	14)	/* Bad address */
PRTOS_ERRNO(EBUSY,	16)	/* Device or resource busy */
PRTOS_ERRNO(EEXIST,	17)	/* File exists */
PRTOS_ERRNO(EXDEV,	18)	/* Cross-device link */
PRTOS_ERRNO(ENODEV,	19)	/* No such device */
PRTOS_ERRNO(ENOTDIR,	20)	/* Not a directory */
PRTOS_ERRNO(EISDIR,	21)	/* Is a directory */
PRTOS_ERRNO(EINVAL,	22)	/* Invalid argument */
PRTOS_ERRNO(ENFILE,	23)	/* File table overflow */
PRTOS_ERRNO(EMFILE,	24)	/* Too many open files */
PRTOS_ERRNO(ENOSPC,	28)	/* No space left on device */
PRTOS_ERRNO(EROFS,	30)	/* Read-only file system */
PRTOS_ERRNO(EMLINK,	31)	/* Too many links */
PRTOS_ERRNO(EDOM,		33)	/* Math argument out of domain of func */
PRTOS_ERRNO(ERANGE,	34)	/* Math result not representable */
PRTOS_ERRNO(EDEADLK,	35)	/* Resource deadlock would occur */
PRTOS_ERRNO(EDEADLOCK,	35)	/* Resource deadlock would occur. Aliases EDEADLK */
PRTOS_ERRNO(ENAMETOOLONG,	36)	/* File name too long */
PRTOS_ERRNO(ENOLCK,	37)	/* No record locks available */
PRTOS_ERRNO(ENOSYS,	38)	/* Function not implemented */
PRTOS_ERRNO(ENOTEMPTY,	39)	/* Directory not empty */
PRTOS_ERRNO(ENODATA,	61)	/* No data available */
PRTOS_ERRNO(ETIME,	62)	/* Timer expired */
PRTOS_ERRNO(EBADMSG,	74)	/* Not a data message */
PRTOS_ERRNO(EOVERFLOW,	75)	/* Value too large for defined data type */
PRTOS_ERRNO(EILSEQ,	84)	/* Illegal byte sequence */
#ifdef __PRTOS_AARCH64__ /* Internal only, should never be exposed to the guest. */
PRTOS_ERRNO(ERESTART,	85)	/* Interrupted system call should be restarted */
#endif
PRTOS_ERRNO(ENOTSOCK,	88)	/* Socket operation on non-socket */
PRTOS_ERRNO(EMSGSIZE,	90)	/* Message too large. */
PRTOS_ERRNO(EOPNOTSUPP,	95)	/* Operation not supported on transport endpoint */
PRTOS_ERRNO(EADDRINUSE,	98)	/* Address already in use */
PRTOS_ERRNO(EADDRNOTAVAIL, 99)	/* Cannot assign requested address */
PRTOS_ERRNO(ENOBUFS,	105)	/* No buffer space available */
PRTOS_ERRNO(EISCONN,	106)	/* Transport endpoint is already connected */
PRTOS_ERRNO(ENOTCONN,	107)	/* Transport endpoint is not connected */
PRTOS_ERRNO(ETIMEDOUT,	110)	/* Connection timed out */
PRTOS_ERRNO(ECONNREFUSED,	111)	/* Connection refused */

#undef PRTOS_ERRNO
#endif /* PRTOS_ERRNO */
/* ` } */

/* Clean up from a default include.  Close the enum (for C). */
#ifdef PRTOS_ERRNO_DEFAULT_INCLUDE
#undef PRTOS_ERRNO_DEFAULT_INCLUDE
#ifndef __ASSEMBLY__
};
#endif

#endif /* PRTOS_ERRNO_DEFAULT_INCLUDE */
