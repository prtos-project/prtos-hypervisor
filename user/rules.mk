PRTOS_CORE_PATH=$(PRTOS_PATH)/core
LIB_PRTOS_PATH=$(PRTOS_PATH)/user/libprtos

if-success = $(shell { $(1); } >/dev/null 2>&1 && echo "$(2)" || echo "$(3)")
check_gcc = $(shell if $(TARGET_CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi)
check_hgcc = $(shell if $(HOST_CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi)

HOST_CFLAGS = -Wall -D$(ARCH) -I$(LIB_PRTOS_PATH)/include -DHOST -D"__PRTOS_INCFLD(_fld)=<prtos_inc/_fld>" -fno-strict-aliasing
HOST_LDFLAGS =

TARGET_CFLAGS = -Wall -nostdlib -nostdinc -D$(ARCH) -fno-strict-aliasing -fomit-frame-pointer -D"__PRTOS_INCFLD(_fld)=<prtos_inc/_fld>"
TARGET_CFLAGS += -I$(LIB_PRTOS_PATH)/include --include prtos_inc/config.h --include prtos_inc/arch/arch_types.h

TARGET_CFLAGS += $(call check_gcc,-Wno-unused-but-set-variable,)
TARGET_CFLAGS += $(call check_gcc,-fno-pie,)
TARGET_CFLAGS += $(call check_gcc,-ffreestanding,)
TARGET_CFLAGS += $(call check_gcc,-Wno-int-in-bool-context,)
TARGET_CFLAGS += $(call check_gcc,-Wno-address-of-packed-member,)
TARGET_CFLAGS += $(call check_gcc,-Wno-array-bounds,)
TARGET_CFLAGS += $(call check_gcc,--std=gnu89,)
TARGET_CFLAGS += $(call check_gcc,-Wno-implicit-function-declaration,)

HOST_CFLAGS += $(call check_hgcc,-Wno-unused-but-set-variable,)
HOST_CFLAGS += $(call check_hgcc,-Wno-address-of-packed-member,)

ifeq ($(ARCH), x86)
    TARGET_CFLAGS += $(TARGET_CFLAGS_ARCH)
    HOST_CFLAGS += $(HOST_CFLAGS_ARCH)
    HOST_ASFLAGS += $(HOST_CFLAGS_ARCH)
endif
ifeq ($(ARCH), aarch64)
    TARGET_CFLAGS += $(TARGET_CFLAGS_ARCH)
    HOST_CFLAGS += $(HOST_CFLAGS_ARCH)
    HOST_ASFLAGS += $(HOST_CFLAGS_ARCH)
endif

TARGET_ASFLAGS = -Wall -D__ASSEMBLY__ -fno-builtin -D$(ARCH) -D"__PRTOS_INCFLD(_fld)=<prtos_inc/_fld>"
TARGET_ASFLAGS += -I$(LIB_PRTOS_PATH)/include -nostdlib -nostdinc --include prtos_inc/config.h
TARGET_ASFLAGS += $(TARGET_ASFLAGS_ARCH)
LIBGCC=`$(TARGET_CC) -print-libgcc-file-name $(TARGET_CFLAGS_ARCH)`

TARGET_LDFLAGS = $(TARGET_LDFLAGS_ARCH) -z noexecstack
TARGET_LDFLAGS += $(call if-success,$(TARGET_LD) -v --no-warn-rwx-segments,--no-warn-rwx-segments,)

%.host.o: %.c
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

%.host.o: %.S
	$(HOST_CC) $(HOST_ASFLAGS) -o $@ -c $<

%.o: %.c
	$(TARGET_CC) $(TARGET_CFLAGS) -c $< -o $@

%.o: %.S
	$(TARGET_CC) $(TARGET_ASFLAGS) -o $@ -c $<

ifdef CONFIG_DEBUG
TARGET_CFLAGS+=-g -O0 -D_DEBUG_
TARGET_ASFLAGS+=-g -O0 -D_DEBUG_
HOST_CFLAGS+=-g -O0 -D_DEBUG_
else
TARGET_CFLAGS+= -O2
HOST_CFLAGS+=-O2
TARGET_CFLAGS+=-fomit-frame-pointer
endif

.PHONY: $(clean-targets) $(config-targets)
dep.mk: $(SRCS)
# don't generate deps  when cleaning
ifeq ($(findstring $(MAKECMDGOALS), $(clean-targets) $(config-targets) ),)
	@for file in $(SRCS) ; do \
		$(TARGET_CC) $(TARGET_CFLAGS) -M $$file ; \
	done > dep.mk
endif

dephost.mk: $(HOST_SRCS)
# don't generate deps  when cleaning
ifeq ($(findstring $(MAKECMDGOALS), $(clean-targets) $(config-targets) ),)
	@for file in $(HOST_SRCS) ; do \
		$(HOST_CC) $(HOST_CFLAGS) -M $$file | sed -e "s/\(.*\).o:/`dirname $$file`\/\1.host.o:/" ; \
	done > dephost.mk
endif

