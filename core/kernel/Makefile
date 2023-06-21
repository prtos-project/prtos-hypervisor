all: objs

include ../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/core/rules.mk
-include dep.mk

SRCS_C-y = irqs.c ktimer.c objdir.c rsvmem.c setup.c hypercalls.c \
	kthread.c panic.c sched.c
#xef.c
SRCS = $(SRCS_C-y)
OBJS += $(patsubst %.c, %.o, $(SRCS_C-y))


objs: $(OBJS)
	@$(TARGET_LD) $(TARGET_LDFLAGS) -r -o kern.o $(OBJS)

#	$(CC) $(filter-out -pg,$(CFLAGS)) -c gprof.c
