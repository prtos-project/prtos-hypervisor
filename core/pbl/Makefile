all: pbl.bin

include ../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/core/rules.mk
#-include dep.mk

SRCS_C-y = pbl.c stdc.c
SRCS_S-y = boot.S

LIBPRTOS=../../user/libprtos
TARGET_CFLAGS+=-I$(LIBPRTOS)/include

SRCS = $(SRCS_S-y) $(SRCS_C-y)
OBJS += $(patsubst %.S, %.o, $(SRCS_S-y)) $(patsubst %.c, %.o, $(SRCS_C-y)) 

$(LIBPRTOS)/libprtos.a:
	$(MAKE) -s -C $(LIBPRTOS)/../ links
	$(MAKE) -s -C $(LIBPRTOS)

pbl.lds: $(LIBPRTOS)/libprtos.a pbl.$(ARCH).lds.in ../include/config.h
	@$(TARGET_CC) $(TARGET_ASFLAGS) -x assembler-with-cpp -E -P -I$(PRTOS_CORE_PATH)/include pbl.$(ARCH).lds.in -o pbl.lds

pbl.elf: pbl.lds $(OBJS)
	@$(TARGET_LD) $(TARGET_LDFLAGS) -o pbl.elf -Tpbl.lds $(OBJS) -L$(LIBPRTOS) -lprtos $(LIBGCC)

pbl.bin: pbl.elf
	@$(TARGET_OBJCOPY) -O binary pbl.elf pbl.bin
#	$(CC) $(filter-out -pg,$(CFLAGS)) -c gprof.c
