/* Generated file, do not edit! */


long do_physdev_op(int cmd, XEN_GUEST_HANDLE_PARAM(void) arg);
long hvm_physdev_op(int cmd, XEN_GUEST_HANDLE_PARAM(void) arg);
int do_arm_physdev_op(int cmd, XEN_GUEST_HANDLE_PARAM(void) arg);
long do_grant_table_op(unsigned int cmd, XEN_GUEST_HANDLE_PARAM(void) uop, unsigned int count);
long hvm_grant_table_op(unsigned int cmd, XEN_GUEST_HANDLE_PARAM(void) uop, unsigned int count);
long do_memory_op(unsigned long cmd, XEN_GUEST_HANDLE_PARAM(void) arg);
long hvm_memory_op(unsigned long cmd, XEN_GUEST_HANDLE_PARAM(void) arg);
long do_xen_version(int cmd, XEN_GUEST_HANDLE_PARAM(void) arg);
long do_vcpu_op(int cmd, unsigned int vcpuid, XEN_GUEST_HANDLE_PARAM(void) arg);
long do_sched_op(int cmd, XEN_GUEST_HANDLE_PARAM(void) arg);
long do_xsm_op(XEN_GUEST_HANDLE_PARAM(void) op);
long do_callback_op(int cmd, XEN_GUEST_HANDLE_PARAM(const_void) arg);
long do_event_channel_op_compat(XEN_GUEST_HANDLE_PARAM(evtchn_op_t) uop);
long dep_event_channel_op_compat(XEN_GUEST_HANDLE_PARAM(evtchn_op_t) uop);
long do_physdev_op_compat(XEN_GUEST_HANDLE_PARAM(physdev_op_t) uop);
long dep_physdev_op_compat(XEN_GUEST_HANDLE_PARAM(physdev_op_t) uop);
long do_sched_op_compat(int cmd, unsigned long arg);
long dep_sched_op_compat(int cmd, unsigned long arg);
long do_set_timer_op(s_time_t timeout);
long do_console_io(unsigned int cmd, unsigned int count, XEN_GUEST_HANDLE_PARAM(char) buffer);
long do_vm_assist(unsigned int cmd, unsigned int type);
long do_event_channel_op(int cmd, XEN_GUEST_HANDLE_PARAM(void) arg);
long do_mmuext_op(XEN_GUEST_HANDLE_PARAM(mmuext_op_t) uops, unsigned int count, XEN_GUEST_HANDLE_PARAM(uint) pdone, unsigned int foreigndom);
long do_multicall(XEN_GUEST_HANDLE_PARAM(multicall_entry_t) call_list, unsigned int nr_calls);
long do_sysctl(XEN_GUEST_HANDLE_PARAM(xen_sysctl_t) u_sysctl);
long do_domctl(XEN_GUEST_HANDLE_PARAM(xen_domctl_t) u_domctl);
long do_paging_domctl_cont(XEN_GUEST_HANDLE_PARAM(xen_domctl_t) u_domctl);
long do_platform_op(XEN_GUEST_HANDLE_PARAM(xen_platform_op_t) u_xenpf_op);
long do_hvm_op(unsigned long op, XEN_GUEST_HANDLE_PARAM(void) arg);
long do_hypfs_op(unsigned int cmd, XEN_GUEST_HANDLE_PARAM(const_char) arg1, unsigned long arg2, XEN_GUEST_HANDLE_PARAM(void) arg3, unsigned long arg4);

