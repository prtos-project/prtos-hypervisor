#SRCS := $(sort $(wildcard *.c))
#SRCS := console.c hm.c  mem.c  status.c  trace.c
#OBJS := $(patsubst %.c,%.o,$(wildcard *.c))
#LOBJS := $(OBJS)

all: objs mods

include ../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/core/rules.mk
-include dep.mk

srcs-y := console.c hm.c  trace.c

srcs-$(CONFIG_OBJ_COMM_PORTS) += commports.c
srcs-$(CONFIG_OBJ_RAW_MEM) += mem.c
srcs-$(CONFIG_OBJ_STATUS) += status.c

SRCS := $(srcs-y) $(srcs-m)
OBJS := $(patsubst %.c,%.o,$(srcs-y))
LOBJS := $(OBJS)

OBJS-M := $(patsubst %.c,%.xo,$(srcs-m))

objs: $(OBJS)
	@$(TARGET_LD) $(TARGET_LDFLAGS) -r -o objects.o $(LOBJS)

mods: $(OBJS-M)
