all: libs

include ../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/user/rules.mk

XAL_PATH	= $(PRTOS_PATH)/user/xal
COMMON_PATH	= $(XAL_PATH)/common
ARCH_PATH	= $(XAL_PATH)/$(ARCH)

TARGET_CFLAGS += -I$(XAL_PATH)/include -I$(ARCH_PATH) --include config.h
TARGET_ASFLAGS += -I$(XAL_PATH)/include -I$(ARCH_PATH) --include config.h

TARG=lib/libxal.a
OBJS=\
	$(patsubst %.c,%.o, $(wildcard $(COMMON_PATH)/*.c))\
	$(patsubst %.S,%.o, $(wildcard $(ARCH_PATH)/*.S))\
	$(patsubst %.c,%.o, $(wildcard $(ARCH_PATH)/*.c))

libs: links $(TARG)
$(TARG): $(OBJS)
	@$(AR) cr $@ $^
	@cp $(ARCH)/loader.lds lib

KCONFIG_AUTOHEADER=$(XAL_PATH)/include/autoconf.h
KCONFIG_AUTOCONFIG=$(XAL_PATH)/include/config/auto.conf
$(KCONFIG_AUTOHEADER): silentoldconfig

links: $(KCONFIG_AUTOHEADER)
	@if [ ! -d $(XAL_PATH)/include/arch ] ; then \
		ln -sf $(XAL_PATH)/include/$(ARCH) $(XAL_PATH)/include/arch; \
	fi

KCONFIG=$(XAL_PATH)/$(ARCH)/Kconfig
DEFCONFIG=$(XAL_PATH)/$(ARCH)/defconfig

conf:
	@$(MAKE) -C $(PRTOS_PATH)/scripts/kconfig conf

mconf:
	@$(MAKE) -C $(PRTOS_PATH)/scripts/kconfig mconf

config: conf
	@$(PRTOS_PATH)/scripts/kconfig/conf $(KCONFIG)

oldconfig: conf
	@$(PRTOS_PATH)/scripts/kconfig/conf -o $(KCONFIG)

silentoldconfig:
	@$(PRTOS_PATH)/scripts/kconfig/conf -s $(KCONFIG)

menuconfig: mconf
	$(PRTOS_PATH)/scripts/kconfig/mconf $(KCONFIG)

defconfig:
	@$(PRTOS_PATH)/scripts/kconfig/conf -D $(DEFCONFIG) $(KCONFIG)

clean:
	@exec echo -e "> Cleaning XAL";
	@exec echo -e "  - Removing *.o *.a *~ files";
	@$(RM) $(TARG)
	@$(RM) $(ARCH_PATH)/*.o $(COMMON_PATH)/*.o
	@$(RM) lib/loader.lds
	@find -name "*~" -exec rm '{}' \;
	@find -name "*.o" -exec rm '{}' \;
	@find -name "*.a" -exec rm '{}' \;
	@find -name "dep.mk" -exec rm '{}' \;
	@exec echo -e "> Done";

distclean: clean
	@exec echo -e "> Cleaning up XAL";
	@find -type f \( -name "dep.mk" -o -name dephost.mk \) -exec rm '{}' \;
	@find -type f \( -name ".config" -o -name .config.old \) -exec rm '{}' \;
	@find -type f -name "autoconf.h" -exec rm '{}' \;
	@find -type f -name ".menuconfig.log" -exec rm '{}' \;
	@find -type f \( -name "mconf" -o -name "conf" \) -exec rm '{}' \;
	@find -type l -exec rm '{}' \;
	@$(RM) -rf $(XAL_PATH)/include/config/* $(XAL_PATH)/lithosconfig.mk
	@$(RM) -f $(XAL_PATH)/include/arch
	@exec echo -e "> Done";
