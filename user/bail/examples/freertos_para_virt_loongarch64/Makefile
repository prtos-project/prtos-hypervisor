# FreeRTOS on PRTOS - Para-Virtualization [LoongArch 64]

BAIL_PATH=../../

all: container.bin resident_sw
include $(BAIL_PATH)/common/rules.mk

XMLCF=prtos_cf.$(ARCH).xml
PARTITIONS=partition.pef

# FreeRTOS source from native base (shared core files)
ORIG = ../native_freertos_run_on_qemu_loongarch64_virt
FREERTOS_SRC = $(ORIG)/FreeRTOS/Source/
FREERTOS_MEMMANG_SRC = $(FREERTOS_SRC)portable/MemMang/

CROSS = loongarch64-linux-gnu
FRT_CC = $(CROSS)-gcc
FRT_LD = $(CROSS)-ld
OBJDIR = obj/

FRT_CFLAGS = -march=loongarch64 -mabi=lp64d -Os -ffreestanding -Wall -Wextra -g -DGUEST
FRT_INC = -I./include -I$(FREERTOS_SRC)include/

LIBGCC = $(shell $(FRT_CC) $(FRT_CFLAGS) -print-libgcc-file-name)

# FreeRTOS core (unchanged)
FREERTOS_OBJS = queue.o list.o tasks.o timers.o
FREERTOS_MEMMANG_OBJS = heap_1.o

# Port layer (LoongArch64 para-virt)
PORT_OBJS = port.o portASM.o

# Application objects
LOCAL_APP_OBJS = main.o start.o tick_config.o uart.o hdr.o \
                 printf-stdarg.o example.o nostdlib.o

OBJS = $(addprefix $(OBJDIR), $(FREERTOS_OBJS) $(FREERTOS_MEMMANG_OBJS) \
       $(PORT_OBJS) $(LOCAL_APP_OBJS))

$(OBJDIR):
	mkdir -p $@

partition: $(OBJDIR) $(OBJS) linker.ld
	$(FRT_LD) -T linker.ld $(OBJS) $(LIBGCC) -o $@

$(OBJDIR)%.o: $(FREERTOS_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

$(OBJDIR)%.o: $(FREERTOS_MEMMANG_SRC)%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

$(OBJDIR)%.o: ./%.c
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

$(OBJDIR)%.o: ./%.S
	$(FRT_CC) -c $(FRT_CFLAGS) $(FRT_INC) $< -o $@

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
