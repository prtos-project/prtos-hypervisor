all: prtosbuildinfo

INSTALL=prtosbuildinfo

include ../../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/user/rules.mk
include $(PRTOS_PATH)/user/install.mk

prtosbuildinfo: prtosbuildinfo.in
	# replace by s/.../$(\1)/
	cp $^ $@
	sed -i "s|\$$TARGET_CC |$(TARGET_CC) |g" $@
	sed -i 's|\$$TARGET_CFLAGS |$(TARGET_CFLAGS) |g' $@
	sed -i "s|\$$TARGET_CFLAGS_ARCH |$(TARGET_CFLAGS_ARCH) |g" $@
	sed -i "s|\$$TARGET_LD |$(TARGET_LD) |g" $@
	sed -i "s|\$$TARGET_LDFLAGS_ARCH |$(TARGET_LDFLAGS_ARCH) |g" $@
	sed -i "s|\$$TARGET_AS |$(TARGET_AS) |g" $@
	sed -i "s|\$$TARGET_OBJCOPY |$(TARGET_OBJCOPY) |g" $@

	sed -i "s|\$$PRTOS_VERSION|$(PRTOS_VERSION)|g" $@
	sed -i "s|\$$PRTOS_SUBVERSION|$(PRTOS_SUBVERSION)|g" $@
	sed -i "s|\$$PRTOS_REVISION|$(PRTOS_REVISION)|g" $@
