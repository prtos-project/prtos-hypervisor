all: objs

include ../../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/core/rules.mk

SRCS_ASM += start.S head.S entry.S

# PRTOS native aarch64 source files
SRCS_C-y += processor.c irqs.c panic.c physmm.c vmmap.c setup.c
SRCS_C-y += kthread.c armv8_timer.c smp.c traps.c mmu.c hypercalls.c
SRCS_C-y += prtos_vgic.c prtos_psci.c prtos_virtio_console.c

# Xen hypervisor core (consolidated)
SRCS_C-y += xen_arch.c xen_mmu.c xen_irq.c xen_sched.c xen_time.c
SRCS_C-y += xen_smp.c xen_dt.c xen_drivers.c xen_events.c
SRCS_C-y += xen_lib.c xen_xsm.c xen_misc.c
SRCS_C-y += vcpreg.c vsysreg.c page_alloc.c fdt_sw.c rangeset.c tasklet.c dummy.c silo.c vpl011.c

# ASM utility files (ARM operations)
SRCS_ASM += bpi.S clear_page.S debug.S memchr.S smc.S strnlen.S

SRCS=$(SRCS_ASM) $(SRCS_C-y)
OBJS = $(patsubst %.S, %.o, $(SRCS_ASM))
OBJS += $(patsubst %.c, %.o, $(SRCS_C-y))

objs: $(OBJS)
	$(TARGET_LD) $(TARGET_LDFLAGS) -r -o karch.o $(OBJS)
