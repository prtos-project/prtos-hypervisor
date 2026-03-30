# FreeRTOS on PRTOS - Hardware-Assisted Virtualization (native, unmodified) for amd64
#
# Builds the native FreeRTOS demo (X86_64, QEMU) as a PRTOS hw-virt partition
# WITHOUT modifying any original source code.
#
# The hypervisor uses Intel VT-x (VMX) with EPT to run the unmodified guest
# in VMX non-root mode. Virtual PIT/PIC/UART are emulated by the hypervisor.
#
# Usage:
#   make          # Build container.bin and resident_sw.iso
#   make run      # Build and run on QEMU with KVM
#   make clean    # Clean all build artifacts

BAIL_PATH=../../

all: container.bin resident_sw.iso
include $(BAIL_PATH)/common/rules.mk

# XMLCF: path to the XML configuration file
XMLCF=prtos_cf.$(ARCH).xml

# PARTITIONS: partition files (pef format)
PARTITIONS=partition.pef

# ============================================================================
# Native FreeRTOS source (completely unmodified)
# ============================================================================
# FreeRTOS core source from aarch64 native port (which has full FreeRTOS-Kernel)
FREERTOS_CORE = ../native_freertos_run_on_qemu_a57_virt
FREERTOS_SRC = $(FREERTOS_CORE)/FreeRTOS/Source/
FREERTOS_MEMMANG_SRC = $(FREERTOS_SRC)portable/MemMang/
# Shared demo files (example.c, nostdlib.c, printf-stdarg.c) from aarch64 demo
SHARED_APP_SRC = $(FREERTOS_CORE)/FreeRTOS/Demo/CORTEX_A57_64-bit/
# x86-64 specific port and demo
ORIG = ../native_freertos_run_on_qemu_amd64_virt
FREERTOS_PORT_SRC = $(ORIG)/FreeRTOS/Source/portable/GCC/X86_64_BIT/
APP_SRC = $(ORIG)/FreeRTOS/Demo/X86_64/

FRT_CC = gcc
FRT_LD = ld
OBJDIR = obj/

# Compiler flags - same as native Makefile
FRT_CFLAGS = -m64 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone \
             -Wall -Wextra -g -DGUEST -mcmodel=kernel -fno-pie
FRT_ASM_FLAGS = -m64 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone \
                -g -mcmodel=kernel -fno-pie

# Include paths - native headers only (no local overrides)
FRT_INC = -I$(FREERTOS_SRC)include/ -I$(APP_SRC)include/ -I$(FREERTOS_PORT_SRC)

# ============================================================================
# Object files - ALL from native source, plus hdr.o for PRTOS packaging
# ============================================================================

# FreeRTOS core (from original source, unchanged)
FREERTOS_OBJS = queue.o list.o tasks.o timers.o

# Memory management (from original source, unchanged)
FREERTOS_MEMMANG_OBJS = heap_1.o

# Port layer (from original source, unchanged)
FREERTOS_PORT_OBJS = port.o portASM.o

# Application (from original x86-64 source, unchanged)
APP_OBJS = main.o start.o FreeRTOS_tick_config.o uart.o

# Shared demo files (from aarch64 demo, but they are architecture-independent)
SHARED_OBJS = printf-stdarg.o example.o nostdlib.o

# PRTOS image header (only addition, doesn't modify native code)
LOCAL_OBJS = hdr.o

OBJS = $(addprefix $(OBJDIR), $(FREERTOS_OBJS) $(FREERTOS_MEMMANG_OBJS) \
       $(FREERTOS_PORT_OBJS) $(APP_OBJS) $(SHARED_OBJS) $(LOCAL_OBJS))

# ============================================================================
# Build rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

# Build partition ELF from FreeRTOS objects
partition: $(OBJDIR) $(OBJS) linker.ld
	$(FRT_LD) -m elf_x86_64 -T linker.ld $(OBJS) -o $@

# FreeRTOS core (from original source)
$(OBJDIR)%.o: $(FREERTOS_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Memory management (from original source)
$(OBJDIR)%.o: $(FREERTOS_MEMMANG_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Port layer (from original source, unchanged)
$(OBJDIR)%.o: $(FREERTOS_PORT_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

$(OBJDIR)%.o: $(FREERTOS_PORT_SRC)%.S
	$(FRT_CC) -c $(FRT_ASM_FLAGS) $(FRT_INC) $< -o $@

# Application (from original source, unchanged)
$(OBJDIR)%.o: $(APP_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

$(OBJDIR)%.o: $(APP_SRC)%.S
	$(FRT_CC) -c $(FRT_ASM_FLAGS) $(FRT_INC) $< -o $@

# Shared demo files (example.c, nostdlib.c, printf-stdarg.c from aarch64 demo)
$(OBJDIR)%.o: $(SHARED_APP_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Local files (hdr.c only)
$(OBJDIR)%.o: ./%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# ============================================================================
# PRTOS Packaging
# ============================================================================

PACK_ARGS=-h $(PRTOS_CORE):prtos_cf.pef.prtos_conf \
	-p 0:partition.pef

container.bin: $(PARTITIONS) prtos_cf.pef.prtos_conf
	$(PRTOS_PACK) check prtos_cf.pef.prtos_conf $(PACK_ARGS)
	$(PRTOS_PACK) build $(PACK_ARGS) $@

include ../run.mk

# Override run.amd64: this example requires KVM for VMX hardware-assisted virtualization.
# Without -enable-kvm -cpu host, QEMU TCG cannot emulate VMX instructions and the
# hypervisor will silently fail to initialize VMX, producing no serial output.
run.amd64:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host -m 512 -cdrom resident_sw.iso -serial stdio -boot d -smp 4

run.amd64.nographic:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host -nographic -m 512 -cdrom resident_sw.iso -serial mon:stdio -boot d -smp 4

clean:
	@rm -f partition *.pef *.prtos_conf container.bin resident_sw resident_sw.bin resident_sw.iso resident_sw_image
	@rm -rf $(OBJDIR)

.PHONY: all clean run
