.PHONY: prtos showsizes scripts

all: prtos

include prtos_config

ifndef PRTOS_PATH
PRTOS_PATH=.
-include path.mk

HOST_ARCH = $(shell uname -m)
ifeq ($(HOST_ARCH), x86_64)
path.mk:
	@exec echo -e "\n# Automatically added by PRTOS" > path.mk
	@exec echo -e "# Please don't modify" >> path.mk
	@exec echo -e "HOST_CFLAGS_ARCH=-m64" >> path.mk
	@exec echo -e "HOST_ASFLAGS_ARCH=-m64" >> path.mk
	@exec echo -e "HOST_LDFLAGS_ARCH=-elf_x86_64" >> path.mk
	@exec echo -e "PRTOS_PATH=`pwd`" >> path.mk
	@exec echo -e "export PRTOS_PATH" >> path.mk
	@cat path.mk >> prtos_config
	@$(RM) -f path.mk
else
path.mk:
	@exec echo -e "\n# Automatically added by PRTOS" > path.mk
	@exec echo -e "# Please don't modify" >> path.mk
	@exec echo -e "HOST_CFLAGS_ARCH=-m32" >> path.mk
	@exec echo -e "HOST_ASFLAGS_ARCH=-m32" >> path.mk
	@exec echo -e "HOST_LDFLAGS_ARCH=-melf_i386" >> path.mk
	@exec echo -e "PRTOS_PATH=`pwd`" >> path.mk
	@exec echo -e "export PRTOS_PATH" >> path.mk
	@cat path.mk >> prtos_config
	@$(RM) -f path.mk
endif

endif

include version
include config.mk

scripts: 
	@exec echo -e "\n> Building Kconfig:";
	@$(MAKE) -s -C scripts/kconfig conf mconf || exit 1

config oldconfig silentoldconfig menuconfig $(defconfig-targets): scripts
ifeq ($(MAKECMDGOALS),menuconfig)
	@exec echo -e "\nYou have to configure three different elements:"
	@exec echo -e "1.- The prtos itself."
	@exec echo -e "2.- The Resident Software, which is charge of loading the system from ROM -> RAM."
	@exec echo -e "3.- The XAL, a basic partition execution environment."
	@exec echo -en "\nPress 'Enter' to configure prtos (step 1)"
	@read dummy
	@$(MAKE) -s -C $(PRTOS_PATH)/core $(MAKECMDGOALS);
	@exec echo -en "Press 'Enter' to configure Resident Sw (step 2)"
	@read dummy
	@$(MAKE) -s -C $(PRTOS_PATH)/user/bootloaders/rsw $(MAKECMDGOALS)
	@exec echo -en "Press 'Enter' to configure XAL (step 3)"
	@read dummy
	@$(MAKE) -s -C $(PRTOS_PATH)/user/xal $(MAKECMDGOALS)
	@exec echo -e "\nNext, you may run 'make'"
else
	@$(MAKE) -s -C $(PRTOS_PATH)/core $(MAKECMDGOALS);
	@$(MAKE) -s -C $(PRTOS_PATH)/user/bootloaders/rsw $(MAKECMDGOALS)
	@$(MAKE) -s -C $(PRTOS_PATH)/user/xal $(MAKECMDGOALS)
endif

prtos: $(PRTOS_PATH)/core/include/autoconf.h
	@exec echo -en "\n> Configuring and building the \"PRTOS Hypervisor\""
	@$(MAKE) -s -C core || exit 1
	@exec echo -en "\n> Configuring and building the \"User Utilities\""
	@$(MAKE) -s -C user || exit 1


