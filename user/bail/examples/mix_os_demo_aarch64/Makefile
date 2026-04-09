# Mixed-OS Demo: Linux (Partition 0) + FreeRTOS (Partition 1)
#
# Partition 0: Linux SMP (3 vCPUs) for HMI/cloud on pCPU 0-2
# Partition 1: FreeRTOS real-time motor controller on pCPU 3
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
# FreeRTOS Build Configuration (Partition 0)
# ============================================================================
ORIG = ../native_freertos_run_on_qemu_a57_virt
FREERTOS_SRC = $(ORIG)/FreeRTOS/Source/
FREERTOS_MEMMANG_SRC = $(FREERTOS_SRC)portable/MemMang/
APP_SRC = $(ORIG)/FreeRTOS/Demo/CORTEX_A57_64-bit/

CROSS = aarch64-linux-gnu
FRT_CC = $(CROSS)-gcc
FRT_LD = $(CROSS)-ld
LNX_CC = $(CROSS)-gcc
LNX_LD = $(CROSS)-ld
DTC = dtc
OBJDIR = obj/

FRT_CFLAGS = -mcpu=cortex-a57 -ffreestanding -Wall -Wextra -g -DGUEST
FRT_INC = -I./include -I$(FREERTOS_SRC)include/ -I$(APP_SRC)include/ -I$(PORT_SRC)

LNX_CFLAGS = -mcpu=cortex-a57 -ffreestanding -Wall -g

# FreeRTOS core objects
FREERTOS_OBJS = queue.o list.o tasks.o timers.o
FREERTOS_MEMMANG_OBJS = heap_1.o
PORT_OBJS = port.o portASM.o
PORT_SRC = $(ORIG)/FreeRTOS/Source/portable/GCC/ARM_CA57_64_BIT/

# FreeRTOS application objects
LOCAL_APP_OBJS = main.o start.o hdr_freertos.o
ORIG_APP_OBJS = FreeRTOS_asm_vectors.o vectors.o exception.o sysctrl.o \
                pstate.o uart.o printf-stdarg.o nostdlib.o \
                gic_v3.o FreeRTOS_tick_config.o

FRT_OBJS = $(addprefix $(OBJDIR), $(FREERTOS_OBJS) $(FREERTOS_MEMMANG_OBJS) \
           $(PORT_OBJS) $(LOCAL_APP_OBJS) $(ORIG_APP_OBJS))

# ============================================================================
# Linux Build Configuration (Partition 1)
# ============================================================================
LINUX_WORKSPACE=/home/chenweis/hdd/Repo/aarch64_linux_workspace
LINUX_IMAGE=$(LINUX_WORKSPACE)/linux-6.19.9/arch/arm64/boot/Image

# ============================================================================
# FreeRTOS Build Rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

# FreeRTOS core
$(OBJDIR)%.o: $(FREERTOS_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Memory management
$(OBJDIR)%.o: $(FREERTOS_MEMMANG_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Local override C files
$(OBJDIR)%.o: ./%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Local override ASM files
$(OBJDIR)%.o: ./%.S
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Original app C files (from FreeRTOS Demo)
$(OBJDIR)%.o: $(APP_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Original app ASM files (from FreeRTOS Demo)
$(OBJDIR)%.o: $(APP_SRC)%.S
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# FreeRTOS portable layer (native, unmodified)
$(OBJDIR)%.o: $(PORT_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

$(OBJDIR)%.o: $(PORT_SRC)%.S
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Link FreeRTOS partition ELF
partition_freertos: $(OBJDIR) $(FRT_OBJS) linker_freertos.ld
	$(FRT_LD) -T linker_freertos.ld $(FRT_OBJS) -o $@

# ============================================================================
# Linux Build Rules
# ============================================================================

linux_guest.dtb: linux_guest.dts
	$(DTC) -I dts -O dtb -o $@ $<

Image: $(LINUX_IMAGE)
	cp $< $@

$(OBJDIR)linux_start.o: linux_start.S linux_guest.dtb Image | $(OBJDIR)
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
	@rm -f linux_guest.dtb Image
	@rm -rf $(OBJDIR) u-boot

.PHONY: all clean run
