/*
 * FILE: assert.h
 *
 * Assert definition
 *
 * www.prtos.org
 */

#ifndef _XAL_ASSERT_H_
#define _XAL_ASSERT_H_

#ifdef _DEBUG_

#include <xal.h>

#define assert(exp) ((exp) ? 0 : line_halt(__FILE__ ":%u: failed assertion `" #exp "'\n", __LINE__))

#define ASSERT(exp) assert(exp)

#else

#define assert(exp) ((void)0)

#endif

#endif
