all: objs

include ../../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/core/rules.mk

SRCS_ASM += start.S head.S entry.S

SRCS_C-y += processor.c irqs.c panic.c physmm.c vmmap.c setup.c
SRCS_C-y += kthread.c timer.c smp.c traps.c mmu.c hypercalls.c prtos_sbi.c

SRCS=$(SRCS_ASM) $(SRCS_C-y)
OBJS = $(patsubst %.S, %.o, $(SRCS_ASM))
OBJS += $(patsubst %.c, %.o, $(SRCS_C-y))

objs: $(OBJS)
	$(TARGET_LD) $(TARGET_LDFLAGS) -r -o karch.o $(OBJS)
