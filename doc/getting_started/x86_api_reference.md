# x86 (32-bit) Platform — API Reference

## Hypercall API

All hypercalls are declared in `user/libprtos/include/prtoshypercalls.h` and accessed via the BAIL library or `libprtos`.

### Time Management

| Function | Signature | Description |
|---|---|---|
| `prtos_get_time` | `prtos_s32_t prtos_get_time(prtos_u32_t clock_id, prtos_time_t *time)` | Get time from HW_CLOCK or EXEC_CLOCK |
| `prtos_set_timer` | `prtos_s32_t prtos_set_timer(prtos_u32_t clock_id, prtos_time_t abstime, prtos_time_t interval)` | Set timer with absolute time or interval |

### Partition Control

| Function | Signature | Description |
|---|---|---|
| `prtos_halt_partition` | `prtos_s32_t prtos_halt_partition(prtos_u32_t partition_id)` | Halt a partition |
| `prtos_suspend_partition` | `prtos_s32_t prtos_suspend_partition(prtos_u32_t partition_id)` | Suspend a partition |
| `prtos_resume_partition` | `prtos_s32_t prtos_resume_partition(prtos_u32_t partition_id)` | Resume a suspended partition |
| `prtos_reset_partition` | `prtos_s32_t prtos_reset_partition(prtos_u32_t partition_id, prtos_u32_t reset_mode)` | Reset a partition |
| `prtos_shutdown_partition` | `prtos_s32_t prtos_shutdown_partition(prtos_u32_t partition_id)` | Shutdown a partition |
| `prtos_idle_self` | `void prtos_idle_self(void)` | Idle the current partition |

### vCPU Management

| Function | Signature | Description |
|---|---|---|
| `prtos_halt_vcpu` | `prtos_s32_t prtos_halt_vcpu(prtos_u32_t vcpu_id)` | Halt a vCPU |
| `prtos_suspend_vcpu` | `prtos_s32_t prtos_suspend_vcpu(prtos_u32_t vcpu_id)` | Suspend a vCPU |
| `prtos_resume_vcpu` | `prtos_s32_t prtos_resume_vcpu(prtos_u32_t vcpu_id)` | Resume a vCPU |
| `prtos_reset_vcpu` | `prtos_s32_t prtos_reset_vcpu(prtos_u32_t vcpu_id, ...)` | Reset a vCPU with page table and entry point |
| `prtos_get_vcpuid` | `prtos_s32_t prtos_get_vcpuid(void)` | Get current vCPU ID |

### System Control

| Function | Signature | Description |
|---|---|---|
| `prtos_halt_system` | `void prtos_halt_system(void)` | Halt the entire system |
| `prtos_reset_system` | `void prtos_reset_system(prtos_u32_t reset_mode)` | Reset the system |

### Object I/O (Communication Ports)

| Function | Signature | Description |
|---|---|---|
| `prtos_read_object` | `prtos_s32_t prtos_read_object(prtos_u32_t obj_id, void *buffer, prtos_u32_t size, prtos_u32_t *flags)` | Read from sampling/queuing port |
| `prtos_write_object` | `prtos_s32_t prtos_write_object(prtos_u32_t obj_id, void *buffer, prtos_u32_t size, prtos_u32_t *flags)` | Write to sampling/queuing port |
| `prtos_seek_object` | `prtos_s32_t prtos_seek_object(prtos_u32_t obj_id, prtos_u32_t offset, prtos_u32_t whence)` | Seek in object |
| `prtos_ctrl_object` | `prtos_s32_t prtos_ctrl_object(prtos_u32_t obj_id, prtos_u32_t cmd, void *arg)` | Send control commands to object |

### Interrupt Management

| Function | Signature | Description |
|---|---|---|
| `prtos_override_trap_hndl` | `prtos_s32_t prtos_override_trap_hndl(prtos_s32_t entry, ...)` | Override trap handler |
| `prtos_clear_irqmask` | `prtos_s32_t prtos_clear_irqmask(prtos_u32_t hw_irqs_to_unmask, prtos_u32_t ext_irqs_to_unmask)` | Clear IRQ mask (enable) |
| `prtos_set_irqmask` | `prtos_s32_t prtos_set_irqmask(prtos_u32_t hw_irqs_to_mask, prtos_u32_t ext_irqs_to_mask)` | Set IRQ mask (disable) |
| `prtos_set_irqpend` | `prtos_s32_t prtos_set_irqpend(prtos_u32_t hw_irqs, prtos_u32_t ext_irqs)` | Set IRQ as pending |
| `prtos_clear_irqpend` | `prtos_s32_t prtos_clear_irqpend(prtos_u32_t hw_irqs, prtos_u32_t ext_irqs)` | Clear IRQ pending flag |
| `prtos_route_irq` | `prtos_s32_t prtos_route_irq(prtos_u32_t type, prtos_u32_t irq, prtos_u16_t vector)` | Route HW IRQ to vector |

