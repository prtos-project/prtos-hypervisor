.DEFAULT: all

$(if $(PRTOS_PATH),, \
	$(warning "The configuration variable PRTOS_PATH is not set,") \
	$(error "check the \"prtos_config\" file (see README)."))

$(if $(wildcard $(PRTOS_PATH)/version),, \
	$(warning "No version file found at $(PRTOS_PATH),") \
	$(error "see the README in the base directory"))

include $(PRTOS_PATH)/version

clean-targets := clean distclean depend
config-targets := scripts config xconfig menuconfig oldconfig silentconfig silentoldconfig
defconfig-targets := $(shell cd $(PRTOS_PATH)/core/kernel/$(ARCH)/ 2>/dev/null && ls defconfig*)
.PHONY: $(clean-targets) $(config-targets) $(defconfig-targets)

# skip .config when configuring
ifeq ($(findstring $(MAKECMDGOALS), $(config-targets) $(clean-targets) $(defconfig-targets)),)
need_config := 1
endif

# check if the .config exists
ifeq ($(PRTOS_PATH)/core/.config, $(wildcard $(PRTOS_PATH)/core/.config))
exists_config := 1
endif

# if there's no .config file warn the user and abort
$(if $(need_config), \
	$(if $(exists_config),, \
	$(warning "No .config file found at $(PRTOS_PATH)/core,") \
	$(error "run `make menuconfig` in the base directory")))

# if the .config is needed include it
ifdef exists_config
include $(PRTOS_PATH)/core/.config
# If .config is newer than core/include/autoconf.h, someone tinkered
# with it and forgot to run make oldconfig.

$(PRTOS_PATH)/core/include/autoconf.h: $(PRTOS_PATH)/core/.config
	@$(MAKE) -C $(PRTOS_PATH)/core silentoldconfig MAKEFLAGS=$(patsubst -j%,,$(MAKEFLAGS))

else
# Dummy target needed, because used as prerequisite
$(PRTOS_PATH)/core/include/autoconf.h: ;
endif

CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
	else if [ -x /bin/bash ]; then echo /bin/bash; \
	else echo sh; fi ; fi)
