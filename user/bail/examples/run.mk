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

run: run.$(ARCH)
	
.PHONY: run run.$(ARCH)
