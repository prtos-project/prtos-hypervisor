INSTALL=prtoseformat
all: i

include ../../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/user/rules.mk
include $(PRTOS_PATH)/user/install.mk

LIBPRTOSHOST=$(PRTOS_PATH)/user/libprtos/libprtoshost.a

$(LIBPRTOSHOST):
	@$(MAKE) -C $(PRTOS_PATH)/user/libprtos libprtoshost.a

-include dephost.mk

XEF_SRCS=prtoseformat.c
XEF_OBJS := $(patsubst %.c,%.host.o,$(XEF_SRCS))

HOST_CFLAGS += --include prtos_inc/config.h --include prtos_inc/arch/arch_types.h -I./

INSTALL=prtoseformat

i: $(INSTALL)

prtoseformat: $(LIBHOSTXEF) $(XEF_OBJS) $(LIBPRTOSHOST)
	@$(HOST_CC) $(HOST_CFLAGS) -o $@ $(XEF_OBJS) -L$(LIBPRTOS_PATH) -lprtoshost

SRCS=$(XEF_SRCS)

clean:
	@$(RM) -f *.o *~ ${INSTALL} $(ARCH)/*~ $(ARCH)/*.o dephost.mk ${INSTALL}.tex ${INSTALL}.3

DATE=$(shell LANG=C date "+%B %Y")

prtoseformat.3: prtoseformat.pod
	pod2man  --section=3  --center="prtos tools" --release=prtos-$(PRTOSVERSION)  --date="${DATE}" $< $@

prtoseformat.tex: prtoseformat.pod
	pod2latex -h1level 3  $< $@
