
INSTALL=prtospack

all: prtospack


include ../../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/user/rules.mk
include $(PRTOS_PATH)/user/install.mk

-include dephost.mk

SRCS=${INSTALL}.c xmlcheck.c
OBJS := $(patsubst %.c,%.host.o,$(SRCS))

HOST_CFLAGS += --include prtos_inc/config.h --include prtos_inc/arch/arch_types.h -I.

${INSTALL}: $(OBJS)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(OBJS) -L$(LIB_PRTOS_PATH) -lprtoshost

%.1: %.pod
	pod2man -c "prtos Utilities" -r -s 1 -n $(<:%.pod=%)  $< -o $@

distclean: clean
	@find -name "*.o" -exec rm '{}' \;
	@find -name "*~" -exec rm '{}' \;
	@find -name "dep.mk" -exec rm '{}' \;
clean:
	@$(RM) -f *.o *~ $(OBJS) ${INSTALL} ${INSTALL}.3 ${INSTALL}.tex dephost.mk

DATE=$(shell LANG=C date "+%B %Y")

${INSTALL}.3: ${INSTALL}.pod
	pod2man  --section=3  --center="prtos tools" --release=prtos-$(PRTOSVERSION)  --date="${DATE}" $< $@

${INSTALL}.tex: ${INSTALL}.pod
	pod2latex -h1level 3  $< $@

