# LoongArch64 平台 — API 参考手册

## Hypercall API

所有 Hypercall 声明于 `user/libprtos/include/prtoshypercalls.h`，通过 BAIL 库或 `libprtos` 访问。各架构上的 API 签名一致，仅底层陷入机制不同（LoongArch64 通过 `syscall 0x11` 指令进入 Hypervisor）。

### 时间管理

| 函数 | 签名 | 描述 |
|---|---|---|
| `prtos_get_time` | `prtos_s32_t prtos_get_time(prtos_u32_t clock_id, prtos_time_t *time)` | 获取 HW_CLOCK 或 EXEC_CLOCK 时间 |
| `prtos_set_timer` | `prtos_s32_t prtos_set_timer(prtos_u32_t clock_id, prtos_time_t abstime, prtos_time_t interval)` | 设置绝对时间或间隔定时器 |

### 分区控制

| 函数 | 签名 | 描述 |
|---|---|---|
| `prtos_halt_partition` | `prtos_s32_t prtos_halt_partition(prtos_u32_t partition_id)` | 停止分区 |
| `prtos_suspend_partition` | `prtos_s32_t prtos_suspend_partition(prtos_u32_t partition_id)` | 挂起分区 |
| `prtos_resume_partition` | `prtos_s32_t prtos_resume_partition(prtos_u32_t partition_id)` | 恢复挂起的分区 |
| `prtos_reset_partition` | `prtos_s32_t prtos_reset_partition(prtos_u32_t partition_id, prtos_u32_t reset_mode)` | 重置分区 |
| `prtos_shutdown_partition` | `prtos_s32_t prtos_shutdown_partition(prtos_u32_t partition_id)` | 关闭分区 |
| `prtos_idle_self` | `void prtos_idle_self(void)` | 使当前分区进入空闲状态 |

### vCPU 管理

| 函数 | 签名 | 描述 |
|---|---|---|
| `prtos_halt_vcpu` | `prtos_s32_t prtos_halt_vcpu(prtos_u32_t vcpu_id)` | 停止 vCPU |
| `prtos_suspend_vcpu` | `prtos_s32_t prtos_suspend_vcpu(prtos_u32_t vcpu_id)` | 挂起 vCPU |
| `prtos_resume_vcpu` | `prtos_s32_t prtos_resume_vcpu(prtos_u32_t vcpu_id)` | 恢复 vCPU |
| `prtos_reset_vcpu` | `prtos_s32_t prtos_reset_vcpu(prtos_u32_t vcpu_id, ...)` | 使用页表和入口点重置 vCPU |
| `prtos_get_vcpuid` | `prtos_s32_t prtos_get_vcpuid(void)` | 获取当前 vCPU ID |

### 系统控制

| 函数 | 签名 | 描述 |
|---|---|---|
| `prtos_halt_system` | `void prtos_halt_system(void)` | 停止整个系统 |
| `prtos_reset_system` | `void prtos_reset_system(prtos_u32_t reset_mode)` | 重置系统 |

### 对象 I/O（通信端口）

| 函数 | 签名 | 描述 |
|---|---|---|
| `prtos_read_object` | `prtos_s32_t prtos_read_object(prtos_u32_t obj_id, void *buffer, prtos_u32_t size, prtos_u32_t *flags)` | 从采样/排队端口读取 |
| `prtos_write_object` | `prtos_s32_t prtos_write_object(prtos_u32_t obj_id, void *buffer, prtos_u32_t size, prtos_u32_t *flags)` | 向采样/排队端口写入 |
| `prtos_seek_object` | `prtos_s32_t prtos_seek_object(prtos_u32_t obj_id, prtos_u32_t offset, prtos_u32_t whence)` | 在对象中定位 |
| `prtos_ctrl_object` | `prtos_s32_t prtos_ctrl_object(prtos_u32_t obj_id, prtos_u32_t cmd, void *arg)` | 向对象发送控制命令 |

### 中断管理

