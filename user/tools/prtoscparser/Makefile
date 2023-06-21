TOOLS=prtoscparser
#prtosproject
#.PHONY: $(TOOLS)
all: $(TOOLS)

INSTALL=$(TOOLS)

include ../../../prtos_config

include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/user/rules.mk
include $(PRTOS_PATH)/user/install.mk
LIB_PRTOS_PATH=$(PRTOS_PATH)/user/libprtos

-include dephost.mk

HOST_CFLAGS += `xml2-config --cflags` -D_GNU_SOURCE -D$(ARCH) -I./ --include prtos_inc/config.h --include prtos_inc/arch/arch_types.h
HOST_LDFLAGS += `xml2-config --libs` -L$(LIB_PRTOS_PATH) -lprtoshost -z noexecstack

prtos_conf.xsd: prtos_conf.xsd.in
	@$(TARGET_CC) $(TARGET_ASFLAGS) -x assembler-with-cpp -E -P $< -o $@.bl
	@sed "/^$$/d" $@.bl > $@

SRCS=main.c process_xml.c prtos_conf.c cgen.c hm.c conv.c checks.c prtoscbuild.c $(ARCH).c rsvmem.c common.c
OBJS=main.host.o process_xml.host.o prtos_conf.host.o cgen.host.o hm.host.o conv.host.o checks.host.o prtoscbuild.host.o $(ARCH).host.o rsvmem.host.o common.host.o

prtoscparser: prtos_conf.xsd  $(OBJS) start_xsd.host.o end_xsd.host.o prtos_conf.xsd
	@exec echo "" | $(HOST_CC) $(HOST_CFLAGS) -x c -c - -o tmp.host.o
	@$(HOST_OBJCOPY) --add-section .rodata.xsd=prtos_conf.xsd tmp.host.o
	$(HOST_CC) -o prtoscparser $(HOST_CFLAGS) $(OBJS) start_xsd.host.o tmp.host.o end_xsd.host.o  $(HOST_LDFLAGS)

distclean: clean

clean:
	@$(RM) -f *.o *~ prtos_conf.xsd.bl $(TOOLS) dephost.mk prtos_conf.xsd

DATE:=$(shell LANG=C date "+%B %Y")
