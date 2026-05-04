run.x86:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-i386  -m 512 -cdrom resident_sw.iso -serial stdio -boot d -smp 4  # Create qemu simulation platform with 4 CPU enabled,
	                                                                  # so that PRTOS up/smp are supported.

run.x86.nographic:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-i386 -nographic -m 512 -cdrom resident_sw.iso -serial mon:stdio -boot d -smp 4  # Create qemu simulation platform with 4 CPU enabled,
	                                                                  # so that PRTOS up/smp are supported.


run.aarch64:
	@make clean
	@make resident_sw
	@echo "=== Creating bootable image ==="
	@aarch64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id -R .comment -S resident_sw resident_sw.bin
	@mkimage -A arm64 -O linux -C none -a 0x40200000 -e 0x40200000 -d resident_sw.bin resident_sw_image
	@mkdir -p u-boot
	@cp $(BAIL_PATH)/bin/u-boot.bin ./u-boot/
	@echo "=== Starting QEMU ==="
	qemu-system-aarch64 \
		-machine virt,gic_version=3 \
		-machine virtualization=true \
		-cpu cortex-a72 \
		-machine type=virt \
		-m 4096 \
		-smp 4 \
		-bios ./u-boot/u-boot.bin \
		-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on \
		-nographic -no-reboot \
		-chardev socket,id=qemu-monitor,host=localhost,port=8889,server=on,wait=off,telnet=on \
		-mon qemu-monitor,mode=readline

run.aarch64.debug:
	@make clean
	@make resident_sw
	@echo "=== Creating bootable image ==="
	@aarch64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id -R .comment -S resident_sw resident_sw.bin
	@mkimage -A arm64 -O linux -C none -a 0x40200000 -e 0x40200000 -d resident_sw.bin resident_sw_image
	@mkdir -p u-boot
	@cp $(BAIL_PATH)/bin/u-boot.bin ./u-boot/
	@echo "=== Starting QEMU ==="
	qemu-system-aarch64 \
		-machine virt,gic_version=3 \
		-machine virtualization=true \
		-cpu cortex-a72 \
		-machine type=virt \
		-m 4096 \
		-smp 4 \
		-bios ./u-boot/u-boot.bin \
		-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on \
		-nographic -no-reboot \
		-chardev socket,id=qemu-monitor,host=localhost,port=8889,server=on,wait=off,telnet=on \
		-mon qemu-monitor,mode=readline -gdb tcp::1234 -S

run.riscv64:
	@$(MAKE) clean
	@$(MAKE) resident_sw
	@echo "=== Creating bootable image ==="
	@riscv64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id -R .comment -S resident_sw resident_sw.bin
	@echo "=== Starting QEMU (Use Ctrl+C to exit) ==="
	@qemu-system-riscv64 \
		-machine virt \
		-cpu rv64 \
		-smp 4 \
		-m 1G \
		-nographic -no-reboot \
		-bios default \
		-kernel resident_sw.bin \
		-monitor none \
		-serial stdio

run.amd64:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64  -m 1024 -cdrom resident_sw.iso -serial stdio -boot d -smp 4

run.amd64.nographic:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -nographic -m 1024 -cdrom resident_sw.iso -serial mon:stdio -boot d -smp 4

run.amd64.kvm:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg -m 1024 -cdrom resident_sw.iso -serial stdio -boot d -smp 4

run.amd64.kvm.nographic:
	@$(MAKE) clean
	@$(MAKE) resident_sw.iso
	@qemu-system-x86_64 -enable-kvm -cpu host,-waitpkg -nographic -m 1024 -cdrom resident_sw.iso -serial mon:stdio -boot d -smp 4

run: run.$(ARCH)
	
.PHONY: run run.aarch64 run.aarch64.debug run.riscv64 run.loongarch64 run.x86 run.x86.nographic run.amd64 run.amd64.nographic run.amd64.kvm run.amd64.kvm.nographic run.amd64.demo run.amd64.demo.nat run.amd64.demo.legacy

# QEMU_LOONGARCH64: path to the QEMU executable for LoongArch64 architecture
# Pls refer to https://github.com/prtos-project/prtos-hypervisor/wiki/Ubuntu-(24.04)-x86_64-host-to-install-loongarch64-cross-compielr to install qemu for LoongArch64 architecture.
QEMU_LOONGARCH64=/home/chenweis/loongarch64_workspace/qemu_install/bin/qemu-system-loongarch64

run.loongarch64:
	@$(MAKE) clean
	@$(MAKE) resident_sw
	@echo "=== Starting QEMU (Use Ctrl+C to exit) ==="
	@$(QEMU_LOONGARCH64) \
		-machine virt \
		-cpu max \
		-smp 4 \
		-m 2G \
		-nographic -no-reboot \
		-kernel resident_sw \
		-monitor none \
		-serial stdio