| 函数 | 签名 | 描述 |
|---|---|---|
| `prtos_override_trap_hndl` | `prtos_s32_t prtos_override_trap_hndl(prtos_s32_t entry, ...)` | 覆盖陷阱处理程序 |
| `prtos_clear_irqmask` | `prtos_s32_t prtos_clear_irqmask(prtos_u32_t hw_irqs_to_unmask, prtos_u32_t ext_irqs_to_unmask)` | 清除 IRQ 掩码（使能中断） |
| `prtos_set_irqmask` | `prtos_s32_t prtos_set_irqmask(prtos_u32_t hw_irqs_to_mask, prtos_u32_t ext_irqs_to_mask)` | 设置 IRQ 掩码（禁用中断） |
| `prtos_set_irqpend` | `prtos_s32_t prtos_set_irqpend(prtos_u32_t hw_irqs, prtos_u32_t ext_irqs)` | 设置 IRQ 挂起标志 |
| `prtos_clear_irqpend` | `prtos_s32_t prtos_clear_irqpend(prtos_u32_t hw_irqs, prtos_u32_t ext_irqs)` | 清除 IRQ 挂起标志 |
| `prtos_route_irq` | `prtos_s32_t prtos_route_irq(prtos_u32_t type, prtos_u32_t irq, prtos_u16_t vector)` | 将硬件 IRQ 路由到中断向量 |

### 分区间虚拟中断 (IPVI)

| 函数 | 签名 | 描述 |
|---|---|---|
| `prtos_raise_ipvi` | `prtos_s32_t prtos_raise_ipvi(prtos_u32_t no_ipvi)` | 触发 IPVI |
| `prtos_raise_partition_ipvi` | `prtos_s32_t prtos_raise_partition_ipvi(prtos_u32_t partition_id, prtos_u32_t no_ipvi)` | 在指定分区中触发 IPVI |

### 内存管理

| 函数 | 签名 | 描述 |
|---|---|---|
| `prtos_set_page_type` | `prtos_s32_t prtos_set_page_type(prtos_address_t addr, prtos_u32_t type)` | 设置页表页面类型标志 |
| `prtos_invld_tlb` | `prtos_s32_t prtos_invld_tlb(prtos_address_t addr)` | 无效化 TLB 条目 |

### 调度管理

| 函数 | 签名 | 描述 |
|---|---|---|
| `prtos_switch_sched_plan` | `prtos_s32_t prtos_switch_sched_plan(prtos_u32_t new_plan_id, prtos_u32_t *current_plan_id)` | 切换调度计划 |

### 批量操作

| 函数 | 签名 | 描述 |
|---|---|---|
| `prtos_multicall` | `prtos_s32_t prtos_multicall(void *start_addr, void *end_addr)` | 执行批量 Hypercall |

### 工具函数

| 函数 | 签名 | 描述 |
|---|---|---|
| `prtos_get_gid_by_name` | `prtos_s32_t prtos_get_gid_by_name(prtos_u8_t *name, prtos_u32_t entity)` | 通过名称获取对象 ID |

---

## LoongArch64 平台特性

### 特权级模型

LoongArch 定义了 4 个特权级（PLV0..PLV3）。PRTOS 的使用方式如下：

| PLV | 在 PRTOS 中的角色 |
|---|---|
| PLV0 | PRTOS Hypervisor（持有 CSR、TLB-refill 处理程序、异常入口） |
| PLV3 | 客户分区（Linux、FreeRTOS、BAIL 程序） |

PLV1 与 PLV2 未使用。LoongArch 没有独立的 "VM" 模式 —— 虚拟化完全由 PLV3 → PLV0 的陷入并模拟实现，硬件支持时可借助 LVZ 扩展进行加速。

### Hypervisor 占用的 CSR

PRTOS 保留以下 Loongson `SAVE` CSR 作为内部用途，分区不应假设它们的值在 Hypercall 前后保持不变：

| CSR | 索引 | 用途 |
|---|---|---|
| `CSR.SAVE0` | `0x30` | 陷入入口主机栈指针暂存 |
| `CSR.SAVE1` | `0x31` | 每 CPU ID 暂存 |
| `CSR.SAVE2` | `0x32` | `t0` 寄存器暂存 |
| `CSR.SAVE5` | `0x35` | LVZ 可用性 / 模式标志 |

其他体系结构 CSR（`EENTRY`、`TLBRENTRY`、`STLBPS`、`PWCL`、`PWCH`、`ASID`、`PGDL`、`PGDH`、`EUEN`、`CRMD` 等）均做了虚拟化处理 —— 客户机读取得到的是每 vCPU 的影子值，写入由 `core/kernel/loongarch64/traps.c` 中的陷入处理程序模拟。

