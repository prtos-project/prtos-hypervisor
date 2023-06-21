#SRCS := $(sort $(wildcard *.c))
#OBJS := $(patsubst %.c,%.o,$(wildcard *.c))
#LOBJS := $(OBJS)

all: objs mods

include ../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/core/rules.mk
-include dep.mk

srcs-y := kdevice.c

ifdef CONFIG_x86
srcs-$(CONFIG_DEV_UART) += pc_uart.c
srcs-$(CONFIG_DEV_VGA) += pc_vga.c
endif

srcs-$(CONFIG_DEV_MEMBLOCK) += memblock.c

SRCS := $(srcs-y) $(srcs-m)
OBJS := $(patsubst %.c,%.o,$(srcs-y))
LOBJS := $(OBJS)

OBJS-M := $(patsubst %.c,%.xo,$(srcs-m))

objs: $(OBJS)
	$(TARGET_LD) $(TARGET_LDFLAGS) -r -o drivers.o $(LOBJS)

mods: $(OBJS-M)
