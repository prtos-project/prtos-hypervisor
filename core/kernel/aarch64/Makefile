all: objs

include ../../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/core/rules.mk
#-include dep.mk


SRCS_ASM += start.S head.S entry.S
SRCS_C-y += processor.c irqs.c panic.c hpet.c apic.c physmm.c vmmap.c setup.c 
SRCS_C-y += kthread.c lapic_timer.c smp.c 


AS_ASM_FILES = \
xen/arch/arm/arm64/cache.S \
xen/arch/arm/arm64/debug.S \
xen/arch/arm/arm64/smc.S \
xen/arch/arm/arm64/bpi.S \
xen/arch/arm/arm64/lib/strncmp.S \
xen/arch/arm/arm64/lib/strlen.S \
xen/arch/arm/arm64/lib/memchr.S \
xen/arch/arm/arm64/lib/clear_page.S \
xen/arch/arm/arm64/lib/strrchr.S \
xen/arch/arm/arm64/lib/memcpy.S \
xen/arch/arm/arm64/lib/strchr.S \
xen/arch/arm/arm64/lib/strnlen.S \
xen/arch/arm/arm64/lib/memcmp.S \
xen/arch/arm/arm64/lib/strcmp.S \
xen/arch/arm/arm64/lib/memset.S \
xen/arch/arm/arm64/lib/memmove.S

