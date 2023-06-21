SRCS := $(sort $(wildcard *.c *.S))
OBJS := $(patsubst %.c,%.o,$(wildcard *.c))
OBJS := $(OBJS) $(patsubst %.S,%.o,$(wildcard *.S))

all: objs

include ../../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/core/rules.mk
-include dep.mk

objs: $(OBJS)
	$(TARGET_LD) $(TARGET_LDFLAGS) -r -o kmm.o $(OBJS)
