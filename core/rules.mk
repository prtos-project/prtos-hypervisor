PRTOS_CORE_PATH=$(PRTOS_PATH)/core

if-success = $(shell { $(1); } >/dev/null 2>&1 && echo "$(2)" || echo "$(3)")

check_gcc = $(shell if $(TARGET_CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi)

TARGET_CFLAGS = -fno-pie -Wall -D_PRTOS_KERNEL_ -fno-builtin -nostdlib -nostdinc -D$(ARCH) -fno-strict-aliasing -D"__PRTOS_INCFLD(_fld)=<_fld>"
TARGET_CFLAGS += -I$(PRTOS_CORE_PATH)/include --include config.h --include $(ARCH)/arch_types.h 


TARGET_CFLAGS += $(TARGET_CFLAGS_ARCH)

TARGET_CFLAGS += $(call check_gcc,-Wno-pointer-sign,)
TARGET_CFLAGS += $(call check_gcc,-fno-stack-protector,)
TARGET_CFLAGS += $(call check_gcc,-Wno-unused-but-set-variable,)
TARGET_CFLAGS += $(call check_gcc,-fno-pie,)
TARGET_CFLAGS += $(call check_gcc,-Wno-int-in-bool-context,)
TARGET_CFLAGS += $(call check_gcc,-Wno-address-of-packed-member,)
TARGET_CFLAGS += $(call check_gcc,-Wno-array-bounds,)
TARGET_CFLAGS += $(call check_gcc,--std=gnu89,)

ifndef CONFIG_NO_GCC_OPT
 TARGET_CFLAGS += -O1
else
 TARGET_CFLAGS += -O0
endif

TARGET_ASFLAGS = -Wall -D__ASSEMBLY__ -D_PRTOS_KERNEL_ -fno-builtin -D$(ARCH) -nostdlib -nostdinc -D"__PRTOS_INCFLD(_fld)=<_fld>"
TARGET_ASFLAGS += -I$(PRTOS_CORE_PATH)/include --include config.h

TARGET_ASFLAGS += $(TARGET_ASFLAGS_ARCH)

LIBGCC = `$(TARGET_CC) -print-libgcc-file-name $(TARGET_CFLAGS)`

TARGET_LDFLAGS =

TARGET_LDFLAGS += $(TARGET_LDFLAGS_ARCH) -z noexecstack
TARGET_LDFLAGS += $(call if-success,$(TARGET_LD) -v --no-warn-rwx-segments,--no-warn-rwx-segments,)


ifdef CONFIG_DEBUG
	TARGET_CFLAGS+=-g
	TARGET_ASFLAGS+=-g
else
	TARGET_CFLAGS+=-fomit-frame-pointer
endif

%.o: %.c
	$(TARGET_CC) $(TARGET_CFLAGS)  -c $< -o $@

%.o: %.S
	$(TARGET_CC) $(TARGET_ASFLAGS)  -c $< -o $@

%.xo: %.c
	@$(TARGET_CC) $(TARGET_CFLAGS) -c $< -o $@.1
	@$(TARGET_OBJCOPY) -O binary -j .esymb $@.1 $@.symb
	@$(TARGET_OBJCOPY) -X --keep-global-symbols=$@.symb $@.1 $@
	@$(RM) -f $@.1 $@.symb

%.xo: %.o
	@$(TARGET_OBJCOPY) -O binary -j .esymb $< $@.symb
	@$(TARGET_OBJCOPY) -X --keep-global-symbols=$@.symb $< $@

dep.mk: $(SRCS)
# don't generate deps  when cleaning
ifeq ($(findstring $(MAKECMDGOALS), clean distclean),)
	@for file in $(SRCS) ; do \
		$(TARGET_CC) $(TARGET_CFLAGS) -M $$file ; \
	done > dep.mk
endif

#distclean:
#	@rm -f *.o *~ dep.mk

