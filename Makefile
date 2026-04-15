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

EXTRA_CLEAN_FILES = \
	core/include/amd64/asm_offsets.h \
	core/include/amd64/ginfo.h \
	core/include/config/amd64.h \
	core/include/config/vmx.h \
	core/kernel/amd64/dep.mk \
	core/kernel/amd64/prtos.lds \
	core/klibc/amd64/dep.mk \
	user/bail/examples/freertos_hw_virt_aarch64/freertos_hw_virt_aarch64.output \
	user/bail/examples/freertos_hw_virt_aarch64/partition \
	user/bail/examples/freertos_hw_virt_aarch64/prtos_cf.bin.prtos_conf \
	user/bail/examples/freertos_hw_virt_amd64/freertos_hw_virt_amd64.output \
	user/bail/examples/freertos_hw_virt_amd64/partition \
	user/bail/examples/freertos_hw_virt_amd64/prtos_cf.bin.prtos_conf \
	user/bail/examples/freertos_hw_virt_amd64/resident_sw.iso \
	user/bail/examples/freertos_hw_virt_riscv/freertos_hw_virt_riscv.output \
	user/bail/examples/freertos_hw_virt_riscv/partition \
	user/bail/examples/freertos_hw_virt_riscv/prtos_cf.bin.prtos_conf \
	user/bail/examples/freertos_para_virt_aarch64/freertos_para_virt_aarch64.output \
	user/bail/examples/freertos_para_virt_aarch64/partition \
	user/bail/examples/freertos_para_virt_aarch64/prtos_cf.bin.prtos_conf \
	user/bail/examples/freertos_para_virt_amd64/freertos_para_virt_amd64.output \
	user/bail/examples/freertos_para_virt_amd64/partition \
	user/bail/examples/freertos_para_virt_amd64/prtos_cf.bin.prtos_conf \
	user/bail/examples/freertos_para_virt_amd64/resident_sw.iso \
	user/bail/examples/freertos_para_virt_riscv/freertos_para_virt_riscv.output \
	user/bail/examples/freertos_para_virt_riscv/partition \
	user/bail/examples/freertos_para_virt_riscv/prtos_cf.bin.prtos_conf \
	user/bail/examples/linux_4vcpu_1partion_aarch64/linux_guest.dtb \
	user/bail/examples/linux_4vcpu_1partion_aarch64/partition \
	user/bail/examples/linux_4vcpu_1partion_aarch64/prtos_cf.bin.prtos_conf \
	user/bail/examples/linux_4vcpu_1partion_amd64/bzImage \
	user/bail/examples/linux_4vcpu_1partion_amd64/partition \
	user/bail/examples/linux_4vcpu_1partion_amd64/prtos_cf.bin.prtos_conf \
	user/bail/examples/linux_4vcpu_1partion_amd64/resident_sw.iso \
	user/bail/examples/linux_4vcpu_1partion_riscv64/partition \
	user/bail/examples/linux_4vcpu_1partion_riscv64/prtos_cf.bin.prtos_conf \
	user/bail/examples/linux_aarch64/Image \
	user/bail/examples/linux_aarch64/linux_guest.dtb \
	user/bail/examples/linux_aarch64/partition \
	user/bail/examples/linux_aarch64/prtos_cf.bin.prtos_conf \
	user/bail/examples/mix_os_demo_aarch64/Image \
	user/bail/examples/mix_os_demo_aarch64/linux_guest.dtb \
	user/bail/examples/mix_os_demo_aarch64/partition_freertos \
	user/bail/examples/mix_os_demo_aarch64/partition_linux \
	user/bail/examples/mix_os_demo_aarch64/prtos_cf.bin.prtos_conf \
	user/bail/examples/mix_os_demo_amd64/bzImage \
	user/bail/examples/mix_os_demo_amd64/partition_freertos \
	user/bail/examples/mix_os_demo_amd64/partition_linux \
	user/bail/examples/mix_os_demo_amd64/prtos_cf.bin.prtos_conf \
	user/bail/examples/mix_os_demo_amd64/resident_sw.iso \
	user/bail/examples/mix_os_demo_riscv64/partition_freertos \
	user/bail/examples/mix_os_demo_riscv64/partition_linux \
	user/bail/examples/mix_os_demo_riscv64/prtos_cf.bin.prtos_conf \
	user/bail/examples/virtio_linux_demo_2p_aarch64/Image \
	user/bail/examples/virtio_linux_demo_2p_aarch64/guest_rootfs_overlay.cpio \
	user/bail/examples/virtio_linux_demo_2p_aarch64/linux_guest.dtb \
	user/bail/examples/virtio_linux_demo_2p_aarch64/linux_system.dtb \
	user/bail/examples/virtio_linux_demo_2p_aarch64/partition_guest \
	user/bail/examples/virtio_linux_demo_2p_aarch64/partition_system \
	user/bail/examples/virtio_linux_demo_2p_aarch64/prtos_cf.bin.prtos_conf \
	user/bail/examples/virtio_linux_demo_2p_aarch64/prtos_manager \
	user/bail/examples/virtio_linux_demo_2p_aarch64/rootfs_overlay.cpio \
	user/bail/examples/virtio_linux_demo_2p_aarch64/set_serial_poll \
	user/bail/examples/virtio_linux_demo_2p_aarch64/virtio_backend \
	user/bail/examples/virtio_linux_demo_2p_aarch64/virtio_frontend \
	user/bail/examples/virtio_linux_demo_2p_amd64/bzImage \
	user/bail/examples/virtio_linux_demo_2p_amd64/disk.img \
	user/bail/examples/virtio_linux_demo_2p_amd64/guest_rootfs_overlay.cpio \
	user/bail/examples/virtio_linux_demo_2p_amd64/obj_exc_elf \
	user/bail/examples/virtio_linux_demo_2p_amd64/partition_guest \
	user/bail/examples/virtio_linux_demo_2p_amd64/partition_guest.pef \
	user/bail/examples/virtio_linux_demo_2p_amd64/partition_system \
	user/bail/examples/virtio_linux_demo_2p_amd64/partition_system.pef \
	user/bail/examples/virtio_linux_demo_2p_amd64/prtos_cf.amd64.xml \
	user/bail/examples/virtio_linux_demo_2p_amd64/prtos_cf.bin.prtos_conf \
	user/bail/examples/virtio_linux_demo_2p_amd64/prtos_cf.pef.prtos_conf \
	user/bail/examples/virtio_linux_demo_2p_amd64/prtos_manager \
	user/bail/examples/virtio_linux_demo_2p_amd64/resident_sw \
	user/bail/examples/virtio_linux_demo_2p_amd64/resident_sw.iso \
	user/bail/examples/virtio_linux_demo_2p_amd64/rootfs_overlay.cpio \
	user/bail/examples/virtio_linux_demo_2p_amd64/set_serial_poll \
	user/bail/examples/virtio_linux_demo_2p_amd64/virtio_backend \
	user/bail/examples/virtio_linux_demo_2p_amd64/virtio_frontend \
	user/bail/examples/virtio_linux_demo_2p_riscv64/Image \
	user/bail/examples/virtio_linux_demo_2p_riscv64/guest_rootfs_overlay.cpio \
	user/bail/examples/virtio_linux_demo_2p_riscv64/linux_guest.dtb \
	user/bail/examples/virtio_linux_demo_2p_riscv64/linux_system.dtb \
	user/bail/examples/virtio_linux_demo_2p_riscv64/partition_guest \
	user/bail/examples/virtio_linux_demo_2p_riscv64/partition_system \
	user/bail/examples/virtio_linux_demo_2p_riscv64/prtos_cf.bin.prtos_conf \
	user/bail/examples/virtio_linux_demo_2p_riscv64/prtos_manager \
	user/bail/examples/virtio_linux_demo_2p_riscv64/rootfs_overlay.cpio \
	user/bail/examples/virtio_linux_demo_2p_riscv64/set_serial_poll \
	user/bail/examples/virtio_linux_demo_2p_riscv64/virtio_backend \
	user/bail/examples/virtio_linux_demo_2p_riscv64/virtio_frontend \
	user/bootloaders/rsw/amd64/rsw.lds \
	core/include/aarch64/asm_offsets.h \
	core/include/aarch64/ginfo.h \
	core/include/config/aarch64.h \
	core/kernel/aarch64/prtos.lds \
	core/klibc/aarch64/dep.mk \
	user/bail/examples/example.001/obj_exc_elf \
	user/bail/examples/example.001/resident_sw_image \
	user/bail/examples/example.002/obj_exc_elf \
	user/bail/examples/example.002/resident_sw_image \
	user/bail/examples/example.003/obj_exc_elf \
	user/bail/examples/example.003/resident_sw_image \
	user/bail/examples/example.004/obj_exc_elf \
	user/bail/examples/example.004/resident_sw_image \
	user/bail/examples/example.005/obj_exc_elf \
	user/bail/examples/example.005/resident_sw_image \
	user/bail/examples/example.006/obj_exc_elf \
	user/bail/examples/example.006/resident_sw_image \
	user/bail/examples/example.007/obj_exc_elf \
	user/bail/examples/example.007/resident_sw_image \
	user/bail/examples/example.008/obj_exc_elf \
	user/bail/examples/example.008/resident_sw_image \
	user/bootloaders/rsw/aarch64/rsw.lds \
	user/bail/examples/linux_4vcpu_1partition_riscv64/Image \
	user/bail/examples/mix_os_demo_riscv64/Image

