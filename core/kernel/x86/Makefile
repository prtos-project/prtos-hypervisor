all: objs

include ../../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/core/rules.mk
-include dep.mk

SRCS_ASM += start.S head.S entry.S

SRCS_C-y += acpi.c mpspec.c hypercalls.c kthread.c pit.c setup.c vmmap.c physmm.c irqs.c panic.c pic.c processor.c tsc.c boot.c hpet.c

SRCS_C-$(CONFIG_APIC) += apic.c lapic_timer.c smp.c

SRCS=$(SRCS_ASM) $(SRCS_C-y)
OBJS = $(patsubst %.S, %.o, $(SRCS_ASM))
OBJS += $(patsubst %.c, %.o, $(SRCS_C-y))

objs: $(OBJS)
	$(TARGET_LD) $(TARGET_LDFLAGS) -r -o karch.o $(OBJS)
