
$(if $(XAL_PATH),, \
	$(warning "The configuration variable XAL_PATH is not set,") \
	$(error "check the \"common/mkconfig.dist\" file (see README)."))

ifneq ($(XAL_PATH)/common/config.mk, $(wildcard $(XAL_PATH)/common/config.mk))
XAL_PATH=../..
PRTOS_PATH=../../../..
else
is_installed := 1
include $(XAL_PATH)/common/config.mk
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
PRTOSBIN_PATH=$(PRTOS_PATH)/bin
PRTOSCORE_PATH=$(PRTOS_PATH)/lib
else
PRTOSBIN_PATH=$(PRTOS_PATH)/user/bin
PRTOSCORE_PATH=$(PRTOS_PATH)/core
endif
XALLIB_PATH=$(XAL_PATH)/lib
XALBIN_PATH=$(XAL_PATH)/bin

# APPLICATIONS
PRTOSCPARSER=$(PRTOSBIN_PATH)/prtoscparser
PRTOSPACK=$(PRTOSBIN_PATH)/prtospack
RSWBUILD=$(PRTOSBIN_PATH)/rswbuild
XEF=$(PRTOSBIN_PATH)/prtoseformat build
GRUBISO=$(PRTOSBIN_PATH)/grub_iso
XPATHSTART=$(XALBIN_PATH)/xpathstart

# PRTOS CORE
PRTOSCORE_ELF=$(PRTOSCORE_PATH)/prtos_core
PRTOSCORE_BIN=$(PRTOSCORE_PATH)/prtos_core.bin
PRTOSCORE=$(PRTOSCORE_PATH)/prtos_core.xef

#LIBRARIES
LIB_PRTOS=-lprtos
LIB_XAL=-lxal

#FLAGS
TARGET_CFLAGS += -I$(XAL_PATH)/include -fno-builtin

if-success = $(shell { $(1); } >/dev/null 2>&1 && echo "$(2)" || echo "$(3)")

ifneq ($(EXTERNAL_LDFLAGS),y)
TARGET_LDFLAGS += -u start -u prtos_image_hdr -T$(XALLIB_PATH)/loader.lds\
	-L$(LIBPRTOS_PATH) -L$(XALLIB_PATH)\
	--start-group $(LIBGCC) $(LIB_PRTOS) $(LIB_XAL) --end-group -z noexecstack
endif
TARGET_LDFLAGS += $(call if-success,$(TARGET_LD) -v --no-warn-rwx-segments,--no-warn-rwx-segments,)

# ADDRESS OF EACH PARTITION
# function usage: $(call xpathstart,partitionid,xmlfile)
# xpathstart = $(shell $(XPATHSTART) $(1) $(2))
xpathstart = $(shell $(XAL_PATH)/bin/xpath -c -f $(2) '/prtos:SystemDescription/prtos:PartitionTable/prtos:Partition['$(1)']/prtos:PhysicalMemoryAreas/prtos:Area[1]/@start')


%.xef:  %
	$(XEF) $< -c -o $@

%.xef.prtos_conf: %.bin.prtos_conf
	$(XEF) -m $< -c -o $@

prtos_cf.bin.prtos_conf: prtos_cf.$(ARCH).xml # $(XMLCF)
	$(PRTOSCPARSER) -o $@ $^

prtos_cf.c.prtos_conf: prtos_cf.$(ARCH).xml # $(XMLCF)
	$(PRTOSCPARSER) -c -o $@ $^


resident_sw: container.bin
	$(RSWBUILD) $^ $@
	
resident_sw.iso: resident_sw
	$(GRUBISO) $@ $^

distclean: clean
	@$(RM) *~

clean:
	@$(RM) $(PARTITIONS) $(patsubst %.bin,%, $(PARTITIONS)) $(patsubst %.xef,%, $(PARTITIONS))
	@$(RM) container.bin resident_sw prtos_cf prtos_cf.bin prtos_cf.*.prtos_conf
	@$(RM) *.o *.*.prtos_conf dep.mk
	@$(RM) *.iso
