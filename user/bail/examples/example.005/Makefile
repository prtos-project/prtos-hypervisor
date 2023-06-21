# BAIL_PATH: path to the PRTOS directory
BAIL_PATH=../../

all: container.bin resident_sw
include $(BAIL_PATH)/common/rules.mk

# XMLCF: path to the XML configuration file
XMLCF=prtos_cf.$(ARCH).xml

# PARTITIONS: partition files (pef format) composing the example
PARTITIONS=partition0.pef

partition0: partition.o
	$(TARGET_LD) -o $@ $^ $(TARGET_LDFLAGS) -Ttext=$(shell $(XPATHSTART) 0 $(XMLCF))

PACK_ARGS=-h $(PRTOS_CORE):prtos_cf.pef.prtos_conf \
	-p 0:partition0.pef:custom_file.pef
	
custom_file.pef: custom_file.txt
	$(PEF) -m -o $@ $<

container.bin: $(PARTITIONS) prtos_cf.pef.prtos_conf custom_file.pef
	$(PRTOS_PACK) check prtos_cf.pef.prtos_conf $(PACK_ARGS)
	$(PRTOS_PACK) build $(PACK_ARGS) $@

include ../run.mk
	
