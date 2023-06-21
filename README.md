# PRTOS - a lightweight static partitioning hypervisor


Introduction
------------

**PRTOS Hypervisor** is a lightweight, open-source embedded hypervisor which aims at providing strong isolation and real-time guarantees. PRTOS provides a minimal implementation of separation kernel hypervisor architecture. 

Designed mainly for targeting mixed-criticality systems, prtos strongly focuses on isolation for fault-containment and real-time behavior. Its implementation comprises only a minimal, thin-layer of privileged software leveraging ISA para-virtualization. The main goal of PRTOS Hypervisor is to provide a virtualization platform that ensures isolation and predictability for critical applications running on embedded systems. It achieves this by using a type-1 hypervisor architecture, where the hypervisor runs directly on the hardware without the need for an underlying operating system.

Key features of PRTOS Hypervisor include:

 - Real-time capabilities: PRTOS Hypervisor is specifically designed for real-time and safety-critical applications, providing deterministic and predictable execution of tasks.

 - Partitioning and isolation: The hypervisor allows for the partitioning of resources, such as CPU, memory, and devices, into separate domains or partitions. Each partition can run its own real-time operating system and applications, ensuring isolation and fault containment.

 - Minimal footprint: PRTOS Hypervisor has a small memory footprint, making it suitable for resource-constrained embedded systems.

 - static resources configuration: PRTOS Hypervisor supports static configuration of partitions, resources are statically partitioned and assigned at VM instantiation time.

 - Inter-partition communication: PRTOS Hypervisor provides mechanisms for inter-partition communication, allowing partitions to exchange data and synchronize their activities.

PRTOS Hypervisor  target for various domains, including aerospace, automotive, and telecommunications, where real-time and safety-critical requirements are essential. It provides a reliable and secure virtualization platform for running critical applications while ensuring isolation and predictable behavior.


**NOTE**: PRTOS Hypervisor is not a hypervisor developed from scratch but rather stands on the shoulders of giants, drawing inspiration from some classic open-source software projects such as Xen Hypervisor (https://xenproject.org/), Lguest Hypervisor (http://lguest.ozlabs.org), XtratuM (https://en.wikipedia.org/wiki/XtratuM), and Linux Kernel (https://www.linux.org/). Because of this, PRTOS Hypervisor is also released under the GPL license. Additionally, a book titled 'Embedded Hypervisor: Architecture, Principles, and Applications' will be published(WIP), offering a detailed introduction to the design and implementation techniques of PRTOS Hypervisor. This aims to facilitate a better understanding of PRTOS Hypervisor and foster an open community where students and enthusiasts interested in hypervisors can participate, thereby promoting the healthy evolution of PRTOS Hypervisor.


**Currently supported platforms**
- [x] QEMU 32bit X86 platform


**PRTOS Hypervisor directory structure**

- `core`:     The source code of the PRTOS Hypervisor
- `scripts`:  The assist tools to configure PRTOS source code
- `user`:     User space utilities (libprtos, tools, examples, etc)


Community Resources
-------------------

Project website:

 - http://www.prtos.org/ 

Source code:

 - https://github.com/prtos-project/prtos-hypervisor
 - git@github.com:prtos-project/prtos-hypervisor.git

 Contributing:
 
 - Please get in touch (cwsun@prtos.org)

License
-------

See the file [LICENSE.md](./LICENSE.md).

