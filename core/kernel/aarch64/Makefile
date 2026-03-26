all: objs

include ../../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/core/rules.mk

SRCS_ASM += start.S head.S entry.S

# PRTOS native aarch64 source files
SRCS_C-y += processor.c irqs.c panic.c physmm.c vmmap.c setup.c
SRCS_C-y += kthread.c armv8_timer.c smp.c traps.c mmu.c hypercalls.c
SRCS_C-y += prtos_vgic.c prtos_psci.c prtos_virtio_console.c

# PRTOS hypervisor core (consolidated)
SRCS_C-y += prtos_arch.c prtos_mmu.c prtos_irq.c prtos_sched.c prtos_time.c
SRCS_C-y += prtos_smp.c prtos_dt.c prtos_drivers.c prtos_events.c
SRCS_C-y += prtos_lib.c prtos_xsm.c prtos_misc.c
SRCS_C-y += vcpreg.c vsysreg.c page_alloc.c fdt_sw.c rangeset.c tasklet.c dummy.c silo.c vpl011.c

# ASM utility files (ARM operations)
SRCS_ASM += bpi.S clear_page.S debug.S memchr.S smc.S strnlen.S

SRCS=$(SRCS_ASM) $(SRCS_C-y)
OBJS = $(patsubst %.S, %.o, $(SRCS_ASM))
OBJS += $(patsubst %.c, %.o, $(SRCS_C-y))

objs: $(OBJS)
	$(TARGET_LD) $(TARGET_LDFLAGS) -r -o karch.o $(OBJS)