#define call_handlers_arm(num, ret, a1, a2, a3, a4, a5) \
({ \
    if ( likely((num) == __HYPERVISOR_event_channel_op) ) \
            ret = do_event_channel_op((int)(a1), (XEN_GUEST_HANDLE_PARAM(void)){ _p(a2) }); \
    else \
        switch ( num ) \
        { \
        case __HYPERVISOR_sched_op_compat: \
            ret = dep_sched_op_compat((int)(a1), (unsigned long)(a2)); \
            break; \
        case __HYPERVISOR_platform_op: \
            ret = do_platform_op((XEN_GUEST_HANDLE_PARAM(xen_platform_op_t)){ _p(a1) }); \
            break; \
        case __HYPERVISOR_memory_op: \
            ret = do_memory_op((unsigned long)(a1), (XEN_GUEST_HANDLE_PARAM(void)){ _p(a2) }); \
            break; \
        case __HYPERVISOR_multicall: \
            ret = do_multicall((XEN_GUEST_HANDLE_PARAM(multicall_entry_t)){ _p(a1) }, (unsigned int)(a2)); \
            break; \
        case __HYPERVISOR_event_channel_op_compat: \
            ret = dep_event_channel_op_compat((XEN_GUEST_HANDLE_PARAM(evtchn_op_t)){ _p(a1) }); \
            break; \
        case __HYPERVISOR_xen_version: \
            ret = do_xen_version((int)(a1), (XEN_GUEST_HANDLE_PARAM(void)){ _p(a2) }); \
            break; \
        case __HYPERVISOR_console_io: \
            ret = do_console_io((unsigned int)(a1), (unsigned int)(a2), (XEN_GUEST_HANDLE_PARAM(char)){ _p(a3) }); \
            break; \
        case __HYPERVISOR_physdev_op_compat: \
            ret = dep_physdev_op_compat((XEN_GUEST_HANDLE_PARAM(physdev_op_t)){ _p(a1) }); \
            break; \
        case __HYPERVISOR_grant_table_op: \
            ret = do_grant_table_op((unsigned int)(a1), (XEN_GUEST_HANDLE_PARAM(void)){ _p(a2) }, (unsigned int)(a3)); \
            break; \
        case __HYPERVISOR_vm_assist: \
            ret = do_vm_assist((unsigned int)(a1), (unsigned int)(a2)); \
            break; \
        case __HYPERVISOR_vcpu_op: \
            ret = do_vcpu_op((int)(a1), (unsigned int)(a2), (XEN_GUEST_HANDLE_PARAM(void)){ _p(a3) }); \
            break; \
        case __HYPERVISOR_xsm_op: \
            ret = do_xsm_op((XEN_GUEST_HANDLE_PARAM(void)){ _p(a1) }); \
            break; \
        case __HYPERVISOR_sched_op: \
            ret = do_sched_op((int)(a1), (XEN_GUEST_HANDLE_PARAM(void)){ _p(a2) }); \
            break; \
        case __HYPERVISOR_physdev_op: \
            ret = do_arm_physdev_op((int)(a1), (XEN_GUEST_HANDLE_PARAM(void)){ _p(a2) }); \
            break; \
        case __HYPERVISOR_hvm_op: \
            ret = do_hvm_op((unsigned long)(a1), (XEN_GUEST_HANDLE_PARAM(void)){ _p(a2) }); \
            break; \
        case __HYPERVISOR_sysctl: \
            ret = do_sysctl((XEN_GUEST_HANDLE_PARAM(xen_sysctl_t)){ _p(a1) }); \
            break; \
        case __HYPERVISOR_domctl: \
            ret = do_domctl((XEN_GUEST_HANDLE_PARAM(xen_domctl_t)){ _p(a1) }); \
            break; \
        case __HYPERVISOR_hypfs_op: \
            ret = do_hypfs_op((unsigned int)(a1), (XEN_GUEST_HANDLE_PARAM(const_char)){ _p(a2) }, (unsigned long)(a3), (XEN_GUEST_HANDLE_PARAM(void)){ _p(a4) }, (unsigned long)(a5)); \
            break; \
        default: \
            ret = -ENOSYS; \
            break; \
        } \
})

#define hypercall_args_arm \
{ \
[__HYPERVISOR_sched_op_compat] = 2, \
[__HYPERVISOR_platform_op] = 1, \
[__HYPERVISOR_memory_op] = 2, \
[__HYPERVISOR_multicall] = 2, \
[__HYPERVISOR_event_channel_op_compat] = 1, \
[__HYPERVISOR_xen_version] = 2, \
[__HYPERVISOR_console_io] = 3, \
[__HYPERVISOR_physdev_op_compat] = 1, \
[__HYPERVISOR_grant_table_op] = 3, \
[__HYPERVISOR_vm_assist] = 2, \
[__HYPERVISOR_vcpu_op] = 3, \
[__HYPERVISOR_xsm_op] = 1, \
[__HYPERVISOR_sched_op] = 2, \
[__HYPERVISOR_event_channel_op] = 2, \
[__HYPERVISOR_physdev_op] = 2, \
[__HYPERVISOR_hvm_op] = 2, \
[__HYPERVISOR_sysctl] = 1, \
[__HYPERVISOR_domctl] = 1, \
[__HYPERVISOR_hypfs_op] = 5, \
}
