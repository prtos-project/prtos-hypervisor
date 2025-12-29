all: objs

include ../../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/core/rules.mk
-include dep.mk

SRCS := $(sort $(wildcard *.c))
OBJS := $(patsubst %.c,%.o,$(wildcard *.c))
LOBJS := $(OBJS)

objs: $(OBJS)
	@$(TARGET_LD) $(TARGET_LDFLAGS) -r -o arch_libc.o $(LOBJS)
