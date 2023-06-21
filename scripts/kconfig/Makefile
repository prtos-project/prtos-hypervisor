.PHONY: menuconfig dochecklxdialog

all:

include ../../prtos_config

check_gcc = $(shell if $(TARGET_CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi)

HOST_CFLAGS = -Wall -O2 -D$(ARCH) -Wno-unused-value
# disable warning: variable set but not used
HOST_CFLAGS += $(call check_gcc,-Wno-unused-but-set-variable,)

%.o: %.c
	$(HOST_CC) $(HOST_CFLAGS) -o $@ -c $<

Kconfig := kernel/$(ARCH)/Kconfig

lxdialog := lxdialog/checklist.o lxdialog/util.o lxdialog/inputbox.o
lxdialog += lxdialog/textbox.o lxdialog/yesno.o lxdialog/menubox.o
check-lxdialog := $(PRTOS_PATH)/scripts/kconfig/lxdialog/check-lxdialog.sh

HOST_EXTRACFLAGS = $(shell $(CONFIG_SHELL) $(check-lxdialog) -ccflags)
HOST_EXTRALIBS = $(shell $(CONFIG_SHELL) $(check-lxdialog) -ldflags $(HOST_CC))

HOST_CFLAGS += $(HOST_EXTRACFLAGS) -DLOCALE

conf-objs := conf.o zconf.tab.o
mconf-objs := mconf.o zconf.tab.o $(lxdialog)

dochecklxdialog:
	@$(CONFIG_SHELL) $(check-lxdialog) -check $(HOST_CC) $(HOST_EXTRACFLAGS) $(HOST_EXTRALIBS)

mconf: dochecklxdialog $(mconf-objs)
	@$(HOST_CC) $(HOST_EXTRACFLAGS) -o $@ $(mconf-objs) $(HOST_EXTRALIBS)

conf: $(conf-objs)
	@$(HOST_CC) $(HOST_EXTRACFLAGS) -o $@ $(conf-objs) $(HOST_EXTRALIBS)

#LKC_GENPARSER := 1

ifdef LKC_GENPARSER

$(obj)/zconf.tab.c: $(src)/zconf.y
$(obj)/lex.zconf.c: $(src)/zconf.l
$(obj)/zconf.hash.c: $(src)/zconf.gperf

%.tab.c: %.y
	bison -l -b $* -p $(notdir $*) $<
	cp $@ $@_shipped

lex.%.c: %.l
	flex -L -P$(notdir $*) -o$@ $<
	cp $@ $@_shipped

%.hash.c: %.gperf
	gperf < $< > $@
	cp $@ $@_shipped

endif

clean:
	@$(RM) -f *.o mconf conf lxdialog/*.o *~ lxdialog/*~