### Inter-Partition Virtual Interrupts (IPVI)

| Function | Signature | Description |
|---|---|---|
| `prtos_raise_ipvi` | `prtos_s32_t prtos_raise_ipvi(prtos_u32_t no_ipvi)` | Raise an IPVI |
| `prtos_raise_partition_ipvi` | `prtos_s32_t prtos_raise_partition_ipvi(prtos_u32_t partition_id, prtos_u32_t no_ipvi)` | Raise IPVI in specific partition |

### Memory Management (Para-virtualization)

| Function | Signature | Description |
|---|---|---|
| `prtos_set_page_type` | `prtos_s32_t prtos_set_page_type(prtos_address_t addr, prtos_u32_t type)` | Set page type flags |
| `prtos_update_page32` | `prtos_s32_t prtos_update_page32(prtos_address_t addr, prtos_u32_t val)` | Update 32-bit page entry |
| `prtos_invld_tlb` | `prtos_s32_t prtos_invld_tlb(prtos_address_t addr)` | Invalidate TLB entry |

### Scheduling

| Function | Signature | Description |
|---|---|---|
| `prtos_switch_sched_plan` | `prtos_s32_t prtos_switch_sched_plan(prtos_u32_t new_plan_id, prtos_u32_t *current_plan_id)` | Switch scheduling plan |

### Batch Operations

| Function | Signature | Description |
|---|---|---|
| `prtos_multicall` | `prtos_s32_t prtos_multicall(void *start_addr, void *end_addr)` | Execute batched hypercalls |

### Utility

| Function | Signature | Description |
|---|---|---|
| `prtos_get_gid_by_name` | `prtos_s32_t prtos_get_gid_by_name(prtos_u8_t *name, prtos_u32_t entity)` | Get object ID by name |

---

## BAIL Library API

BAIL (Bare-metal Application Interface Library) is a minimal partition development environment.

### Core Functions

| Function | Header | Description |
|---|---|---|
| `printf()` | `stdio.h` | Formatted output to console |
| `sprintf()` / `snprintf()` | `stdio.h` | Formatted output to string |
| `putchar()` | `stdio.h` | Write single character |
| `memset()` / `memcpy()` / `memcmp()` | `string.h` | Memory operations |
| `strcpy()` / `strcmp()` / `strlen()` | `string.h` | String operations |
| `atoi()` / `strtoul()` / `strtol()` | `stdlib.h` | String to number conversion |
| `exit()` | `stdlib.h` | Exit partition |
| `install_trap_handler()` | `irqs.h` | Install trap/exception handler |
| `assert()` | `assert.h` | Debug assertion |

### Interrupt Handler Installation

```c
#include <irqs.h>

void my_timer_handler(trap_ctxt_t *ctxt) {
    printf("Timer interrupt received\n");
}

void partition_main(void) {
    install_trap_handler(BAIL_PRTOSEXT_TRAP(0), my_timer_handler);
    prtos_clear_irqmask(0, (1 << 0));  // Unmask extended IRQ 0
}
```

---

## XML Configuration Reference

### Memory Area Flags

| Flag | Description |
|---|---|
| `none` | Default (no special flags) |
| `shared` | Accessible by multiple partitions |
| `read-only` | Read-only access |
| `unmapped` | Not mapped |
| `uncacheable` | Cache disabled |
| `rom` | Read-only memory |
| `flag0`–`flag3` | User-defined flags |

### Partition Flags

| Flag | Description |
|---|---|
| `system` | System partition with management privileges |
| `fp` | Floating-point enabled |
| `none` | Standard partition (default) |

### Health Monitoring Events

| Event | Description |
|---|---|
| `PRTOS_HM_EV_FATAL_ERROR` | Fatal system error |
| `PRTOS_HM_EV_SYSTEM_ERROR` | System-level error |
| `PRTOS_HM_EV_PARTITION_ERROR` | Partition-level error |
| `PRTOS_HM_EV_MEM_PROTECTION` | Memory protection violation |
| `PRTOS_HM_EV_UNEXPECTED_TRAP` | Unexpected trap/exception |

### Health Monitoring Actions

| Action | Description |
|---|---|
| `PRTOS_HM_AC_IGNORE` | Ignore the event |
| `PRTOS_HM_AC_PARTITION_COLD_RESET` | Cold reset the partition |
| `PRTOS_HM_AC_PARTITION_WARM_RESET` | Warm reset the partition |
| `PRTOS_HM_AC_PARTITION_HALT` | Halt the partition |
| `PRTOS_HM_AC_PROPAGATE` | Propagate to system partition |

## Further Reading

- [*Embedded Hypervisor: Architecture, Principles, and Implementation*](http://www.prtos.org/embedded_hypervisor_book/) — for the theoretical foundations of Hypercall design and partition management