### 陷入入口分发

`core/kernel/loongarch64/entry.S` 中的统一陷入入口在保存寄存器之前先区分三种来源：

1. **LVZ Guest 退出**（`GSTAT.PVM == 1` 时）：跳转到 `_trap_from_lvz_guest`。
2. **PLV3 Guest 陷入**（`PRMD.PPLV != 0`）：跳转到 `_trap_from_guest`。
3. **Host 陷入**（`PRMD.PPLV == 0`）：跳转到 `_trap_save_regs`。

### Hypercall 调用方式

LoongArch64 分区通过下列指令发起 Hypercall：

```
syscall 0x11
```

参数与返回值遵循 LoongArch LP64 调用约定（`a0..a7` 传参，`v0` / `a0` 返回）。对应的胶水代码位于 `user/libprtos/loongarch64/`。

---

## BAIL 库 API

BAIL（裸机应用接口库）是一个最小化的分区开发环境。

### 核心函数

| 函数 | 头文件 | 描述 |
|---|---|---|
| `printf()` | `stdio.h` | 格式化输出到控制台 |
| `sprintf()` / `snprintf()` | `stdio.h` | 格式化输出到字符串 |
| `putchar()` | `stdio.h` | 写入单个字符 |
| `memset()` / `memcpy()` / `memcmp()` | `string.h` | 内存操作 |
| `strcpy()` / `strcmp()` / `strlen()` | `string.h` | 字符串操作 |
| `atoi()` / `strtoul()` / `strtol()` | `stdlib.h` | 字符串转数字 |
| `exit()` | `stdlib.h` | 退出分区 |
| `install_trap_handler()` | `irqs.h` | 安装陷阱/异常处理程序 |
| `assert()` | `assert.h` | 调试断言 |

### 中断处理程序安装

```c
#include <irqs.h>

void my_timer_handler(trap_ctxt_t *ctxt) {
    printf("Timer interrupt received\n");
}

void partition_main(void) {
    install_trap_handler(BAIL_PRTOSEXT_TRAP(0), my_timer_handler);
    prtos_clear_irqmask(0, (1 << 0));  // 取消屏蔽扩展 IRQ 0
}
```

---

## XML 配置参考

### 内存区域标志

| 标志 | 描述 |
|---|---|
| `none` | 默认（无特殊标志） |
| `shared` | 多分区可访问 |
| `read-only` | 只读访问 |
| `unmapped` | 不映射 |
| `uncacheable` | 禁用缓存 |
| `rom` | 只读存储器 |
| `flag0`–`flag3` | 用户自定义标志 |

### 分区标志

| 标志 | 描述 |
|---|---|
| `system` | 系统分区，具有管理权限 |
| `fp` | 启用浮点运算（LoongArch FPU / LSX 上下文在切换时保留） |
| `none` | 标准分区（默认） |

> **说明**：在 LoongArch64 上，`hw_virt` 分区属性当前被忽略 —— 所有分区均以半虚拟化方式运行在 PLV3。

### 健康监控事件

| 事件 | 描述 |
|---|---|
| `PRTOS_HM_EV_FATAL_ERROR` | 致命系统错误 |
| `PRTOS_HM_EV_SYSTEM_ERROR` | 系统级错误 |
| `PRTOS_HM_EV_PARTITION_ERROR` | 分区级错误 |
| `PRTOS_HM_EV_MEM_PROTECTION` | 内存保护违规 |
| `PRTOS_HM_EV_UNEXPECTED_TRAP` | 意外陷阱/异常 |

### 健康监控动作

| 动作 | 描述 |
|---|---|
| `PRTOS_HM_AC_IGNORE` | 忽略事件 |
| `PRTOS_HM_AC_PARTITION_COLD_RESET` | 冷重置分区 |
| `PRTOS_HM_AC_PARTITION_WARM_RESET` | 热重置分区 |
| `PRTOS_HM_AC_PARTITION_HALT` | 停止分区 |
| `PRTOS_HM_AC_PROPAGATE` | 传播到系统分区 |

## 延伸阅读

- 《[嵌入式 Hypervisor：架构、原理与应用](http://www.prtos.org/embedded_hypervisor_book/)》 —— 深入了解 LoongArch 上陷入并模拟的虚拟化理论基础
