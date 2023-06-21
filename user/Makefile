all: links user
include ../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/user/rules.mk

PRTOSUSR_SUBDIRS = libprtos tools xal bootloaders/rsw

.PHONY: links clean_links

links:
	@if test -d $(LIBPRTOS_PATH)/include/prtos_inc; then \
		$(MAKE) -s -C . clean-silent;\
	fi
	@if ! test -d $(LIBPRTOS_PATH)/include/arch; then \
		ln -sf $(LIBPRTOS_PATH)/include/$(ARCH) $(LIBPRTOS_PATH)/include/arch; \
	fi
	@if ! test -d $(LIBPRTOS_PATH)/include/prtos_inc; then \
		ln -sf $(PRTOS_CORE_PATH)/include $(LIBPRTOS_PATH)/include/prtos_inc; \
	fi
	@if ! test -f $(LIBPRTOS_PATH)/common/md5.c; then \
		ln -s $(PRTOS_PATH)/core/klibc/md5.c $(LIBPRTOS_PATH)/common/md5.c ; \
	fi

clean_links:
	@$(RM) -f $(LIBPRTOS_PATH)/include/arch $(LIBPRTOS_PATH)/include/prtos_inc

user:
	@exec echo -e "\n> Building PRTOS User";
	@for dir in $(PRTOSUSR_SUBDIRS) ; do \
		echo "  - $$dir"; \
		$(MAKE) -C $$dir all ; \
	done
	@exec echo "> Done";

distclean: clean_links
	@exec echo -e "\n> Cleaning PRTOS User";
	@for dir in $(PRTOSUSR_SUBDIRS) ; do \
		echo "  - $$dir"; \
		$(MAKE) -C $$dir distclean ; \
	done
	@exec echo "> Done";
clean:
	@exec echo -e "> Cleaning PRTOS User";
	@exec echo -e "  - Removing *.o *.a *~ files";
	$(MAKE) -s -C . clean-silent
	@exec echo -e "> Done";

clean-silent:
	@find -name "*~" -exec rm '{}' \;
	@find -name "*.o" -exec rm '{}' \;
	@find -name "*.a" -exec rm '{}' \;

