SRCS = stdio.c stdlib.c string.c
#md5.c
#$(sort $(wildcard *.c))
OBJS = $(patsubst %.c,%.o, $(SRCS))

all: objs

include ../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/core/rules.mk
-include dep.mk

objs: $(OBJS)
	@$(TARGET_LD) $(TARGET_LDFLAGS) -r -o libc.o $(OBJS)