scripts: 
	@exec echo -e "\n> Building Kconfig:";
	@$(MAKE) -s -C scripts/kconfig conf mconf || exit 1

config oldconfig silentoldconfig menuconfig $(defconfig-targets): scripts
ifeq ($(MAKECMDGOALS),menuconfig)
	@exec echo -e "\nYou have to configure three different elements:"
	@exec echo -e "1.- The PRTOS Core."
	@exec echo -e "2.- The Resident Software, which is charge of loading the system from ROM -> RAM."
	@exec echo -e "3.- The BAIL(Bare-metal Application Interface Library), a basic partition \"C\" execution environment."
	@exec echo -en "\nPress 'Enter' to configure prtos (step 1)"
	@read dummy
	@$(MAKE) -s -C $(PRTOS_PATH)/core $(MAKECMDGOALS);
	@exec echo -en "Press 'Enter' to configure Resident Sw (step 2)"
	@read dummy
	@$(MAKE) -s -C $(PRTOS_PATH)/user/bootloaders/rsw $(MAKECMDGOALS)
	@exec echo -en "Press 'Enter' to configure BAIL (step 3)"
	@read dummy
	@$(MAKE) -s -C $(PRTOS_PATH)/user/bail $(MAKECMDGOALS)
	@exec echo -e "\nNext, you may run 'make'"
