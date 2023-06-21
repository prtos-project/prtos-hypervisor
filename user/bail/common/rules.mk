
$(if $(BAIL_PATH),, \
	$(warning "The configuration variable BAIL_PATH is not set,") \
	$(error "check the \"common/mkconfig.dist\" file (see README)."))

ifneq ($(BAIL_PATH)/common/config.mk, $(wildcard $(BAIL_PATH)/common/config.mk))
BAIL_PATH=../..
PRTOS_PATH=../../../..
else
is_installed := 1
include $(BAIL_PATH)/common/config.mk
endif


# early detect of misconfiguration and missing variables
$(if $(PRTOS_PATH),, \
	$(warning "The configuration variable PRTOS_PATH is not set,") \
	$(error "check the \"common/mkconfig.dist\" file (see README)."))

#INCLUDES OF PRTOS CONFIGURATION
include $(PRTOS_PATH)/prtos_config
include $(PRTOS_PATH)/version
ifdef is_installed
include $(PRTOS_PATH)/lib/rules.mk
else
include $(PRTOS_PATH)/user/rules.mk
endif

#PATHS
ifdef is_installed
PRTOS_BIN_PATH=$(PRTOS_PATH)/bin
PRTOS_CORE_PATH=$(PRTOS_PATH)/lib
else
PRTOS_BIN_PATH=$(PRTOS_PATH)/user/bin
PRTOS_CORE_PATH=$(PRTOS_PATH)/core
endif
BAIL_LIB_PATH=$(BAIL_PATH)/lib
BAIL_BIN_PATH=$(BAIL_PATH)/bin

# APPLICATIONS
PRTOS_CPARSER=$(PRTOS_BIN_PATH)/prtoscparser
PRTOS_PACK=$(PRTOS_BIN_PATH)/prtospack
RSW_BUILD=$(PRTOS_BIN_PATH)/rswbuild
PEF=$(PRTOS_BIN_PATH)/prtoseformat build
GRUB_ISO=$(PRTOS_BIN_PATH)/grub_iso
XPATHSTART=$(BAIL_BIN_PATH)/xpathstart

# PRTOS CORE
PRTOS_CORE_ELF=$(PRTOS_CORE_PATH)/prtos_core
PRTOS_CORE_BIN=$(PRTOS_CORE_PATH)/prtos_core.bin
PRTOS_CORE=$(PRTOS_CORE_PATH)/prtos_core.pef

#LIBRARIES
LIB_PRTOS=-lprtos
LIB_BAIL=-lbail

#FLAGS
TARGET_CFLAGS += -I$(BAIL_PATH)/include -fno-builtin

if-success = $(shell { $(1); } >/dev/null 2>&1 && echo "$(2)" || echo "$(3)")

ifneq ($(EXTERNAL_LDFLAGS),y)
TARGET_LDFLAGS += -u start -u prtos_image_hdr -T$(BAIL_LIB_PATH)/loader.lds\
	-L$(LIB_PRTOS_PATH) -L$(BAIL_LIB_PATH)\
	--start-group $(LIBGCC) $(LIB_PRTOS) $(LIB_BAIL) --end-group -z noexecstack
endif
TARGET_LDFLAGS += $(call if-success,$(TARGET_LD) -v --no-warn-rwx-segments,--no-warn-rwx-segments,)

# ADDRESS OF EACH PARTITION
# function usage: $(call xpathstart,partitionid,xmlfile)
# xpathstart = $(shell $(XPATHSTART) $(1) $(2))
xpathstart = $(shell $(BAIL_PATH)/bin/xpath -c -f $(2) '/prtos:SystemDescription/prtos:PartitionTable/prtos:Partition['$(1)']/prtos:PhysicalMemoryAreas/prtos:Area[1]/@start')


%.pef:  %
	$(PEF) $< -c -o $@

prtos_cf.pef.prtos_conf: prtos_cf.bin.prtos_conf
	$(PEF) -m $< -c -o $@

prtos_cf.bin.prtos_conf: prtos_cf.$(ARCH).xml # $(XMLCF)
	$(PRTOS_CPARSER) -o $@ $^

prtos_cf.c.prtos_conf: prtos_cf.$(ARCH).xml # $(XMLCF)
	$(PRTOS_CPARSER) -c -o $@ $^


resident_sw: container.bin
	$(RSW_BUILD) $^ $@
	
resident_sw.iso: resident_sw
	$(GRUB_ISO) $@ $^

distclean: clean
	@$(RM) *~

clean:
	@$(RM) $(PARTITIONS) $(patsubst %.bin,%, $(PARTITIONS)) $(patsubst %.pef,%, $(PARTITIONS))
	@$(RM) container.bin resident_sw prtos_cf prtos_cf.bin prtos_cf.*.prtos_conf
	@$(RM) *.o *.*.prtos_conf dep.mk
	@$(RM) *.iso
