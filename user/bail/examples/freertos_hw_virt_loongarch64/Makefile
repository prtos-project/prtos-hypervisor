# FreeRTOS on PRTOS - Hardware-Assisted Virtualization (native, unmodified) [LoongArch64]
#
# Builds the native FreeRTOS demo (LoongArch64, QEMU virt) as a PRTOS
# hw-virt partition WITHOUT modifying any original source code.
#
# The hypervisor emulates timer via LVZ guest CSRs (TCFG/TVAL/TICLR)
# and UART is mapped via guest TLB pass-through (configured in XML).
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
PARTITIONS=partition.pef

# ============================================================================
# Native FreeRTOS source (completely unmodified LoongArch64 port)
# ============================================================================
ORIG = ../native_freertos_run_on_qemu_loongarch64_virt
FREERTOS_SRC = $(ORIG)/FreeRTOS/Source/
FREERTOS_MEMMANG_SRC = $(FREERTOS_SRC)portable/MemMang/
FREERTOS_PORT_SRC = $(FREERTOS_SRC)portable/GCC/RISC_V_64_BIT/
APP_SRC = $(ORIG)/FreeRTOS/Demo/RISC_V_64-bit/

CROSS = loongarch64-linux-gnu
FRT_CC = $(CROSS)-gcc
FRT_LD = $(CROSS)-ld
OBJDIR = obj/

# Compiler flags - LoongArch64 specific
FRT_CFLAGS = -march=la464 -mabi=lp64d -ffreestanding -Wall -Wextra -g -DGUEST

# Include paths - native headers only (no local overrides)
FRT_INC = -I$(FREERTOS_SRC)include/ -I$(APP_SRC)include/ -I$(FREERTOS_PORT_SRC)

LIBGCC = $(shell $(FRT_CC) $(FRT_CFLAGS) -print-libgcc-file-name)

# ============================================================================
# Object files - ALL from native source, plus hdr.o for PRTOS packaging
# ============================================================================

# FreeRTOS core (from original source, unchanged)
FREERTOS_OBJS = queue.o list.o tasks.o timers.o

# Memory management (from original source, unchanged)
FREERTOS_MEMMANG_OBJS = heap_1.o

# Port layer (from original source, unchanged)
FREERTOS_PORT_OBJS = port.o portASM.o

# Application (from original source, unchanged)
APP_OBJS = main.o start.o tick_config.o uart.o \
           printf-stdarg.o example.o nostdlib.o

# PRTOS image header (only addition, doesn't modify native code)
LOCAL_OBJS = hdr.o

OBJS = $(addprefix $(OBJDIR), $(FREERTOS_OBJS) $(FREERTOS_MEMMANG_OBJS) \
       $(FREERTOS_PORT_OBJS) $(APP_OBJS) $(LOCAL_OBJS))

# ============================================================================
# Build rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

# Build partition ELF from FreeRTOS objects
partition: $(OBJDIR) $(OBJS) linker.ld
	$(FRT_LD) -T linker.ld $(OBJS) $(LIBGCC) -o $@

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
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Application (from original source, unchanged)
$(OBJDIR)%.o: $(APP_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

$(OBJDIR)%.o: $(APP_SRC)%.S
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

clean:
	@rm -f partition *.pef *.prtos_conf container.bin resident_sw resident_sw.bin resident_sw_image
	@rm -rf $(OBJDIR)

.PHONY: all clean run