else
	@$(MAKE) -s -C $(PRTOS_PATH)/core $(MAKECMDGOALS);
	@$(MAKE) -s -C $(PRTOS_PATH)/user/bootloaders/rsw $(MAKECMDGOALS)
	@$(MAKE) -s -C $(PRTOS_PATH)/user/bail $(MAKECMDGOALS)
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
	@find -type f -name "resident_sw_image" -exec rm '{}' \;
	@find -type f -name "obj_exc_elf" -exec rm '{}' \;
	@find -type f -name "*.pef.prtos_conf" -exec rm '{}' \;
	@find -type f -name "*.pef.prtos_conf" -exec rm '{}' \;
	@find -type f -name "*.pef" -exec rm '{}' \;
	@find -type f -name "prtos_cf" -exec rm '{}' \;

	@find -type f \( -name prtoseformat -o -name prtosbuildinfo -o -name prtospack -o -name prtoscparser -o -name prtoscpartcheck -o -name prtosccheck \) -exec rm '{}' \;

	@find -type l -exec rm '{}' \;
	@$(RM) -rf $(PRTOS_PATH)/core/include/config/*
	@$(RM) -rf $(PRTOS_PATH)/user/bail/include/config/*
	@$(RM) -rf $(PRTOS_PATH)/user/bootloaders/rsw/include/config/* $(PRTOS_PATH)/user/bootloaders/rsw/$(ARCH)/rsw.lds
	@$(RM) -f $(PRTOS_PATH)/prtos_config $(PRTOS_PATH)/core/include/autoconf.h $(PRTOS_PATH)/core/include/$(ARCH)/asm_offsets.h $(PRTOS_PATH)/core/include/$(ARCH)/brksize.h $(PRTOS_PATH)/core/include/$(ARCH)/ginfo.h $(PRTOS_PATH)/scripts/lxdialog/lxdialog $(PRTOS_PATH)/core/prtos_core $(PRTOS_PATH)/core/prtos_core.bin $(PRTOS_PATH)/core/prtos_core.pef $(PRTOS_PATH)/core/build.info $(PRTOS_PATH)/core/kernel/$(ARCH)/prtos.lds $(PRTOS_PATH)/scripts/extractinfo user/bin/rswbuild $(PRTOS_PATH)/core/module.lds $(PRTOS_PATH)/core/module.lds.in
	@$(RM) -f $(PRTOS_PATH)/core/Kconfig.ver $(PRTOS_PATH)/core/include/comp.h
	@$(RM) $(DISTRO_RUN) $(DISTRO_TAR)
	@exec echo -e "> Done";

clean:
	@exec echo -e "> Cleaning PRTOS";
	@exec echo -e "  - Removing *.o *.a *~ files";
	@find \( -name "*~" -o -name "*.o" -o -name "*.a" -o -name "*.xo" \) -exec rm '{}' \;
	@find -name "*.gcno" -exec rm '{}' \;
	@find . -name "*.bin" -not -path "*/user/bail/bin/u-boot.bin" -not -path "*/native_linux_run_on_qemu_*/*" -exec rm '{}' \;
	@$(RM) -f $(addprefix $(PRTOS_PATH)/,$(EXTRA_CLEAN_FILES))
	@$(RM) -f $(PRTOS_PATH)/core/build.info $(PRTOS_PATH)/user/tools/prtoscparser/prtos_conf.xsd $(PRTOS_PATH)/$(DISTRO).run
	@exec echo -e "> Done";

DISTRO	= prtos_$(ARCH)_$(PRTOSVERSION)
DISTRO_TMP=/tmp/$(DISTRO)-$$PPID
DISTRO_TAR = $(DISTRO).tar.bz2
$(DISTRO_TAR): prtos
	@$(RM) $(DISTRO_TAR)
	@make -s -C user/bail/examples -f Makefile clean
	@mkdir $(DISTRO_TMP) || exit 0
	@user/bin/prtosdistro $(DISTRO_TMP)/$(DISTRO) $(DISTRO_TAR)
	@$(RM) -r $(DISTRO_TMP)

DISTRO_RUN = $(DISTRO).run
DISTRO_LABEL= "prtos binary distribution $(PRTOSVERSION): "
$(DISTRO_RUN): $(DISTRO_TAR)
	@which makeself >/dev/null || (echo "Error: makeself program not found; install the makeself package" && exit -1)
	@mkdir $(DISTRO_TMP) || exit 0
	@tar xf $(DISTRO_TAR) -C $(DISTRO_TMP)
	@/bin/echo "> Generating self extracting binary distribution \"$(DISTRO_RUN)\""
	@makeself --bzip2 $(DISTRO_TMP)/$(DISTRO) $(DISTRO_RUN) $(DISTRO_LABEL) ./prtos-installer > /dev/null 2>&1
	@#| tr "\n" "#" | sed -u "s/[^#]*#/./g; s/..././g"
	@$(RM) $(DISTRO_TAR)
	@$(RM) -r $(DISTRO_TMP)
	@/bin/echo -e "> Done\n"

distro-tar: $(DISTRO_TAR)
distro-run: $(DISTRO_RUN)
