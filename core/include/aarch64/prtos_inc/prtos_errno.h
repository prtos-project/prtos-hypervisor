#ifndef __PRTOS_ERRNO_H__
#define __PRTOS_ERRNO_H__

#ifndef __ASSEMBLY__

#define PRTOS_ERRNO(name, value) name = (value),
enum {
#include <public_errno.h>
};

#else /* !__ASSEMBLY__ */

#define PRTOS_ERRNO(name, value) .equ name, value
#include <public_errno.h>

#endif /* __ASSEMBLY__ */

#endif /*  __PRTOS_ERRNO_H__ */
