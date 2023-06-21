/*
 * FILE: objdir.h
 *
 * Object directory definition
 *
 * www.prtos.org
 */

#ifndef _PRTOS_OBJDIR_H_
#define _PRTOS_OBJDIR_H_

typedef prtos_u32_t prtos_obj_desc_t;
// Object descriptor:
// VALIDITY  | CLASS| VCPUID | PARTITIONID | ID
//     1     |   7  |   4    |      10     |  10

#define OBJDESC_VALIDITY_MASK (1 << 31)
#define OBJDESC_CLASS_MASK ((1 << 7) - 1)
#define OBJDESC_VCPUID_MASK ((1 << 4) - 1)
#define OBJDESC_NODEID_MASK ((1 << 4) - 1)
#define OBJDESC_PARTITIONID_MASK ((1 << 10) - 1)
#define OBJDESC_ID_MASK ((1 << 10) - 1)
#define OBJDESC_VALIDITY_BIT 31

#define OBJDESC_GET_CLASS(OBJ_DESC) ((OBJ_DESC >> 24) & OBJDESC_CLASS_MASK)
#define OBJDESC_SET_CLASS(OBJ_DESC, class) ((OBJ_DESC & ~(OBJDESC_CLASS_MASK << 24)) | ((class & OBJDESC_CLASS_MASK) << 24))

#define OBJDESC_GET_VCPUID(OBJ_DESC) ((OBJ_DESC >> 20) & OBJDESC_VCPUID_MASK)
#define OBJDESC_SET_VCPUID(OBJ_DESC, vcpu_id) ((OBJ_DESC & ~(OBJDESC_VCPUID_MASK << 20)) | ((vcpu_id & OBJDESC_VCPUID_MASK) << 20))

#define OBJDESC_GET_NODEID(OBJ_DESC) ((OBJ_DESC >> 20) & OBJDESC_NODEID_MASK)
#define OBJDESC_SET_NODEID(OBJ_DESC, node_id) ((OBJ_DESC & ~(OBJDESC_NODEID_MASK << 20)) | ((node_id & OBJDESC_NODEID_MASK) << 20))

#define OBJDESC_GET_PARTITIONID(OBJ_DESC) ((OBJ_DESC >> 10) & OBJDESC_PARTITIONID_MASK)
#define OBJDESC_SET_PARTITIONID(OBJ_DESC, partition_id) ((OBJ_DESC & ~(OBJDESC_PARTITIONID_MASK << 10)) | ((partition_id & OBJDESC_PARTITIONID_MASK) << 10))

#define OBJDESC_GET_ID(OBJ_DESC) (OBJ_DESC & OBJDESC_ID_MASK)
#define OBJDESC_SET_ID(OBJ_DESC, id) ((OBJ_DESC & ~(OBJDESC_ID_MASK)) | ((id & OBJDESC_ID_MASK)))

#define OBJDESC_IS_VALID(OBJ_DESC) (((OBJ_DESC & (1 << 31))) ? 0 : 1)
#define OBJDESC_SET_INVALID(OBJ_DESC) (OBJ_DESC | (1 << 31))
#define OBJDESC_SET_VALID(OBJ_DESC) (OBJ_DESC & ~(1 << 31))

#define OBJDESC_BUILD(class, partition_id, id) \
    (((class & OBJDESC_CLASS_MASK) << 24) | ((0 & OBJDESC_VCPUID_MASK) << 20) | ((partition_id & OBJDESC_PARTITIONID_MASK) << 10) | (id & OBJDESC_ID_MASK))
#define OBJDESC_BUILD_VCPUID(class, vcpu_id, partition_id, id) \
    (((class & OBJDESC_CLASS_MASK) << 24) | ((vcpu_id & OBJDESC_VCPUID_MASK) << 20) | ((partition_id & OBJDESC_PARTITIONID_MASK) << 10) | (id & OBJDESC_ID_MASK))
#define OBJDESC_BUILD_NODEID(class, node_id, partition_id, id) \
    (((class & OBJDESC_CLASS_MASK) << 24) | ((node_id & OBJDESC_VCPUID_MASK) << 20) | ((partition_id & OBJDESC_PARTITIONID_MASK) << 10) | (id & OBJDESC_ID_MASK))

#define OBJ_CLASS_NULL 0
#define OBJ_CLASS_CONSOLE 1
#define OBJ_CLASS_TRACE 2
#define OBJ_CLASS_SAMPLING_PORT 3
#define OBJ_CLASS_QUEUING_PORT 4
#define OBJ_CLASS_MEM 5
#define OBJ_CLASS_HM 6
#define OBJ_CLASS_STATUS 7
#define OBJ_NO_CLASSES 8

#ifdef _PRTOS_KERNEL_

#include <prtosconf.h>
#include <gaccess.h>

typedef prtos_s32_t (*read_obj_op_t)(prtos_obj_desc_t, void *, prtos_u_size_t, prtos_u32_t *);
typedef prtos_s32_t (*write_obj_op_t)(prtos_obj_desc_t, void *, prtos_u_size_t, prtos_u32_t *);
typedef prtos_s32_t (*seek_obj_op_t)(prtos_obj_desc_t, prtos_u_size_t, prtos_u32_t);
typedef prtos_s32_t (*ctrl_obj_op_t)(prtos_obj_desc_t, prtos_u32_t, void *);

struct object {
    read_obj_op_t read;
    write_obj_op_t write;
    seek_obj_op_t seek;
    ctrl_obj_op_t ctrl;
};

extern const struct object *object_table[OBJ_NO_CLASSES];

extern void setup_obj_dir(void);

#define REGISTER_OBJ(_init)                    \
    __asm__(".section .objsetuptab, \"a\"\n\t" \
            ".align 4\n\t"                     \
            ".long " #_init "\n\t"             \
            ".previous\n\t")

#endif

#endif
