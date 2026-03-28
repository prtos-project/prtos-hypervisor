# FreeRTOS on PRTOS - Para-Virtualization [RISC-V 64]
#
# Builds a modified FreeRTOS that uses PRTOS hypercalls for timer and
# interrupt management, running as a para-virtualized partition.
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
# FreeRTOS Build Configuration
# ============================================================================

# Original FreeRTOS source tree (unchanged core files)
ORIG = ../native_freertos_run_on_qemu_risc64_virt
FREERTOS_SRC = $(ORIG)/FreeRTOS/Source/
FREERTOS_MEMMANG_SRC = $(FREERTOS_SRC)portable/MemMang/

CROSS = riscv64-linux-gnu
FRT_CC = $(CROSS)-gcc
FRT_LD = $(CROSS)-ld
OBJDIR = obj/

# Compiler flags
FRT_CFLAGS = -march=rv64gc -mabi=lp64d -mcmodel=medany -ffreestanding -Wall -Wextra -g -DGUEST
FRT_INC = -I./include -I$(FREERTOS_SRC)include/

LIBGCC = $(shell $(FRT_CC) $(FRT_CFLAGS) -print-libgcc-file-name)

# ============================================================================
# Object files
# ============================================================================

# FreeRTOS core (from original source, unchanged)
FREERTOS_OBJS = queue.o list.o tasks.o timers.o

# Memory management (from original source, unchanged)
FREERTOS_MEMMANG_OBJS = heap_1.o

# Port layer (LOCAL overrides - para-virt specific)
PORT_OBJS = port.o portASM.o

# Application objects (LOCAL - para-virt specific)
LOCAL_APP_OBJS = main.o start.o tick_config.o uart.o hdr.o \
                 printf-stdarg.o example.o nostdlib.o

OBJS = $(addprefix $(OBJDIR), $(FREERTOS_OBJS) $(FREERTOS_MEMMANG_OBJS) \
       $(PORT_OBJS) $(LOCAL_APP_OBJS))

# ============================================================================
# Build rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

# Build partition ELF
partition: $(OBJDIR) $(OBJS) linker.ld
	$(FRT_LD) -T linker.ld $(OBJS) $(LIBGCC) -o $@

# FreeRTOS core (from original source)
$(OBJDIR)%.o: $(FREERTOS_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Memory management (from original source)
$(OBJDIR)%.o: $(FREERTOS_MEMMANG_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Local C files (para-virt overrides)
$(OBJDIR)%.o: ./%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Local ASM files (para-virt overrides)
$(OBJDIR)%.o: ./%.S
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
