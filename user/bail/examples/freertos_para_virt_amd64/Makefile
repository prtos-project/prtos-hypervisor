# FreeRTOS on PRTOS - Para-Virtualization [amd64]
#
# Builds FreeRTOS as a para-virtualized partition linked with BAIL and libprtos.
# BAIL provides: boot.S, init_arch (IDT/GDT), setup_irqs, hdr.c, printf
# FreeRTOS provides: scheduler, tasks, timers
# This port provides: custom timer/yield ISRs using PRTOS hypercalls
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

# FreeRTOS core source from aarch64 native port (which has full FreeRTOS-Kernel)
ORIG = ../native_freertos_run_on_qemu_a57_virt
FREERTOS_SRC = $(ORIG)/FreeRTOS/Source/
FREERTOS_MEMMANG_SRC = $(FREERTOS_SRC)portable/MemMang/
# Shared app files (example.c, nostdlib.c) from aarch64 demo
APP_SRC = $(ORIG)/FreeRTOS/Demo/CORTEX_A57_64-bit/
# Port layer is local (amd64 PRTOS-specific)
FREERTOS_PORT_SRC = ./FreeRTOS/Source/portable/GCC/X86_64_PRTOS/

OBJDIR = obj/

# Compiler flags: use TARGET_CC from BAIL rules.mk
# Note: TARGET_CFLAGS has -nostdinc which prevents FreeRTOS from finding
# <stdint.h>/<stddef.h>. We use our own flags with only the needed PRTOS includes.
FRT_CC = $(TARGET_CC)
FRT_CFLAGS = -m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
             -ffreestanding -fno-stack-protector -fno-pie -fno-strict-aliasing \
             -fomit-frame-pointer -Wall -Wextra -g -DGUEST -D$(ARCH)
FRT_INC = -I./include -I$(FREERTOS_SRC)include/ -I$(FREERTOS_PORT_SRC)

# ============================================================================
# Object files
# ============================================================================

# FreeRTOS core (shared)
FREERTOS_OBJS = queue.o list.o tasks.o timers.o

# Memory management
FREERTOS_MEMMANG_OBJS = heap_1.o

# Port layer (para-virt specific)
PORT_OBJS = port.o portASM.o

# Application objects
LOCAL_APP_OBJS = main.o tick_config.o printf-stdarg.o
# Shared app files from aarch64 demo (example.c, nostdlib.c)
SHARED_APP_OBJS = example.o nostdlib.o

OBJS = $(addprefix $(OBJDIR), $(FREERTOS_OBJS) $(FREERTOS_MEMMANG_OBJS) \
       $(PORT_OBJS) $(LOCAL_APP_OBJS) $(SHARED_APP_OBJS))

# ============================================================================
# Build rules
# ============================================================================

$(OBJDIR):
	mkdir -p $@

# Build partition: link FreeRTOS objects with BAIL and libprtos
EXTERNAL_LDFLAGS=y
partition: $(OBJDIR) $(OBJS)
	$(TARGET_LD) -u start -u prtos_image_hdr -T$(BAIL_LIB_PATH)/loader.lds \
		-Ttext=$(shell $(XPATHSTART) 0 $(XMLCF)) \
		$(OBJS) \
		-L$(LIB_PRTOS_PATH) -L$(BAIL_LIB_PATH) \
		--start-group $(LIBGCC) $(LIB_PRTOS) $(LIB_BAIL) --end-group \
		$(call if-success,$(TARGET_LD) -v --no-warn-rwx-segments,--no-warn-rwx-segments,) \
		-z noexecstack -o $@

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

# Local C files
$(OBJDIR)%.o: ./%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Shared app files (example.c, nostdlib.c from aarch64 demo)
$(OBJDIR)%.o: $(APP_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

# Local ASM files
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
	@rm -f partition *.pef *.prtos_conf container.bin resident_sw resident_sw.bin resident_sw_image resident_sw.iso
	@rm -rf $(OBJDIR)

.PHONY: all clean run
