# can be overriden by Makefiles before include install.mk
INSTALLDIR ?= $(PRTOS_PATH)/user/bin

INSTALLFILES=$(patsubst %,$(INSTALLDIR)/%,$(INSTALL))

$(INSTALLDIR)/%: %
	@cp $(*) $(INSTALLDIR)/$(*)


install:	$(INSTALLFILES)

uninstall:
	@rm -f $(INSTALLFILES)

.PHONY: install uninstall
