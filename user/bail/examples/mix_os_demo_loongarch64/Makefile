# Mixed-OS Demo (LoongArch64): Linux (Partition 0) + FreeRTOS para-virt (Partition 1)
#
# Partition 0: Linux SMP (3 vCPUs) for HMI/cloud on pCPU 0-2 (hw-virt)
# Partition 1: FreeRTOS real-time motor controller on pCPU 3 (para-virt)
#
# Usage:
#   make          # Build container.bin and resident_sw
#   make run      # Build and run on QEMU
#   make clean    # Clean all build artifacts

BAIL_PATH=../../

all: container.bin resident_sw
include $(BAIL_PATH)/common/rules.mk

# XMLCF: path to the XML configuration file
XMLCF=prtos_cf.$(ARCH).xml

# PARTITIONS: partition files (pef format)
PARTITIONS=partition_freertos.pef partition_linux.pef

# ============================================================================
# FreeRTOS Build Configuration (Partition 1 - para-virt)
# ============================================================================
FREERTOS_PARA = ../freertos_para_virt_loongarch64
ORIG = ../native_freertos_run_on_qemu_loongarch64_virt
FREERTOS_SRC = $(ORIG)/FreeRTOS/Source/
FREERTOS_MEMMANG_SRC = $(FREERTOS_SRC)portable/MemMang/

CROSS = loongarch64-linux-gnu
FRT_CC = $(CROSS)-gcc
FRT_LD = $(CROSS)-ld
LNX_CC = $(CROSS)-gcc
LNX_LD = $(CROSS)-ld
LNX_OBJCOPY = $(CROSS)-objcopy
OBJDIR = obj/

FRT_CFLAGS = -march=loongarch64 -mabi=lp64d -ffreestanding -Wall -Wextra -g -DGUEST
FRT_INC = -I./include -I$(FREERTOS_PARA)/include -I$(FREERTOS_SRC)include/

LNX_CFLAGS = -march=loongarch64 -mabi=lp64d -ffreestanding -Wall -g

LIBGCC = $(shell $(FRT_CC) $(FRT_CFLAGS) -print-libgcc-file-name)

# FreeRTOS core objects (from original source, unchanged)
FREERTOS_OBJS = queue.o list.o tasks.o timers.o

# Memory management (from original source, unchanged)
FREERTOS_MEMMANG_OBJS = heap_1.o

# Port layer (from freertos_para_virt_loongarch64, para-virt specific)
PORT_OBJS = port.o portASM.o

# FreeRTOS application objects
LOCAL_APP_OBJS = main.o hdr_freertos.o uart.o
PARA_APP_OBJS = start.o tick_config.o printf-stdarg.o nostdlib.o

FRT_OBJS = $(addprefix $(OBJDIR), $(FREERTOS_OBJS) $(FREERTOS_MEMMANG_OBJS) \
           $(PORT_OBJS) $(LOCAL_APP_OBJS) $(PARA_APP_OBJS))

# ============================================================================
# Linux Build Configuration (Partition 0 - hw-virt)
# ============================================================================
LINUX_WORKSPACE=/home/chenweis/hdd/Repo/loongarch64_linux_workspace
LINUX_VMLINUX=$(LINUX_WORKSPACE)/linux-6.19.9/vmlinux.unstripped

# Prevent Make implicit rule from rebuilding external vmlinux via host cc
$(LINUX_VMLINUX): ;

# ============================================================================
# FreeRTOS Build Rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

# FreeRTOS core (from original source)
$(OBJDIR)%.o: $(FREERTOS_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Memory management (from original source)
$(OBJDIR)%.o: $(FREERTOS_MEMMANG_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Local override C files (main.c, hdr_freertos.c)
$(OBJDIR)%.o: ./%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Para-virt files from freertos_para_virt_loongarch64
$(OBJDIR)%.o: $(FREERTOS_PARA)/%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

$(OBJDIR)%.o: $(FREERTOS_PARA)/%.S
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Link FreeRTOS partition ELF
partition_freertos: $(OBJDIR) $(FRT_OBJS) linker_freertos.ld
	$(FRT_LD) -T linker_freertos.ld $(FRT_OBJS) $(LIBGCC) -o $@

# ============================================================================
# Linux Build Rules
# ============================================================================

# Extract flat binary from vmlinux ELF
vmlinux.bin: $(LINUX_VMLINUX)
	$(LNX_OBJCOPY) -O binary $< $@

$(OBJDIR)linux_start.o: linux_start.S vmlinux.bin | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) $< -o $@

$(OBJDIR)hdr_linux.o: hdr_linux.c | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) $< -o $@

LNX_OBJS = $(OBJDIR)hdr_linux.o $(OBJDIR)linux_start.o

EXTERNAL_LDFLAGS=y

partition_linux: $(LNX_OBJS) linker_linux.ld
	$(LNX_LD) -T linker_linux.ld $(LNX_OBJS) -o $@

# ============================================================================
# PRTOS Packaging (2 partitions)
# ============================================================================

PACK_ARGS=-h $(PRTOS_CORE):prtos_cf.pef.prtos_conf \
	-p 0:partition_linux.pef \
	-p 1:partition_freertos.pef

container.bin: $(PARTITIONS) prtos_cf.pef.prtos_conf
	$(PRTOS_PACK) check prtos_cf.pef.prtos_conf $(PACK_ARGS)
	$(PRTOS_PACK) build $(PACK_ARGS) $@

include ../run.mk

clean:
	@rm -f partition_freertos partition_linux
	@rm -f *.pef *.prtos_conf container.bin resident_sw
	@rm -f resident_sw.bin resident_sw_image
	@rm -f vmlinux.bin
	@rm -rf $(OBJDIR)

.PHONY: all clean run