distclean: clean
	@make -s -C $(PRTOS_PATH)/user distclean
	@make -C $(PRTOS_PATH)/core clean
	@exec echo -e "> Cleaning up PRTOS";
	@exec echo -e "  - Removing dep.mk Rules.mk files";
	@find -type f \( -name "dep.mk" -o -name dephost.mk \) -exec rm '{}' \;
	@find -type f \( -name ".config" -o -name .config.old \) -exec rm '{}' \;
	@find -type f -name "autoconf.h" -exec rm '{}' \;
	@find -type f -name ".menuconfig.log" -exec rm '{}' \;
	@find -type f \( -name "mconf" -o -name "conf" \) -exec rm '{}' \;

	@find -type f -name "partition?" -exec rm '{}' \;
	@find -type f -name "resident_sw" -exec rm '{}' \;
	@find -type f -name "*.c.prtos_conf" -exec rm '{}' \;
	@find -type f -name "*.bin.prtos_conf" -exec rm '{}' \;
	@find -type f -name "*.xef.prtos_conf" -exec rm '{}' \;
	@find -type f -name "*.xef" -exec rm '{}' \;
	@find -type f -name "prtos_cf" -exec rm '{}' \;

	@find -type f \( -name prtoseformat -o -name prtosbuildinfo -o -name prtospack -o -name prtoscparser -o -name prtoscpartcheck -o -name prtosccheck \) -exec rm '{}' \;

	@find -type l -exec rm '{}' \;
	@$(RM) -rf $(PRTOS_PATH)/core/include/config/*
	@$(RM) -rf $(PRTOS_PATH)/user/xal/include/config/*
	@$(RM) -rf $(PRTOS_PATH)/user/bootloaders/rsw/include/config/* $(PRTOS_PATH)/user/bootloaders/rsw/$(ARCH)/rsw.lds
	@$(RM) -f $(PRTOS_PATH)/prtos_config $(PRTOS_PATH)/core/include/autoconf.h $(PRTOS_PATH)/core/include/$(ARCH)/asm_offsets.h $(PRTOS_PATH)/core/include/$(ARCH)/brksize.h $(PRTOS_PATH)/core/include/$(ARCH)/ginfo.h $(PRTOS_PATH)/scripts/lxdialog/lxdialog $(PRTOS_PATH)/core/prtos_core $(PRTOS_PATH)/core/prtos_core.bin $(PRTOS_PATH)/core/prtos_core.xef $(PRTOS_PATH)/core/build.info $(PRTOS_PATH)/core/kernel/$(ARCH)/prtos.lds $(PRTOS_PATH)/scripts/extractinfo user/bin/rswbuild $(PRTOS_PATH)/core/module.lds $(PRTOS_PATH)/core/module.lds.in
	@$(RM) -f $(PRTOS_PATH)/core/Kconfig.ver $(PRTOS_PATH)/core/include/comp.h
	@$(RM) $(DISTRORUN) $(DISTROTAR)
	@exec echo -e "> Done";

clean:
	@exec echo -e "> Cleaning PRTOS";
	@exec echo -e "  - Removing *.o *.a *~ files";
	@find \( -name "*~" -o -name "*.o" -o -name "*.a" -o -name "*.xo" \) -exec rm '{}' \;
	@find -name "*.gcno" -exec rm '{}' \;
	@find -name "*.bin" -exec rm '{}' \;
	@$(RM) -f $(PRTOS_PATH)/core/build.info $(PRTOS_PATH)/user/tools/prtoscparser/prtos_conf.xsd $(PRTOS_PATH)/$(DISTRO).run
	@exec echo -e "> Done";

DISTRO	= prtos-$(PRTOSVERSION)
DISTROTMP=/tmp/$(DISTRO)-$$PPID
DISTROTAR = $(DISTRO).tar.bz2
$(DISTROTAR): prtos
	@$(RM) $(DISTROTAR)
	@make -s -C user/xal/examples -f Makefile clean
	@mkdir $(DISTROTMP) || exit 0
	@user/bin/prtosdistro $(DISTROTMP)/$(DISTRO) $(DISTROTAR)
	@$(RM) -r $(DISTROTMP)

DISTRORUN = $(DISTRO).run
DISTROLABEL= "prtos binary distribution $(PRTOSVERSION): "
$(DISTRORUN): $(DISTROTAR)
	@which makeself >/dev/null || (echo "Error: makeself program not found; install the makeself package" && exit -1)
	@mkdir $(DISTROTMP) || exit 0
	@tar xf $(DISTROTAR) -C $(DISTROTMP)
	@/bin/echo "> Generating self extracting binary distribution \"$(DISTRORUN)\""
	@makeself --bzip2 $(DISTROTMP)/$(DISTRO) $(DISTRORUN) $(DISTROLABEL) ./prtos-installer > /dev/null 2>&1
	@#| tr "\n" "#" | sed -u "s/[^#]*#/./g; s/..././g"
	@$(RM) $(DISTROTAR)
	@$(RM) -r $(DISTROTMP)
	@/bin/echo -e "> Done\n"

distro-tar: $(DISTROTAR)
distro-run: $(DISTRORUN)