FILES =  \
xen/arch/arm/vsmc.c \
xen/arch/arm/vgic-v3.c \
xen/arch/arm/guest_atomics.c \
xen/arch/arm/early_printk.c \
xen/arch/arm/time.c \
xen/arch/arm/domctl.c \
xen/arch/arm/guestcopy.c \
xen/arch/arm/domain_build.c \
xen/arch/arm/smp.c \
xen/arch/arm/bootfdt.c \
xen/arch/arm/platforms/brcm-raspberry-pi.c \
xen/arch/arm/monitor.c \
xen/arch/arm/io.c \
xen/arch/arm/efi/runtime.c \
xen/arch/arm/efi/ebmalloc.c \
xen/arch/arm/platform_hypercall.c \
xen/arch/arm/arm64/domctl.c \
xen/arch/arm/arm64/vsysreg.c \
xen/arch/arm/arm64/smpboot.c \
xen/arch/arm/arm64/domain.c \
xen/arch/arm/arm64/mmu/mm.c \
xen/arch/arm/arm64/insn.c \
xen/arch/arm/arm64/cpufeature.c \
xen/arch/arm/arm64/traps.c \
xen/arch/arm/arm64/vfp.c \
xen/arch/arm/device.c \
xen/arch/arm/kernel.c \
xen/arch/arm/vm_event.c \
xen/arch/arm/gic-v3.c \
xen/arch/arm/vpsci.c \
xen/arch/arm/physdev.c \
xen/arch/arm/smpboot.c \
xen/arch/arm/domain.c \
xen/arch/arm/vtimer.c \
xen/arch/arm/mmu/pt.c \
xen/arch/arm/mmu/smpboot.c \
xen/arch/arm/mmu/p2m.c \
xen/arch/arm/mmu/setup.c \
xen/arch/arm/mm.c \
xen/arch/arm/gic.c \
xen/arch/arm/p2m.c \
xen/arch/arm/vcpreg.c \
xen/arch/arm/percpu.c \
xen/arch/arm/psci.c \
xen/arch/arm/platform.c \
xen/arch/arm/vuart.c \
xen/arch/arm/dom0less-build.c \
xen/arch/arm/cpuerrata.c \
xen/arch/arm/shutdown.c \
xen/arch/arm/static-evtchn.c \
xen/arch/arm/cpufeature.c \
xen/arch/arm/vpl011.c \
xen/arch/arm/alternative.c \
xen/arch/arm/decode.c \
xen/arch/arm/guest_walk.c \
xen/arch/arm/traps.c \
xen/arch/arm/gic-vgic.c \
xen/arch/arm/irq.c \
xen/arch/arm/vgic.c \
xen/arch/arm/setup.c \
xen/arch/arm/processor.c \
xen/arch/arm/hvm.c \
xen/arch/arm/sysctl.c \
xen/arch/arm/arm64/lib/bitops.c \
xen/drivers/char/pl011.c \
xen/drivers/char/console.c \
xen/drivers/char/arm-uart.c \
xen/drivers/char/cadence-uart.c \
xen/drivers/char/scif-uart.c \
xen/drivers/char/serial.c \
xen/drivers/passthrough/arm/iommu_fwspec.c \
xen/drivers/passthrough/arm/iommu_helpers.c \
xen/drivers/passthrough/arm/smmu.c \
xen/drivers/passthrough/arm/iommu.c \
xen/drivers/passthrough/iommu.c \
xen/drivers/passthrough/device_tree.c \
xen/common/version.c \
xen/common/radix-tree.c \
xen/common/warning.c \
xen/common/virtual_region.c \
xen/common/bitops.c \
xen/common/vmap.c \
xen/common/symbols.c \
xen/common/stop_machine.c \
xen/common/time.c \
xen/common/domctl.c \
xen/common/symbols-dummy.c \
xen/common/guestcopy.c \
xen/common/memory.c \
xen/common/smp.c \
xen/common/monitor.c \
xen/common/softirq.c \
xen/common/kernel.c \
xen/common/wait.c \
xen/common/vm_event.c \
xen/common/event_channel.c \
xen/common/trace.c \
xen/common/vsprintf.c \
xen/common/event_fifo.c \
xen/common/hypfs.c \
xen/common/domain.c \
xen/common/page_alloc.c \
xen/common/preempt.c \
xen/common/xmalloc_tlsf.c \
xen/common/event_2l.c \
xen/common/cpu.c \
xen/common/multicall.c \
xen/common/timer.c \
xen/common/rangeset.c \
xen/common/bitmap.c \
xen/common/libfdt/fdt_rw.c \
xen/common/libfdt/fdt_overlay.c \
xen/common/libfdt/fdt_addresses.c \
xen/common/libfdt/fdt_sw.c \
xen/common/libfdt/fdt_check.c \
xen/common/libfdt/fdt_strerror.c \
xen/common/libfdt/fdt_wip.c \
xen/common/libfdt/fdt_ro.c \
xen/common/libfdt/fdt_empty_tree.c \
xen/common/libfdt/fdt.c \
xen/common/sched/arinc653.c \
xen/common/sched/rt.c \
xen/common/sched/core.c \
xen/common/sched/credit.c \
xen/common/sched/null.c \
xen/common/sched/credit2.c \
xen/common/sched/cpupool.c \
xen/common/shutdown.c \
xen/common/pdx.c \
xen/common/gzip/gunzip.c \
xen/common/tasklet.c \
xen/common/rcupdate.c \
xen/common/keyhandler.c \
xen/common/irq.c \
xen/common/grant_table.c \
xen/common/notifier.c \
xen/common/random.c \
xen/common/pmap.c \
xen/common/rwlock.c \
xen/common/spinlock.c \
xen/common/sysctl.c \
xen/common/device_tree.c \
xen/xsm/xsm_policy.c \
xen/xsm/dummy.c \
xen/xsm/silo.c \
xen/xsm/flask/ss/ebitmap.c \
xen/xsm/flask/ss/sidtab.c \
xen/xsm/flask/ss/policydb.c \
xen/xsm/flask/ss/services.c \
xen/xsm/flask/ss/symtab.c \
xen/xsm/flask/ss/avtab.c \
xen/xsm/flask/ss/hashtab.c \
xen/xsm/flask/ss/conditional.c \
xen/xsm/flask/ss/mls.c \
xen/xsm/flask/hooks.c \
xen/xsm/flask/flask_op.c \
xen/xsm/flask/avc.c \
xen/xsm/xsm_core.c \
xen/lib/strncmp.c \
xen/lib/strlen.c \
xen/lib/ctype.c \
xen/lib/strncasecmp.c \
xen/lib/parse-size.c \
xen/lib/memchr.c \
xen/lib/ctors.c \
xen/lib/strrchr.c \
xen/lib/strlcpy.c \
xen/lib/strspn.c \
xen/lib/strtoll.c \
xen/lib/generic-flsl.c \
xen/lib/strcasecmp.c \
xen/lib/strtol.c \
xen/lib/strstr.c \
xen/lib/rbtree.c \
xen/lib/bsearch.c \
xen/lib/strtoull.c \
xen/lib/memcpy.c \
xen/lib/list-sort.c \
xen/lib/sort.c \
xen/lib/strlcat.c \
xen/lib/strchr.c \
xen/lib/strnlen.c \
xen/lib/memcmp.c \
xen/lib/strcmp.c \
xen/lib/muldiv64.c \
xen/lib/strtoul.c \
xen/lib/generic-ffsl.c \
xen/lib/strpbrk.c \
xen/lib/strsep.c \
xen/lib/memchr_inv.c \
xen/lib/memset.c \
xen/lib/find-next-bit.c \
xen/lib/memmove.c


SRCS_C-y += $(FILES)
SRCS_ASM += $(AS_ASM_FILES)


SRCS=$(SRCS_ASM) $(SRCS_C-y)
OBJS = $(patsubst %.S, %.o, $(SRCS_ASM))
OBJS += $(patsubst %.c, %.o, $(SRCS_C-y))

objs: $(OBJS)
	$(TARGET_LD) $(TARGET_LDFLAGS) -r -o karch.o $(OBJS)
