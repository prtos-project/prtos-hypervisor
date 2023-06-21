# XAL_PATH: path to the PRTOS directory
XAL_PATH=../../

all: container.bin resident_sw
include $(XAL_PATH)/common/rules.mk

# XMLCF: path to the XML configuration file
XMLCF=prtos_cf.$(ARCH).xml

# PARTITIONS: partition files (xef format) composing the example
PARTITIONS=partition0.xef partition1.xef partition2.xef

partition0: partition0.o
	$(TARGET_LD) -o $@ $^ $(TARGET_LDFLAGS) -Ttext=$(shell $(XPATHSTART) 0 $(XMLCF))
	
partition1: partition1.o
	$(TARGET_LD) -o $@ $^ $(TARGET_LDFLAGS) -Ttext=$(shell $(XPATHSTART) 1 $(XMLCF))
	
partition2: partition2.o
	$(TARGET_LD) -o $@ $^ $(TARGET_LDFLAGS) -Ttext=$(shell $(XPATHSTART) 2 $(XMLCF))

PACK_ARGS=-h $(PRTOSCORE):prtos_cf.xef.prtos_conf \
	-p 0:partition0.xef \
	-p 1:partition1.xef \
	-p 2:partition2.xef
	
container.bin: $(PARTITIONS) prtos_cf.xef.prtos_conf
	$(PRTOSPACK) check prtos_cf.xef.prtos_conf $(PACK_ARGS)
	$(PRTOSPACK) build $(PACK_ARGS) $@

include ../run.mk
	
