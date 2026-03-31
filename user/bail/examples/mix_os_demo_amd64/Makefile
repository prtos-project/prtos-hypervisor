# Mixed-OS Demo (amd64): Linux (hw-virt VMX, Partition 0) + FreeRTOS (para-virt, Partition 1)
#
# Partition 0: Linux x86_64 with embedded bzImage (hw-virt via VMX)
#   - 1 vCPU on pCPU 0, 256MB RAM at GPA 0x6000000
# Partition 1: FreeRTOS real-time motor controller (para-virt via BAIL)
#   - 1 vCPU on pCPU 3, 2MB RAM at GPA 0x4000000
#
# Usage:
#   make          # Build container.bin and resident_sw.iso
#   make run      # Build and run on QEMU (requires KVM)
#   make clean    # Clean all build artifacts

BAIL_PATH=../../

all: container.bin resident_sw.iso
include $(BAIL_PATH)/common/rules.mk

# XMLCF: path to the XML configuration file
XMLCF=prtos_cf.$(ARCH).xml

# PARTITIONS: both partition files (pef format)
PARTITIONS=partition_linux.pef partition_freertos.pef

# ============================================================================
# FreeRTOS Build Configuration (Partition 1 - para-virt)
# ============================================================================
FREERTOS_PARA = ../freertos_para_virt_amd64
ORIG = ../native_freertos_run_on_qemu_a57_virt
FREERTOS_SRC = $(ORIG)/FreeRTOS/Source/
FREERTOS_MEMMANG_SRC = $(FREERTOS_SRC)portable/MemMang/
FREERTOS_PORT_SRC = $(FREERTOS_PARA)/FreeRTOS/Source/portable/GCC/X86_64_PRTOS/
APP_SRC = $(ORIG)/FreeRTOS/Demo/CORTEX_A57_64-bit/

OBJDIR = obj/

FRT_CC = $(TARGET_CC)
FRT_CFLAGS = -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
             -ffreestanding -fno-stack-protector -fno-pie -fno-strict-aliasing \
             -fomit-frame-pointer -Wall -Wextra -g -DGUEST -D$(ARCH)
FRT_INC = -I./include -I$(FREERTOS_PARA)/include -I$(FREERTOS_SRC)include/ -I$(FREERTOS_PORT_SRC)

# FreeRTOS core objects
FREERTOS_OBJS = queue.o list.o tasks.o timers.o

# Memory management
FREERTOS_MEMMANG_OBJS = heap_1.o

# Port layer (para-virt specific)
PORT_OBJS = port.o portASM.o

# Application objects
LOCAL_APP_OBJS = main.o tick_config.o printf-stdarg.o
SHARED_APP_OBJS = nostdlib.o

FRT_OBJS = $(addprefix $(OBJDIR), $(FREERTOS_OBJS) $(FREERTOS_MEMMANG_OBJS) \
           $(PORT_OBJS) $(LOCAL_APP_OBJS) $(SHARED_APP_OBJS))

# ============================================================================
# Linux Build Configuration (Partition 0 - hw-virt VMX)
# ============================================================================
LINUX_WORKSPACE=../native_linux_run_on_qemu_amd64_virt
LINUX_BZIMAGE=$(LINUX_WORKSPACE)/linux-6.19.9/arch/x86/boot/bzImage

LNX_CC = gcc
LNX_LD = ld

LNX_CFLAGS = -m64 -ffreestanding -Wall -g -fno-pie -fno-stack-protector -mcmodel=kernel
LNX_ASFLAGS = -m64 -ffreestanding -g -fno-pie -fno-stack-protector

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

# Port layer (C)
$(OBJDIR)%.o: $(FREERTOS_PORT_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Port layer (ASM)
$(OBJDIR)%.o: $(FREERTOS_PORT_SRC)%.S
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Local C files (main.c, tick_config.c, printf-stdarg.c)
$(OBJDIR)%.o: ./%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Shared app files from aarch64 demo (nostdlib.c)
$(OBJDIR)%.o: $(APP_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Link FreeRTOS partition with BAIL/libprtos
partition_freertos: $(OBJDIR) $(FRT_OBJS)
	$(TARGET_LD) -u start -u prtos_image_hdr -T$(BAIL_LIB_PATH)/loader.lds \
		-Ttext=$(shell $(XPATHSTART) 1 $(XMLCF)) \
		$(FRT_OBJS) \
		-L$(LIB_PRTOS_PATH) -L$(BAIL_LIB_PATH) \
		--start-group $(LIBGCC) $(LIB_PRTOS) $(LIB_BAIL) --end-group \
		$(call if-success,$(TARGET_LD) -v --no-warn-rwx-segments,--no-warn-rwx-segments,) \
		-z noexecstack -o $@

# ============================================================================
# Linux Build Rules
# ============================================================================

# Copy bzImage to local directory for .incbin
bzImage: $(LINUX_BZIMAGE)
	cp $< $@

# Compile boot stub (embeds bzImage via .incbin)
$(OBJDIR)start.o: start.S bzImage | $(OBJDIR)
	$(LNX_CC) -c $(LNX_ASFLAGS) $< -o $@

# Compile PRTOS image header for Linux partition
$(OBJDIR)hdr_linux.o: hdr_linux.c | $(OBJDIR)
	$(LNX_CC) -c $(LNX_CFLAGS) $< -o $@

LNX_OBJS = $(OBJDIR)hdr_linux.o $(OBJDIR)start.o

EXTERNAL_LDFLAGS=y

partition_linux: $(LNX_OBJS) linker_linux.ld
	$(LNX_LD) -m elf_x86_64 -T linker_linux.ld $(LNX_OBJS) -o $@

# Override default PEF build rule: skip compression for large Linux partition
partition_linux.pef: partition_linux
	$(PEF) $< -o $@

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

# Override run.amd64: requires KVM for VMX hardware-assisted virtualization.
run.amd64:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg -m 512 -cdrom resident_sw.iso -serial stdio -boot d -smp 4

run.amd64.nographic:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg -nographic -m 512 -cdrom resident_sw.iso -serial mon:stdio -boot d -smp 4

clean:
	@rm -f partition_freertos partition_linux
	@rm -f *.pef *.prtos_conf container.bin resident_sw resident_sw.iso
	@rm -f resident_sw.bin resident_sw_image
	@rm -f bzImage
	@rm -rf $(OBJDIR)

.PHONY: all clean run
