# FreeRTOS on PRTOS - Hardware-Assisted Virtualization
#
# This Makefile builds FreeRTOS as a standalone ELF for a PRTOS hw-virt
# partition, then packages it using PRTOS tools (prtoseformat, prtoscparser,
# prtospack, rswbuild).
#
# Usage:
#   make                          # Build container.bin and resident_sw
#   make run                      # Build and run on QEMU
#   make clean                    # Clean all build artifacts

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

# Original FreeRTOS source tree (unchanged files)
ORIG = ../native_freertos_run_on_qemu_a57_virt
FREERTOS_SRC = $(ORIG)/FreeRTOS/Source/
FREERTOS_MEMMANG_SRC = $(FREERTOS_SRC)portable/MemMang/
APP_SRC = $(ORIG)/FreeRTOS/Demo/CORTEX_A57_64-bit/

CROSS = aarch64-linux-gnu
FRT_CC = $(CROSS)-gcc
FRT_LD = $(CROSS)-ld
OBJDIR = obj/

# Compiler flags
FRT_CFLAGS = -mcpu=cortex-a57 -ffreestanding -Wall -Wextra -g -DGUEST
FRT_INC = -I./include -I$(FREERTOS_SRC)include/ -I$(APP_SRC)include/

# ============================================================================
# Object files
# ============================================================================

# FreeRTOS core (from original source, unchanged)
FREERTOS_OBJS = queue.o list.o tasks.o timers.o

# Memory management (from original source, unchanged)
FREERTOS_MEMMANG_OBJS = heap_1.o

# Port layer (LOCAL overrides - system register GIC access)
PORT_OBJS = port.o portASM.o

# Application objects:
#   LOCAL overrides: main.o start.o gic_v3.o FreeRTOS_tick_config.o
#   From original: FreeRTOS_asm_vectors.o vectors.o exception.o sysctrl.o
#                  pstate.o uart.o printf-stdarg.o example.o nostdlib.o
LOCAL_APP_OBJS = main.o start.o gic_v3.o FreeRTOS_tick_config.o hdr.o
ORIG_APP_OBJS = FreeRTOS_asm_vectors.o vectors.o exception.o sysctrl.o \
                pstate.o uart.o printf-stdarg.o example.o nostdlib.o

OBJS = $(addprefix $(OBJDIR), $(FREERTOS_OBJS) $(FREERTOS_MEMMANG_OBJS) \
       $(PORT_OBJS) $(LOCAL_APP_OBJS) $(ORIG_APP_OBJS))

# ============================================================================
# Build rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

# Build partition ELF from FreeRTOS objects
partition: $(OBJDIR) $(OBJS) linker.ld
	$(FRT_LD) -T linker.ld $(OBJS) -o $@

# FreeRTOS core (from original source)
$(OBJDIR)%.o: $(FREERTOS_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Memory management (from original source)
$(OBJDIR)%.o: $(FREERTOS_MEMMANG_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Local override C files
$(OBJDIR)%.o: ./%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Local override ASM files
$(OBJDIR)%.o: ./%.S
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Original app C files (from Demo directory)
$(OBJDIR)%.o: $(APP_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Original app ASM files (from Demo directory)
$(OBJDIR)%.o: $(APP_SRC)%.S
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
	@rm -rf $(OBJDIR) u-boot

.PHONY: all clean run
