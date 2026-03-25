#ifndef __XEN_ERRNO_H__
#define __XEN_ERRNO_H__

#ifndef __ASSEMBLY__

#define XEN_ERRNO(name, value) name = (value),
enum {
#include <public_errno.h>
};

#else /* !__ASSEMBLY__ */

#define XEN_ERRNO(name, value) .equ name, value
#include <public_errno.h>

#endif /* __ASSEMBLY__ */

#endif /*  __XEN_ERRNO_H__ */
