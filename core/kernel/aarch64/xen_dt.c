/* Xen device tree - consolidated */
/* === BEGIN INLINED: common_device_tree.c === */
#include <xen_xen_config.h>
/*
 * Device Tree
 *
 * Copyright (C) 2012 Citrix Systems, Inc.
 * Copyright 2009 Benjamin Herrenschmidt, IBM Corp
 * benh@kernel.crashing.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <xen_types.h>
#include <xen_init.h>
#include <xen_guest_access.h>
#include <xen_device_tree.h>
#include <xen_kernel.h>
#include <xen_lib.h>
#include <xen_libfdt_libfdt.h>
#include <xen_mm.h>
#include <xen_stdarg.h>
#include <xen_string.h>
#include <xen_cpumask.h>
#include <xen_ctype.h>
#include <asm_setup.h>
#include <xen_err.h>

const void *device_tree_flattened;
dt_irq_xlate_func dt_irq_xlate;
/* Host device tree */
struct dt_device_node *dt_host;
/* Interrupt controller node*/
const struct dt_device_node *dt_interrupt_controller;
DEFINE_RWLOCK(dt_host_lock);

/**
 * struct dt_alias_prop - Alias property in 'aliases' node
 * @link: List node to link the structure in aliases_lookup list
 * @alias: Alias property name
 * @np: Pointer to device_node that the alias stands for
 * @id: Index value from end of alias name
 * @stem: Alias string without the index
 *
 * The structure represents one alias property of 'aliases' node as
 * an entry in aliases_lookup list.
 */
struct dt_alias_prop {
    struct list_head link;
    const char *alias;
    struct dt_device_node *np;
    int id;
    char stem[0];
};

static LIST_HEAD(aliases_lookup);

#ifdef CONFIG_DEVICE_TREE_DEBUG
static void dt_dump_addr(const char *s, const __be32 *addr, int na)
{
    dt_dprintk("%s", s);
    while ( na-- )
        dt_dprintk(" %08x", be32_to_cpu(*(addr++)));
    dt_dprintk("\n");
}
#else
static void dt_dump_addr(const char *s, const __be32 *addr, int na) { }
#endif

#define DT_BAD_ADDR ((u64)-1)

/* Max address size we deal with */
#define DT_MAX_ADDR_CELLS 4
#define DT_CHECK_ADDR_COUNT(na) ((na) > 0 && (na) <= DT_MAX_ADDR_CELLS)
#define DT_CHECK_COUNTS(na, ns) (DT_CHECK_ADDR_COUNT(na) && (ns) > 0)

/* Callbacks for bus specific translators */
struct dt_bus
{
    const char *name;
    const char *addresses;
    bool (*match)(const struct dt_device_node *node);
    void (*count_cells)(const struct dt_device_node *child,
                        int *addrc, int *sizec);
    u64 (*map)(__be32 *addr, const __be32 *range, int na, int ns, int pna);
    int (*translate)(__be32 *addr, u64 offset, int na);
    unsigned int (*get_flags)(const __be32 *addr);
};

void dt_get_range(const __be32 **cellp, const struct dt_device_node *np,
                  u64 *address, u64 *size)
{
    *address = dt_next_cell(dt_n_addr_cells(np), cellp);
    *size = dt_next_cell(dt_n_size_cells(np), cellp);
}

void dt_set_cell(__be32 **cellp, int size, u64 val)
{
    int cells = size;

    while ( size-- )
    {
        (*cellp)[size] = cpu_to_fdt32(val);
        val >>= 32;
    }

    (*cellp) += cells;
}


void dt_child_set_range(__be32 **cellp, int addrcells, int sizecells,
                        u64 address, u64 size)
{
    dt_set_cell(cellp, addrcells, address);
    dt_set_cell(cellp, sizecells, size);
}

static void __init *unflatten_dt_alloc(unsigned long *mem, unsigned long size,
                                       unsigned long align)
{
    void *res;

    *mem = ROUNDUP(*mem, align);
    res = (void *)*mem;
    *mem += size;

    return res;
}

/* Find a property with a given name for a given node and return it. */
const struct dt_property *dt_find_property(const struct dt_device_node *np,
                                           const char *name, u32 *lenp)
{
    const struct dt_property *pp;

    if ( !np )
        return NULL;

    for ( pp = np->properties; pp; pp = pp->next )
    {
        if ( dt_prop_cmp(pp->name, name) == 0 )
        {
            if ( lenp )
                *lenp = pp->length;
            break;
        }
    }

    return pp;
}

const void *dt_get_property(const struct dt_device_node *np,
                            const char *name, u32 *lenp)
{
    const struct dt_property *pp = dt_find_property(np, name, lenp);

    return pp ? pp->value : NULL;
}

bool dt_property_read_u32(const struct dt_device_node *np,
                          const char *name, u32 *out_value)
{
    u32 len;
    const __be32 *val;

    val = dt_get_property(np, name, &len);
    if ( !val || len < sizeof(*out_value) )
        return 0;

    *out_value = be32_to_cpup(val);

    return 1;
}


bool dt_property_read_u64(const struct dt_device_node *np,
                          const char *name, u64 *out_value)
{
    u32 len;
    const __be32 *val;

    val = dt_get_property(np, name, &len);
    if ( !val || len < sizeof(*out_value) )
        return 0;

    *out_value = dt_read_number(val, 2);

    return 1;
}
int dt_property_read_string(const struct dt_device_node *np,
                            const char *propname, const char **out_string)
{
    const struct dt_property *pp = dt_find_property(np, propname, NULL);

    if ( !pp )
        return -EINVAL;
    if ( !pp->length )
        return -ENODATA;
    if ( strnlen(pp->value, pp->length) >= pp->length )
        return -EILSEQ;

    *out_string = pp->value;

    return 0;
}

/**
 * dt_find_property_value_of_size
 *
 * @np:     device node from which the property value is to be read.
 * @propname:   name of the property to be searched.
 * @min:    minimum allowed length of property value
 * @max:    maximum allowed length of property value (0 means unlimited)
 * @len:    if !=NULL, actual length is written to here
 *
 * Search for a property in a device node and valid the requested size.
 *
 * Return: The property value on success, -EINVAL if the property does not
 * exist, -ENODATA if property does not have a value, and -EOVERFLOW if the
 * property data is too small or too large.
 */
static void *dt_find_property_value_of_size(const struct dt_device_node *np,
                                            const char *propname, u32 min,
                                            u32 max, size_t *len)
{
    const struct dt_property *prop = dt_find_property(np, propname, NULL);

    if ( !prop )
        return ERR_PTR(-EINVAL);
    if ( !prop->value )
        return ERR_PTR(-ENODATA);
    if ( prop->length < min )
        return ERR_PTR(-EOVERFLOW);
    if ( max && prop->length > max )
        return ERR_PTR(-EOVERFLOW);

    if ( len )
        *len = prop->length;

    return prop->value;
}


int dt_property_match_string(const struct dt_device_node *np,
                             const char *propname, const char *string)
{
    const struct dt_property *dtprop = dt_find_property(np, propname, NULL);
    size_t l;
    int i;
    const char *p, *end;

    if ( !dtprop )
        return -EINVAL;
    if ( !dtprop->value )
        return -ENODATA;

    p = dtprop->value;
    end = p + dtprop->length;

    for ( i = 0; p < end; i++, p += l )
    {
        l = strnlen(p, end - p) + 1;
        if ( p + l > end )
            return -EILSEQ;
        if ( strcmp(string, p) == 0 )
            return i; /* Found it; return index */
    }
    return -ENODATA;
}

bool dt_device_is_compatible(const struct dt_device_node *device,
                             const char *compat)
{
    const char* cp;
    u32 cplen, l;

    cp = dt_get_property(device, "compatible", &cplen);
    if ( cp == NULL )
        return 0;
    while ( cplen > 0 )
    {
        if ( dt_compat_cmp(cp, compat) == 0 )
            return 1;
        l = strlen(cp) + 1;
        cp += l;
        cplen -= l;
    }

    return 0;
}

bool dt_machine_is_compatible(const char *compat)
{
    const struct dt_device_node *root;
    bool rc = false;

    root = dt_find_node_by_path("/");
    if ( root )
    {
        rc = dt_device_is_compatible(root, compat);
    }
    return rc;
}



struct dt_device_node *dt_find_node_by_path_from(struct dt_device_node *from,
                                                 const char *path)
{
    struct dt_device_node *np;

    dt_for_each_device_node(from, np)
        if ( np->full_name && (dt_node_cmp(np->full_name, path) == 0) )
            break;

    return np;
}

int dt_find_node_by_gpath(XEN_GUEST_HANDLE(char) u_path, uint32_t u_plen,
                          struct dt_device_node **node)
{
    char *path;

    path = safe_copy_string_from_guest(u_path, u_plen, PAGE_SIZE);
    if ( IS_ERR(path) )
        return PTR_ERR(path);

    *node = dt_find_node_by_path(path);

    xfree(path);

    return (*node == NULL) ? -ESRCH : 0;
}

struct dt_device_node *dt_find_node_by_alias(const char *alias)
{
    const struct dt_alias_prop *app;

    list_for_each_entry( app, &aliases_lookup, link )
    {
        if ( !strcmp(app->alias, alias) )
            return app->np;
    }

    return NULL;
}

const struct dt_device_match *
dt_match_node(const struct dt_device_match *matches,
              const struct dt_device_node *node)
{
    if ( !matches )
        return NULL;

    while ( matches->path || matches->type ||
            matches->compatible || matches->not_available || matches->prop )
    {
        bool match = true;

        if ( matches->path )
            match &= dt_node_path_is_equal(node, matches->path);

        if ( matches->type )
            match &= dt_device_type_is_equal(node, matches->type);

        if ( matches->compatible )
            match &= dt_device_is_compatible(node, matches->compatible);

        if ( matches->not_available )
            match &= !dt_device_is_available(node);

        if ( matches->prop )
            match &= dt_find_property(node, matches->prop, NULL) != NULL;

        if ( match )
            return matches;
        matches++;
    }

    return NULL;
}

const struct dt_device_node *dt_get_parent(const struct dt_device_node *node)
{
    if ( !node )
        return NULL;

    return node->parent;
}

struct dt_device_node *
dt_find_compatible_node(struct dt_device_node *from,
                        const char *type,
                        const char *compatible)
{
    struct dt_device_node *np;
    struct dt_device_node *dt;

    dt = from ? from->allnext : dt_host;
    dt_for_each_device_node(dt, np)
    {
        if ( type
             && !(np->type && (dt_node_cmp(np->type, type) == 0)) )
            continue;
        if ( dt_device_is_compatible(np, compatible) )
            break;
    }

    return np;
}

struct dt_device_node *
dt_find_matching_node(struct dt_device_node *from,
                      const struct dt_device_match *matches)
{
    struct dt_device_node *np;
    struct dt_device_node *dt;

    dt = from ? from->allnext : dt_host;
    dt_for_each_device_node(dt, np)
    {
        if ( dt_match_node(matches, np) )
            return np;
    }

    return NULL;
}

static int __dt_n_addr_cells(const struct dt_device_node *np, bool parent)
{
    const __be32 *ip;

    do {
        if ( np->parent && !parent )
            np = np->parent;
        parent = false;

        ip = dt_get_property(np, "#address-cells", NULL);
        if ( ip )
            return be32_to_cpup(ip);
    } while ( np->parent );
    /* No #address-cells property for the root node */
    return DT_ROOT_NODE_ADDR_CELLS_DEFAULT;
}

static int __dt_n_size_cells(const struct dt_device_node *np, bool parent)
{
    const __be32 *ip;

    do {
        if ( np->parent && !parent )
            np = np->parent;
        parent = false;

        ip = dt_get_property(np, "#size-cells", NULL);
        if ( ip )
            return be32_to_cpup(ip);
    } while ( np->parent );
    /* No #address-cells property for the root node */
    return DT_ROOT_NODE_SIZE_CELLS_DEFAULT;
}

int dt_n_addr_cells(const struct dt_device_node *np)
{
    return __dt_n_addr_cells(np, false);
}

int dt_n_size_cells(const struct dt_device_node *np)
{
    return __dt_n_size_cells(np, false);
}

int dt_child_n_addr_cells(const struct dt_device_node *parent)
{
    return __dt_n_addr_cells(parent, true);
}

int dt_child_n_size_cells(const struct dt_device_node *parent)
{
    return __dt_n_size_cells(parent, true);
}

/*
 * These are defined in Linux where much of this code comes from, but
 * are currently unused outside this file in the context of Xen.
 */
#define IORESOURCE_BITS         0x000000ff      /* Bus-specific bits */

#define IORESOURCE_TYPE_BITS    0x00001f00      /* Resource type */
#define IORESOURCE_IO           0x00000100      /* PCI/ISA I/O ports */
#define IORESOURCE_MEM          0x00000200
#define IORESOURCE_REG          0x00000300      /* Register offsets */
#define IORESOURCE_IRQ          0x00000400
#define IORESOURCE_DMA          0x00000800
#define IORESOURCE_BUS          0x00001000

#define IORESOURCE_PREFETCH     0x00002000      /* No side effects */
#define IORESOURCE_READONLY     0x00004000
#define IORESOURCE_CACHEABLE    0x00008000
#define IORESOURCE_RANGELENGTH  0x00010000
#define IORESOURCE_SHADOWABLE   0x00020000

/*
 * Default translator (generic bus)
 */
static bool dt_bus_default_match(const struct dt_device_node *node)
{
    /* Root node doesn't have "ranges" property */
    if ( node->parent == NULL )
        return 1;

    /* The default bus is only used when the "ranges" property exists.
     * Otherwise we can't translate the address
     */
    return (dt_get_property(node, "ranges", NULL) != NULL);
}

static void dt_bus_default_count_cells(const struct dt_device_node *dev,
                                int *addrc, int *sizec)
{
    if ( addrc )
        *addrc = dt_n_addr_cells(dev);
    if ( sizec )
        *sizec = dt_n_size_cells(dev);
}

static u64 dt_bus_default_map(__be32 *addr, const __be32 *range,
                              int na, int ns, int pna)
{
    u64 cp, s, da;

    cp = dt_read_number(range, na);
    s = dt_read_number(range + na + pna, ns);
    da = dt_read_number(addr, na);

    dt_dprintk("DT: default map, cp=%llx, s=%llx, da=%llx\n",
               (unsigned long long)cp, (unsigned long long)s,
               (unsigned long long)da);

    /*
     * If the number of address cells is larger than 2 we assume the
     * mapping doesn't specify a physical address. Rather, the address
     * specifies an identifier that must match exactly.
     */
    if ( na > 2 && memcmp(range, addr, na * 4) != 0 )
        return DT_BAD_ADDR;

    if ( da < cp || da >= (cp + s) )
        return DT_BAD_ADDR;
    return da - cp;
}

static int dt_bus_default_translate(__be32 *addr, u64 offset, int na)
{
    u64 a = dt_read_number(addr, na);

    memset(addr, 0, na * 4);
    a += offset;
    if ( na > 1 )
        addr[na - 2] = cpu_to_be32(a >> 32);
    addr[na - 1] = cpu_to_be32(a & 0xffffffffu);

    return 0;
}
static unsigned int dt_bus_default_get_flags(const __be32 *addr)
{
    return IORESOURCE_MEM;
}

/*
 * PCI bus specific translator
 */

static bool dt_node_is_pci(const struct dt_device_node *np)
{
    bool is_pci = !strcmp(np->name, "pcie") || !strcmp(np->name, "pci");

    if ( is_pci )
        printk(XENLOG_WARNING "%s: Missing device_type\n", np->full_name);

    return is_pci;
}

static bool dt_bus_pci_match(const struct dt_device_node *np)
{
    /*
     * "pciex" is PCI Express "vci" is for the /chaos bridge on 1st-gen PCI
     * powermacs "ht" is hypertransport
     *
     * If none of the device_type match, and that the node name is
     * "pcie" or "pci", accept the device as PCI (with a warning).
     */
    return !strcmp(np->type, "pci") || !strcmp(np->type, "pciex") ||
        !strcmp(np->type, "vci") || !strcmp(np->type, "ht") ||
        dt_node_is_pci(np);
}

static void dt_bus_pci_count_cells(const struct dt_device_node *np,
				   int *addrc, int *sizec)
{
    if (addrc)
        *addrc = 3;
    if (sizec)
        *sizec = 2;
}

static unsigned int dt_bus_pci_get_flags(const __be32 *addr)
{
    unsigned int flags = 0;
    u32 w = be32_to_cpup(addr);

    switch((w >> 24) & 0x03) {
    case 0x01:
        flags |= IORESOURCE_IO;
        break;
    case 0x02: /* 32 bits */
    case 0x03: /* 64 bits */
        flags |= IORESOURCE_MEM;
        break;
    }
    if (w & 0x40000000)
        flags |= IORESOURCE_PREFETCH;
    return flags;
}

static u64 dt_bus_pci_map(__be32 *addr, const __be32 *range, int na, int ns,
		int pna)
{
    u64 cp, s, da;
    unsigned int af, rf;

    af = dt_bus_pci_get_flags(addr);
    rf = dt_bus_pci_get_flags(range);

    /* Check address type match */
    if ((af ^ rf) & (IORESOURCE_MEM | IORESOURCE_IO))
        return DT_BAD_ADDR;

    /* Read address values, skipping high cell */
    cp = dt_read_number(range + 1, na - 1);
    s  = dt_read_number(range + na + pna, ns);
    da = dt_read_number(addr + 1, na - 1);

    dt_dprintk("DT: PCI map, cp=%llx, s=%llx, da=%llx\n",
               (unsigned long long)cp, (unsigned long long)s,
               (unsigned long long)da);

    if (da < cp || da >= (cp + s))
        return DT_BAD_ADDR;
    return da - cp;
}

static int dt_bus_pci_translate(__be32 *addr, u64 offset, int na)
{
    return dt_bus_default_translate(addr + 1, offset, na - 1);
}

/*
 * Array of bus specific translators
 */
static const struct dt_bus dt_busses[] =
{
    /* PCI */
    {
        .name = "pci",
        .addresses = "assigned-addresses",
        .match = dt_bus_pci_match,
        .count_cells = dt_bus_pci_count_cells,
        .map = dt_bus_pci_map,
        .translate = dt_bus_pci_translate,
        .get_flags = dt_bus_pci_get_flags,
    },
    /* Default */
    {
        .name = "default",
        .addresses = "reg",
        .match = dt_bus_default_match,
        .count_cells = dt_bus_default_count_cells,
        .map = dt_bus_default_map,
        .translate = dt_bus_default_translate,
        .get_flags = dt_bus_default_get_flags,
    },
};

static const struct dt_bus *dt_match_bus(const struct dt_device_node *np)
{
    int i;

    for ( i = 0; i < ARRAY_SIZE(dt_busses); i++ )
        if ( !dt_busses[i].match || dt_busses[i].match(np) )
            return &dt_busses[i];

    return NULL;
}

static const __be32 *dt_get_address(const struct dt_device_node *dev,
                                    unsigned int index, u64 *size,
                                    unsigned int *flags)
{
    const __be32 *prop;
    u32 psize;
    const struct dt_device_node *parent;
    const struct dt_bus *bus;
    int onesize, i, na, ns;

    /* Get parent & match bus type */
    parent = dt_get_parent(dev);
    if ( parent == NULL )
        return NULL;

    bus = dt_match_bus(parent);
    if ( !bus )
        return NULL;
    bus->count_cells(dev, &na, &ns);

    if ( !DT_CHECK_ADDR_COUNT(na) )
        return NULL;

    /* Get "reg" or "assigned-addresses" property */
    prop = dt_get_property(dev, bus->addresses, &psize);
    if ( prop == NULL )
        return NULL;
    psize /= 4;

    onesize = na + ns;
    for ( i = 0; psize >= onesize; psize -= onesize, prop += onesize, i++ )
    {
        if ( i == index )
        {
            if ( size )
                *size = dt_read_number(prop + na, ns);
            if ( flags )
                *flags = bus->get_flags(prop);
            return prop;
        }
    }
    return NULL;
}

static int dt_translate_one(const struct dt_device_node *parent,
                            const struct dt_bus *bus,
                            const struct dt_bus *pbus,
                            __be32 *addr, int na, int ns,
                            int pna, const char *rprop)
{
    const __be32 *ranges;
    unsigned int rlen;
    int rone;
    u64 offset = DT_BAD_ADDR;

    ranges = dt_get_property(parent, rprop, &rlen);
    if ( ranges == NULL )
    {
        printk(XENLOG_ERR "DT: no ranges; cannot translate\n");
        return 1;
    }
    if ( rlen == 0 )
    {
        offset = dt_read_number(addr, na);
        memset(addr, 0, pna * 4);
        dt_dprintk("DT: empty ranges; 1:1 translation\n");
        goto finish;
    }

    dt_dprintk("DT: walking ranges...\n");

    /* Now walk through the ranges */
    rlen /= 4;
    rone = na + pna + ns;
    for ( ; rlen >= rone; rlen -= rone, ranges += rone )
    {
        offset = bus->map(addr, ranges, na, ns, pna);
        if ( offset != DT_BAD_ADDR )
            break;
    }
    if ( offset == DT_BAD_ADDR )
    {
        dt_dprintk("DT: not found !\n");
        return 1;
    }
    memcpy(addr, ranges + na, 4 * pna);

finish:
    dt_dump_addr("DT: parent translation for:", addr, pna);
    dt_dprintk("DT: with offset: %llx\n", (unsigned long long)offset);

    /* Translate it into parent bus space */
    return pbus->translate(addr, offset, pna);
}

/*
 * Translate an address from the device-tree into a CPU physical address,
 * this walks up the tree and applies the various bus mappings on the
 * way.
 *
 * Note: We consider that crossing any level with #size-cells == 0 to mean
 * that translation is impossible (that is we are not dealing with a value
 * that can be mapped to a cpu physical address). This is not really specified
 * that way, but this is traditionally the way IBM at least do things
 */
static u64 __dt_translate_address(const struct dt_device_node *dev,
                                  const __be32 *in_addr, const char *rprop)
{
    const struct dt_device_node *parent = NULL;
    const struct dt_bus *bus, *pbus;
    __be32 addr[DT_MAX_ADDR_CELLS];
    int na, ns, pna, pns;
    u64 result = DT_BAD_ADDR;

    dt_dprintk("DT: ** translation for device %s **\n", dev->full_name);

    /* Get parent & match bus type */
    parent = dt_get_parent(dev);
    if ( parent == NULL )
        goto bail;
    bus = dt_match_bus(parent);
    if ( !bus )
        goto bail;

    /* Count address cells & copy address locally */
    bus->count_cells(dev, &na, &ns);
    if ( !DT_CHECK_COUNTS(na, ns) )
    {
        printk(XENLOG_ERR "dt_parse: Bad cell count for device %s\n",
                  dev->full_name);
        goto bail;
    }
    memcpy(addr, in_addr, na * 4);

    dt_dprintk("DT: bus is %s (na=%d, ns=%d) on %s\n",
               bus->name, na, ns, parent->full_name);
    dt_dump_addr("DT: translating address:", addr, na);

    /* Translate */
    for ( ;; )
    {
        /* Switch to parent bus */
        dev = parent;
        parent = dt_get_parent(dev);

        /* If root, we have finished */
        if ( parent == NULL )
        {
            dt_dprintk("DT: reached root node\n");
            result = dt_read_number(addr, na);
            break;
        }

        /* Get new parent bus and counts */
        pbus = dt_match_bus(parent);
        if ( pbus == NULL )
        {
            printk("DT: %s is not a valid bus\n", parent->full_name);
            break;
        }
        pbus->count_cells(dev, &pna, &pns);
        if ( !DT_CHECK_COUNTS(pna, pns) )
        {
            printk(XENLOG_ERR "dt_parse: Bad cell count for parent %s\n",
                   dev->full_name);
            break;
        }

        dt_dprintk("DT: parent bus is %s (na=%d, ns=%d) on %s\n",
                   pbus->name, pna, pns, parent->full_name);

        /* Apply bus translation */
        if ( dt_translate_one(dev, bus, pbus, addr, na, ns, pna, rprop) )
            break;

        /* Complete the move up one level */
        na = pna;
        ns = pns;
        bus = pbus;

        dt_dump_addr("DT: one level translation:", addr, na);
    }

bail:
    return result;
}

/* dt_device_address - Translate device tree address and return it */
int dt_device_get_address(const struct dt_device_node *dev, unsigned int index,
                          u64 *addr, u64 *size)
{
    const __be32 *addrp;
    unsigned int flags;

    addrp = dt_get_address(dev, index, size, &flags);
    if ( addrp == NULL )
        return -EINVAL;

    if ( !addr )
        return -EINVAL;

    *addr = __dt_translate_address(dev, addrp, "ranges");

    if ( *addr == DT_BAD_ADDR )
        return -EINVAL;

    return 0;
}

int dt_device_get_paddr(const struct dt_device_node *dev, unsigned int index,
                        paddr_t *addr, paddr_t *size)
{
    uint64_t dt_addr, dt_size;
    int ret;

    ret = dt_device_get_address(dev, index, &dt_addr, &dt_size);
    if ( ret )
        return ret;

    if ( !addr )
        return -EINVAL;

    if ( dt_addr != (paddr_t)dt_addr )
    {
        printk("Error: Physical address 0x%"PRIx64" for node=%s is greater than max width (%zu bytes) supported\n",
               dt_addr, dev->name, sizeof(paddr_t));
        return -ERANGE;
    }

    *addr = dt_addr;

    if ( size )
    {
        if ( dt_size != (paddr_t)dt_size )
        {
            printk("Error: Physical size 0x%"PRIx64" for node=%s is greater than max width (%zu bytes) supported\n",
                   dt_size, dev->name, sizeof(paddr_t));
            return -ERANGE;
        }

        *size = dt_size;
    }

    return ret;
}

int dt_for_each_range(const struct dt_device_node *dev,
                      int (*cb)(const struct dt_device_node *dev,
                                uint64_t addr, uint64_t length,
                                void *data),
                      void *data)
{
    const struct dt_device_node *parent = NULL;
    const struct dt_bus *bus, *pbus;
    const __be32 *ranges;
    __be32 addr[DT_MAX_ADDR_CELLS];
    unsigned int rlen;
    int na, ns, pna, pns, rone;

    bus = dt_match_bus(dev);
    if ( !bus )
        return 0; /* device is not a bus */

    parent = dt_get_parent(dev);
    if ( parent == NULL )
        return -EINVAL;

    ranges = dt_get_property(dev, "ranges", &rlen);
    if ( ranges == NULL )
    {
        printk(XENLOG_ERR "DT: no ranges; cannot enumerate %s\n",
               dev->full_name);
        return -EINVAL;
    }
    if ( rlen == 0 ) /* Nothing to do */
        return 0;

    bus->count_cells(dev, &na, &ns);
    if ( !DT_CHECK_COUNTS(na, ns) )
    {
        printk(XENLOG_ERR "dt_parse: Bad cell count for device %s\n",
                  dev->full_name);
        return -EINVAL;
    }

    pbus = dt_match_bus(parent);
    if ( pbus == NULL )
    {
        printk("DT: %s is not a valid bus\n", parent->full_name);
        return -EINVAL;
    }

    pbus->count_cells(dev, &pna, &pns);
    if ( !DT_CHECK_COUNTS(pna, pns) )
    {
        printk(XENLOG_ERR "dt_parse: Bad cell count for parent %s\n",
               dev->full_name);
        return -EINVAL;
    }

    /* Now walk through the ranges */
    rlen /= 4;
    rone = na + pna + ns;

    dt_dprintk("%s: dev=%s, bus=%s, parent=%s, rlen=%d, rone=%d\n",
               __func__,
               dt_node_name(dev), bus->name,
               dt_node_name(parent), rlen, rone);

    for ( ; rlen >= rone; rlen -= rone, ranges += rone )
    {
        uint64_t a, s;
        int ret;

        memcpy(addr, ranges + na, 4 * pna);

        a = __dt_translate_address(dev, addr, "ranges");
        s = dt_read_number(ranges + na + pna, ns);

        ret = cb(dev, a, s, data);
        if ( ret )
        {
            dt_dprintk(" -> callback failed=%d\n", ret);
            return ret;
        }

    }

    return 0;
}

/**
 * dt_find_node_by_phandle - Find a node given a phandle
 * @handle: phandle of the node to find
 *
 * Returns a node pointer.
 */
struct dt_device_node *dt_find_node_by_phandle(dt_phandle handle)
{
    struct dt_device_node *np;

    dt_for_each_device_node(dt_host, np)
        if ( np->phandle == handle )
            break;

    return np;
}

/**
 * dt_irq_find_parent - Given a device node, find its interrupt parent node
 * @child: pointer to device node
 *
 * Returns a pointer to the interrupt parent node, or NULL if the interrupt
 * parent could not be determined.
 */
static const struct dt_device_node *
dt_irq_find_parent(const struct dt_device_node *child)
{
    const struct dt_device_node *p;
    const __be32 *parp;

    do
    {
        parp = dt_get_property(child, "interrupt-parent", NULL);
        if ( parp == NULL )
            p = dt_get_parent(child);
        else
            p = dt_find_node_by_phandle(be32_to_cpup(parp));
        child = p;
    } while ( p && dt_get_property(p, "#interrupt-cells", NULL) == NULL );

    return p;
}

unsigned int dt_number_of_irq(const struct dt_device_node *device)
{
    const struct dt_device_node *p;
    const __be32 *intspec, *tmp;
    u32 intsize, intlen;
    int intnum;

    dt_dprintk("dt_irq_number: dev=%s\n", device->full_name);

    /* Try the new-style interrupts-extended first */
    intnum = dt_count_phandle_with_args(device, "interrupts-extended",
                                        "#interrupt-cells");
    if ( intnum >= 0 )
    {
        dt_dprintk(" using 'interrupts-extended' property\n");
        dt_dprintk(" intnum=%d\n", intnum);
        return intnum;
    }

    /* Get the interrupts property */
    intspec = dt_get_property(device, "interrupts", &intlen);
    if ( intspec == NULL )
        return 0;
    intlen /= sizeof(*intspec);

    dt_dprintk(" using 'interrupts' property\n");
    dt_dprintk(" intspec=%d intlen=%d\n", be32_to_cpup(intspec), intlen);

    /* Look for the interrupt parent. */
    p = dt_irq_find_parent(device);
    if ( p == NULL )
        return 0;

    /* Get size of interrupt specifier */
    tmp = dt_get_property(p, "#interrupt-cells", NULL);
    if ( tmp == NULL )
        return 0;
    intsize = be32_to_cpu(*tmp);

    dt_dprintk(" intsize=%d intlen=%d\n", intsize, intlen);

    return (intlen / intsize);
}

unsigned int dt_number_of_address(const struct dt_device_node *dev)
{
    const __be32 *prop;
    u32 psize;
    const struct dt_device_node *parent;
    const struct dt_bus *bus;
    int onesize, na, ns;

    /* Get parent & match bus type */
    parent = dt_get_parent(dev);
    if ( parent == NULL )
        return 0;

    bus = dt_match_bus(parent);
    if ( !bus )
        return 0;
    bus->count_cells(dev, &na, &ns);

    if ( !DT_CHECK_COUNTS(na, ns) )
        return 0;

    /* Get "reg" or "assigned-addresses" property */
    prop = dt_get_property(dev, bus->addresses, &psize);
    if ( prop == NULL )
        return 0;

    psize /= 4;
    onesize = na + ns;

    return (psize / onesize);
}

int dt_for_each_irq_map(const struct dt_device_node *dev,
                        int (*cb)(const struct dt_device_node *dev,
                                  const struct dt_irq *dt_irq,
                                  void *data),
                        void *data)
{
    const struct dt_device_node *ipar, *tnode, *old = NULL;
    const __be32 *tmp, *imap;
    u32 intsize = 1, addrsize, pintsize = 0, paddrsize = 0;
    u32 imaplen;
    int i, ret;

    struct dt_raw_irq dt_raw_irq;
    struct dt_irq dt_irq;

    dt_dprintk("%s: par=%s cb=%p data=%p\n", __func__,
               dev->full_name, cb, data);

    ipar = dev;

    /* First get the #interrupt-cells property of the current cursor
     * that tells us how to interpret the passed-in intspec. If there
     * is none, we are nice and just walk up the tree
     */
    do {
        tmp = dt_get_property(ipar, "#interrupt-cells", NULL);
        if ( tmp != NULL )
        {
            intsize = be32_to_cpu(*tmp);
            break;
        }
        tnode = ipar;
        ipar = dt_irq_find_parent(ipar);
    } while ( ipar );
    if ( ipar == NULL )
    {
        dt_dprintk(" -> no parent found !\n");
        goto fail;
    }

    dt_dprintk("%s: ipar=%s, size=%d\n", __func__, ipar->full_name, intsize);

    if ( intsize > DT_MAX_IRQ_SPEC )
    {
        dt_dprintk(" -> too many irq specifier cells\n");
        goto fail;
    }

    /* Look for this #address-cells. We have to implement the old linux
     * trick of looking for the parent here as some device-trees rely on it
     */
    old = ipar;
    do {
        tmp = dt_get_property(old, "#address-cells", NULL);
        tnode = dt_get_parent(old);
        old = tnode;
    } while ( old && tmp == NULL );

    old = NULL;
    addrsize = (tmp == NULL) ? 2 : be32_to_cpu(*tmp);

    dt_dprintk(" -> addrsize=%d\n", addrsize);

    /* Now look for an interrupt-map */
    imap = dt_get_property(dev, "interrupt-map", &imaplen);
    /* No interrupt-map found. Ignore */
    if ( imap == NULL )
    {
        dt_dprintk(" -> no map, ignoring\n");
        return 0;
    }
    imaplen /= sizeof(u32);

    /* Parse interrupt-map */
    while ( imaplen > (addrsize + intsize + 1) )
    {
        /* skip child unit address and child interrupt specifier */
        imap += addrsize + intsize;
        imaplen -= addrsize + intsize;

        /* Get the interrupt parent */
        ipar = dt_find_node_by_phandle(be32_to_cpup(imap));
        imap++;
        --imaplen;

        /* Check if not found */
        if ( ipar == NULL )
        {
            dt_dprintk(" -> imap parent not found !\n");
            goto fail;
        }

        dt_dprintk(" -> ipar %s\n", dt_node_name(ipar));

        /* Get #interrupt-cells and #address-cells of new
         * parent
         */
        tmp = dt_get_property(ipar, "#interrupt-cells", NULL);
        if ( tmp == NULL )
        {
            dt_dprintk(" -> parent lacks #interrupt-cells!\n");
            goto fail;
        }
        pintsize = be32_to_cpu(*tmp);
        tmp = dt_get_property(ipar, "#address-cells", NULL);
        paddrsize = (tmp == NULL) ? 0 : be32_to_cpu(*tmp);

        dt_dprintk(" -> pintsize=%d, paddrsize=%d\n",
                   pintsize, paddrsize);

        if ( pintsize > DT_MAX_IRQ_SPEC )
        {
            dt_dprintk(" -> too many irq specifier cells in parent\n");
            goto fail;
        }

        /* Check for malformed properties */
        if ( imaplen < (paddrsize + pintsize) )
            goto fail;

        imap += paddrsize;
        imaplen -= paddrsize;

        dt_raw_irq.controller = ipar;
        dt_raw_irq.size = pintsize;
        for ( i = 0; i < pintsize; i++ )
            dt_raw_irq.specifier[i] = dt_read_number(imap + i, 1);

        if ( dt_raw_irq.controller != dt_interrupt_controller )
        {
            /*
             * We don't map IRQs connected to secondary IRQ controllers as
             * these IRQs have no meaning to us until they connect to the
             * primary controller.
             *
             * Secondary IRQ controllers will at some point connect to
             * the primary controller (possibly via other IRQ controllers).
             * We map the IRQs at that last connection point.
             */
            imap += pintsize;
            imaplen -= pintsize;
            dt_dprintk(" -> Skipped IRQ for secondary IRQ controller\n");
            continue;
        }

        ret = dt_irq_translate(&dt_raw_irq, &dt_irq);
        if ( ret )
        {
            dt_dprintk(" -> failed to translate IRQ: %d\n", ret);
            return ret;
        }

        ret = cb(dev, &dt_irq, data);
        if ( ret )
        {
            dt_dprintk(" -> callback failed=%d\n", ret);
            return ret;
        }

        imap += pintsize;
        imaplen -= pintsize;

        dt_dprintk(" -> imaplen=%d\n", imaplen);
    }

    return 0;

fail:
    return -EINVAL;
}

/**
 * dt_irq_map_raw - Low level interrupt tree parsing
 * @parent:     the device interrupt parent
 * @intspec:    interrupt specifier ("interrupts" property of the device)
 * @ointsize:   size of the passed in interrupt specifier
 * @addr:       address specifier (start of "reg" property of the device)
 * @oirq:       structure dt_raw_irq filled by this function
 *
 * Returns 0 on success and a negative number on error
 *
 * This function is a low-level interrupt tree walking function. It
 * can be used to do a partial walk with synthesized reg and interrupts
 * properties, for example when resolving PCI interrupts when no device
 * node exist for the parent.
 */
static int dt_irq_map_raw(const struct dt_device_node *parent,
                          const __be32 *intspec, u32 ointsize,
                          const __be32 *addr,
                          struct dt_raw_irq *oirq)
{
    const struct dt_device_node *ipar, *tnode, *old = NULL, *newpar = NULL;
    const __be32 *tmp, *imap, *imask;
    u32 intsize = 1, addrsize, newintsize = 0, newaddrsize = 0;
    u32 imaplen;
    int match, i;

    dt_dprintk("dt_irq_map_raw: par=%s,intspec=[0x%08x 0x%08x...],ointsize=%d\n",
               parent->full_name, be32_to_cpup(intspec),
               be32_to_cpup(intspec + 1), ointsize);

    ipar = parent;

    /* First get the #interrupt-cells property of the current cursor
     * that tells us how to interpret the passed-in intspec. If there
     * is none, we are nice and just walk up the tree
     */
    do {
        tmp = dt_get_property(ipar, "#interrupt-cells", NULL);
        if ( tmp != NULL )
        {
            intsize = be32_to_cpu(*tmp);
            break;
        }
        tnode = ipar;
        ipar = dt_irq_find_parent(ipar);
    } while ( ipar );
    if ( ipar == NULL )
    {
        dt_dprintk(" -> no parent found !\n");
        goto fail;
    }

    dt_dprintk("dt_irq_map_raw: ipar=%s, size=%d\n", ipar->full_name, intsize);

    if ( ointsize != intsize )
        return -EINVAL;

    /* Look for this #address-cells. We have to implement the old linux
     * trick of looking for the parent here as some device-trees rely on it
     */
    old = ipar;
    do {
        tmp = dt_get_property(old, "#address-cells", NULL);
        tnode = dt_get_parent(old);
        old = tnode;
    } while ( old && tmp == NULL );

    old = NULL;
    addrsize = (tmp == NULL) ? 2 : be32_to_cpu(*tmp);

    dt_dprintk(" -> addrsize=%d\n", addrsize);

    /* Now start the actual "proper" walk of the interrupt tree */
    while ( ipar != NULL )
    {
        /* Now check if cursor is an interrupt-controller and if it is
         * then we are done
         */
        if ( dt_get_property(ipar, "interrupt-controller", NULL) != NULL )
        {
            dt_dprintk(" -> got it !\n");
            if ( intsize > DT_MAX_IRQ_SPEC )
            {
                dt_dprintk(" -> intsize(%u) greater than DT_MAX_IRQ_SPEC(%u)\n",
                           intsize, DT_MAX_IRQ_SPEC);
                goto fail;
            }
            for ( i = 0; i < intsize; i++ )
                oirq->specifier[i] = dt_read_number(intspec + i, 1);
            oirq->size = intsize;
            oirq->controller = ipar;
            return 0;
        }

        /* Now look for an interrupt-map */
        imap = dt_get_property(ipar, "interrupt-map", &imaplen);
        /* No interrupt map, check for an interrupt parent */
        if ( imap == NULL )
        {
            dt_dprintk(" -> no map, getting parent\n");
            newpar = dt_irq_find_parent(ipar);
            goto skiplevel;
        }
        imaplen /= sizeof(u32);

        /* Look for a mask */
        imask = dt_get_property(ipar, "interrupt-map-mask", NULL);

        /* If we were passed no "reg" property and we attempt to parse
         * an interrupt-map, then #address-cells must be 0.
         * Fail if it's not.
         */
        if ( addr == NULL && addrsize != 0 )
        {
            dt_dprintk(" -> no reg passed in when needed !\n");
            goto fail;
        }

        /* Parse interrupt-map */
        match = 0;
        while ( imaplen > (addrsize + intsize + 1) && !match )
        {
            /* Compare specifiers */
            match = 1;
            for ( i = 0; i < addrsize && match; ++i )
            {
                __be32 mask = imask ? imask[i] : cpu_to_be32(0xffffffffu);
                match = ((addr[i] ^ imap[i]) & mask) == 0;
            }
            for ( ; i < (addrsize + intsize) && match; ++i )
            {
                __be32 mask = imask ? imask[i] : cpu_to_be32(0xffffffffu);
                match = ((intspec[i-addrsize] ^ imap[i]) & mask) == 0;
            }
            imap += addrsize + intsize;
            imaplen -= addrsize + intsize;

            dt_dprintk(" -> match=%d (imaplen=%d)\n", match, imaplen);

            /* Get the interrupt parent */
            newpar = dt_find_node_by_phandle(be32_to_cpup(imap));
            imap++;
            --imaplen;

            /* Check if not found */
            if ( newpar == NULL )
            {
                dt_dprintk(" -> imap parent not found !\n");
                goto fail;
            }

            /* Get #interrupt-cells and #address-cells of new
             * parent
             */
            tmp = dt_get_property(newpar, "#interrupt-cells", NULL);
            if ( tmp == NULL )
            {
                dt_dprintk(" -> parent lacks #interrupt-cells!\n");
                goto fail;
            }
            newintsize = be32_to_cpu(*tmp);
            tmp = dt_get_property(newpar, "#address-cells", NULL);
            newaddrsize = (tmp == NULL) ? 0 : be32_to_cpu(*tmp);

            dt_dprintk(" -> newintsize=%d, newaddrsize=%d\n",
                       newintsize, newaddrsize);

            /* Check for malformed properties */
            if ( imaplen < (newaddrsize + newintsize) )
                goto fail;

            imap += newaddrsize + newintsize;
            imaplen -= newaddrsize + newintsize;

            dt_dprintk(" -> imaplen=%d\n", imaplen);
        }
        if ( !match )
            goto fail;

        old = newpar;
        addrsize = newaddrsize;
        intsize = newintsize;
        intspec = imap - intsize;
        addr = intspec - addrsize;

    skiplevel:
        /* Iterate again with new parent */
        dt_dprintk(" -> new parent: %s\n", dt_node_full_name(newpar));
        ipar = newpar;
        newpar = NULL;
    }
fail:
    return -EINVAL;
}

int dt_device_get_raw_irq(const struct dt_device_node *device,
                          unsigned int index,
                          struct dt_raw_irq *out_irq)
{
    const struct dt_device_node *p;
    const __be32 *intspec, *tmp, *addr;
    u32 intsize, intlen;
    int res = -EINVAL;
    struct dt_phandle_args args;
    int i;

    dt_dprintk("dt_device_get_raw_irq: dev=%s, index=%u\n",
               device->full_name, index);

    /* Get the reg property (if any) */
    addr = dt_get_property(device, "reg", NULL);

    /* Try the new-style interrupts-extended first */
    res = dt_parse_phandle_with_args(device, "interrupts-extended",
                                     "#interrupt-cells", index, &args);
    if ( !res )
    {
        dt_dprintk(" using 'interrupts-extended' property\n");
        dt_dprintk(" intspec=%d intsize=%d\n", args.args[0], args.args_count);

        for ( i = 0; i < args.args_count; i++ )
            args.args[i] = cpu_to_be32(args.args[i]);

        return dt_irq_map_raw(args.np, args.args, args.args_count,
                              addr, out_irq);
    }

    /* Get the interrupts property */
    intspec = dt_get_property(device, "interrupts", &intlen);
    if ( intspec == NULL )
        return -EINVAL;
    intlen /= sizeof(*intspec);

    dt_dprintk(" using 'interrupts' property\n");
    dt_dprintk(" intspec=%d intlen=%d\n", be32_to_cpup(intspec), intlen);

    /* Look for the interrupt parent. */
    p = dt_irq_find_parent(device);
    if ( p == NULL )
        return -EINVAL;

    /* Get size of interrupt specifier */
    tmp = dt_get_property(p, "#interrupt-cells", NULL);
    if ( tmp == NULL )
        goto out;
    intsize = be32_to_cpu(*tmp);

    dt_dprintk(" intsize=%d intlen=%d\n", intsize, intlen);

    /* Check index */
    if ( (index + 1) * intsize > intlen )
        goto out;

    /* Get new specifier and map it */
    res = dt_irq_map_raw(p, intspec + index * intsize, intsize,
                         addr, out_irq);
    if ( res )
        goto out;
out:
    return res;
}

int dt_irq_translate(const struct dt_raw_irq *raw,
                     struct dt_irq *out_irq)
{
    ASSERT(dt_irq_xlate != NULL);
    ASSERT(dt_interrupt_controller != NULL);

    /*
     * TODO: Retrieve the right irq_xlate. This is only works for the primary
     * interrupt controller.
     */
    if ( raw->controller != dt_interrupt_controller )
        return -EINVAL;

    return dt_irq_xlate(raw->specifier, raw->size,
                        &out_irq->irq, &out_irq->type);
}

int dt_device_get_irq(const struct dt_device_node *device, unsigned int index,
                      struct dt_irq *out_irq)
{
    struct dt_raw_irq raw;
    int res;

    res = dt_device_get_raw_irq(device, index, &raw);

    if ( res )
        return res;

    return dt_irq_translate(&raw, out_irq);
}

bool dt_device_is_available(const struct dt_device_node *device)
{
    const char *status;
    u32 statlen;

    status = dt_get_property(device, "status", &statlen);
    if ( status == NULL )
        return 1;

    if ( statlen > 0 )
    {
        if ( !strcmp(status, "okay") || !strcmp(status, "ok") )
            return 1;
    }

    return 0;
}

bool dt_device_for_passthrough(const struct dt_device_node *device)
{
    return (dt_find_property(device, "xen,passthrough", NULL) != NULL);

}

static int __dt_parse_phandle_with_args(const struct dt_device_node *np,
                                        const char *list_name,
                                        const char *cells_name,
                                        int cell_count, int index,
                                        struct dt_phandle_args *out_args)
{
    const __be32 *list, *list_end;
    int rc = 0, cur_index = 0;
    u32 size, count = 0;
    struct dt_device_node *node = NULL;
    dt_phandle phandle;

    /* Retrieve the phandle list property */
    list = dt_get_property(np, list_name, &size);
    if ( !list )
        return -ENOENT;
    list_end = list + size / sizeof(*list);

    /* Loop over the phandles until all the requested entry is found */
    while ( list < list_end )
    {
        rc = -EINVAL;
        count = 0;

        /*
         * If phandle is 0, then it is an empty entry with no
         * arguments.  Skip forward to the next entry.
         * */
        phandle = be32_to_cpup(list++);
        if ( phandle )
        {
            /*
             * Find the provider node and parse the #*-cells
             * property to determine the argument length.
             *
             * This is not needed if the cell count is hard-coded
             * (i.e. cells_name not set, but cell_count is set),
             * except when we're going to return the found node
             * below.
             */
            if ( cells_name || cur_index == index )
            {
                node = dt_find_node_by_phandle(phandle);
                if ( !node )
                {
                    printk(XENLOG_ERR "%s: could not find phandle\n",
                           np->full_name);
                    goto err;
                }
            }

            if ( cells_name )
            {
                if ( !dt_property_read_u32(node, cells_name, &count) )
                {
                    printk("%s: could not get %s for %s\n",
                           np->full_name, cells_name, node->full_name);
                    goto err;
                }
            }
            else
                count = cell_count;

            /*
             * Make sure that the arguments actually fit in the
             * remaining property data length
             */
            if ( list + count > list_end )
            {
                printk(XENLOG_ERR "%s: arguments longer than property\n",
                       np->full_name);
                goto err;
            }
        }

        /*
         * All of the error cases above bail out of the loop, so at
         * this point, the parsing is successful. If the requested
         * index matches, then fill the out_args structure and return,
         * or return -ENOENT for an empty entry.
         */
        rc = -ENOENT;
        if ( cur_index == index )
        {
            if (!phandle)
                goto err;

            if ( out_args )
            {
                int i;

                WARN_ON(count > MAX_PHANDLE_ARGS);
                if (count > MAX_PHANDLE_ARGS)
                    count = MAX_PHANDLE_ARGS;
                out_args->np = node;
                out_args->args_count = count;
                for ( i = 0; i < count; i++ )
                    out_args->args[i] = be32_to_cpup(list++);
            }

            /* Found it! return success */
            return 0;
        }

        node = NULL;
        list += count;
        cur_index++;
    }

    /*
     * Returning result will be one of:
     * -ENOENT : index is for empty phandle
     * -EINVAL : parsing error on data
     * [1..n]  : Number of phandle (count mode; when index = -1)
     */
    rc = index < 0 ? cur_index : -ENOENT;
err:
    return rc;
}

struct dt_device_node *dt_parse_phandle(const struct dt_device_node *np,
                                        const char *phandle_name, int index)
{
    struct dt_phandle_args args;

    if (index < 0)
        return NULL;

    if (__dt_parse_phandle_with_args(np, phandle_name, NULL, 0,
                                     index, &args))
        return NULL;

    return args.np;
}


int dt_parse_phandle_with_args(const struct dt_device_node *np,
                               const char *list_name,
                               const char *cells_name, int index,
                               struct dt_phandle_args *out_args)
{
    if ( index < 0 )
        return -EINVAL;
    return __dt_parse_phandle_with_args(np, list_name, cells_name, 0,
                                        index, out_args);
}

int dt_count_phandle_with_args(const struct dt_device_node *np,
                               const char *list_name,
                               const char *cells_name)
{
    return __dt_parse_phandle_with_args(np, list_name, cells_name, 0, -1, NULL);
}

/**
 * unflatten_dt_node - Alloc and populate a device_node from the flat tree
 * @fdt: The parent device tree blob
 * @mem: Memory chunk to use for allocating device nodes and properties
 * @p: pointer to node in flat tree
 * @dad: Parent struct device_node
 * @allnextpp: pointer to ->allnext from last allocated device_node
 * @fpsize: Size of the node path up at the current depth.
 */
static unsigned long unflatten_dt_node(const void *fdt,
                                       unsigned long mem,
                                       unsigned long *p,
                                       struct dt_device_node *dad,
                                       struct dt_device_node ***allnextpp,
                                       unsigned long fpsize)
{
    struct dt_device_node *np;
    struct dt_property *pp, **prev_pp = NULL;
    char *pathp;
    u32 tag;
    unsigned int l, allocl;
    int has_name = 0;
    int new_format = 0;

    tag = be32_to_cpup((__be32 *)(*p));
    if ( tag != FDT_BEGIN_NODE )
    {
        printk(XENLOG_WARNING "Weird tag at start of node: %x\n", tag);
        return mem;
    }
    *p += 4;
    pathp = (char *)*p;
    l = allocl = strlen(pathp) + 1;
    *p = ROUNDUP(*p + l, 4);

    /* version 0x10 has a more compact unit name here instead of the full
     * path. we accumulate the full path size using "fpsize", we'll rebuild
     * it later. We detect this because the first character of the name is
     * not '/'.
     */
    if ( (*pathp) != '/' )
    {
        new_format = 1;
        if ( fpsize == 0 )
        {
            /* root node: special case. fpsize accounts for path
             * plus terminating zero. root node only has '/', so
             * fpsize should be 2, but we want to avoid the first
             * level nodes to have two '/' so we use fpsize 1 here
             */
            fpsize = 1;
            allocl = 2;
        }
        else
        {
            /* account for '/' and path size minus terminal 0
             * already in 'l'
             */
            fpsize += l;
            allocl = fpsize;
        }
    }

    np = unflatten_dt_alloc(&mem, sizeof(struct dt_device_node) + allocl,
                            __alignof__(struct dt_device_node));
    if ( allnextpp )
    {
        memset(np, 0, sizeof(*np));
        np->full_name = ((char *)np) + sizeof(struct dt_device_node);
        /* By default dom0 owns the device */
        np->used_by = 0;
        /* By default the device is not protected */
        np->is_protected = false;
        INIT_LIST_HEAD(&np->domain_list);

        if ( new_format )
        {
            char *fn = np->full_name;
            /* rebuild full path for new format */
            if ( dad && dad->parent )
            {
                strlcpy(fn, dad->full_name, allocl);
#ifdef DEBUG_DT
                if ( (strlen(fn) + l + 1) != allocl )
                {
                    dt_dprintk("%s: p: %d, l: %d, a: %d\n",
                               pathp, (int)strlen(fn),
                               l, allocl);
                }
#endif
                fn += strlen(fn);
            }
            *(fn++) = '/';
            memcpy(fn, pathp, l);
        }
        else
            memcpy(np->full_name, pathp, l);
        prev_pp = &np->properties;
        **allnextpp = np;
        *allnextpp = &np->allnext;
        if ( dad != NULL )
        {
            np->parent = dad;
            /* we temporarily use the next field as `last_child'*/
            if ( dad->next == NULL )
                dad->child = np;
            else
                dad->next->sibling = np;
            dad->next = np;
        }
    }
    /* process properties */
    while ( 1 )
    {
        u32 sz, noff;
        const char *pname;

        tag = be32_to_cpup((__be32 *)(*p));
        if ( tag == FDT_NOP )
        {
            *p += 4;
            continue;
        }
        if ( tag != FDT_PROP )
            break;
        *p += 4;
        sz = be32_to_cpup((__be32 *)(*p));
        noff = be32_to_cpup((__be32 *)((*p) + 4));
        *p += 8;
        if ( fdt_version(fdt) < 0x10 )
            *p = ROUNDUP(*p, sz >= 8 ? 8 : 4);

        pname = fdt_string(fdt, noff);
        if ( pname == NULL )
        {
            dt_dprintk("Can't find property name in list!\n");
            break;
        }
        if ( strcmp(pname, "name") == 0 )
            has_name = 1;
        l = strlen(pname) + 1;
        pp = unflatten_dt_alloc(&mem, sizeof(struct dt_property),
                                __alignof__(struct dt_property));
        if ( allnextpp )
        {
            /* We accept flattened tree phandles either in
             * ePAPR-style "phandle" properties, or the
             * legacy "linux,phandle" properties.  If both
             * appear and have different values, things
             * will get weird.  Don't do that. */
            if ( (strcmp(pname, "phandle") == 0) ||
                 (strcmp(pname, "linux,phandle") == 0) )
            {
                if ( np->phandle == 0 )
                    np->phandle = be32_to_cpup((__be32*)*p);
            }
            /* And we process the "ibm,phandle" property
             * used in pSeries dynamic device tree
             * stuff */
            if ( strcmp(pname, "ibm,phandle") == 0 )
                np->phandle = be32_to_cpup((__be32 *)*p);
            pp->name = pname;
            pp->length = sz;
            pp->value = (void *)*p;
            *prev_pp = pp;
            prev_pp = &pp->next;
        }
        *p = ROUNDUP((*p) + sz, 4);
    }
    /* with version 0x10 we may not have the name property, recreate
     * it here from the unit name if absent
     */
    if ( !has_name )
    {
        char *p1 = pathp, *ps = pathp, *pa = NULL;
        int sz;

        while ( *p1 )
        {
            if ( (*p1) == '@' )
                pa = p1;
            if ( (*p1) == '/' )
                ps = p1 + 1;
            p1++;
        }
        if ( pa < ps )
            pa = p1;
        sz = (pa - ps) + 1;
        pp = unflatten_dt_alloc(&mem, sizeof(struct dt_property) + sz,
                                __alignof__(struct dt_property));
        if ( allnextpp )
        {
            pp->name = "name";
            pp->length = sz;
            pp->value = pp + 1;
            /*
             * The device tree creation code assume that the property
             * "name" is not a fake.
             * To avoid a big divergence with Linux code, only remove
             * property link. In this case we will lose a bit of memory
             */
#if 0
            *prev_pp = pp;
            prev_pp = &pp->next;
#endif
            np->name = pp->value;
            memcpy(pp->value, ps, sz - 1);
            ((char *)pp->value)[sz - 1] = 0;
            dt_dprintk("fixed up name for %s -> %s\n", pathp,
                       (char *)pp->value);
            /* Generic device initialization */
            np->dev.type = DEV_DT;
            np->dev.of_node = np;
        }
    }
    if ( allnextpp )
    {
        *prev_pp = NULL;
        np->name = (np->name) ? : dt_get_property(np, "name", NULL);
        np->type = dt_get_property(np, "device_type", NULL);

        if ( !np->name )
            np->name = "<NULL>";
        if ( !np->type )
            np->type = "<NULL>";
    }
    while ( tag == FDT_BEGIN_NODE || tag == FDT_NOP )
    {
        if ( tag == FDT_NOP )
            *p += 4;
        else
            mem = unflatten_dt_node(fdt, mem, p, np, allnextpp, fpsize);
        tag = be32_to_cpup((__be32 *)(*p));
    }
    if ( tag != FDT_END_NODE )
    {
        printk(XENLOG_WARNING "Weird tag at end of node: %x\n", tag);
        return mem;
    }

    *p += 4;
    return mem;
}

int unflatten_device_tree(const void *fdt, struct dt_device_node **mynodes)
{
    unsigned long start, mem, size;
    struct dt_device_node **allnextp = mynodes;

    dt_dprintk(" -> unflatten_device_tree()\n");

    dt_dprintk("Unflattening device tree:\n");
    dt_dprintk("magic: %#08x\n", fdt_magic(fdt));
    dt_dprintk("size: %#08x\n", fdt_totalsize(fdt));
    dt_dprintk("version: %#08x\n", fdt_version(fdt));

    /* First pass, scan for size */
    start = ((unsigned long)fdt) + fdt_off_dt_struct(fdt);
    size = unflatten_dt_node(fdt, 0, &start, NULL, NULL, 0);
    if ( !size )
        return -EINVAL;

    size = (size | 3) + 1;

    dt_dprintk("  size is %#lx allocating...\n", size);

    /* Allocate memory for the expanded device tree */
    mem = (unsigned long)_xmalloc (size + 4, __alignof__(struct dt_device_node));  // _xmalloc crashed here
    if ( !mem )
        return -ENOMEM;

    ((__be32 *)mem)[size / 4] = cpu_to_be32(0xdeadbeefU);

    dt_dprintk("  unflattening %lx...\n", mem);

    /* Second pass, do actual unflattening */
    start = ((unsigned long)fdt) + fdt_off_dt_struct(fdt);
    unflatten_dt_node(fdt, mem, &start, NULL, &allnextp, 0);
    if ( be32_to_cpup((__be32 *)start) != FDT_END )
    {
        printk(XENLOG_ERR "Weird tag at end of tree: %08x\n",
                  *((u32 *)start));
        xfree((void *)mem);
        return -EINVAL;
    }

    if ( be32_to_cpu(((__be32 *)mem)[size / 4]) != 0xdeadbeefU )
    {
        printk(XENLOG_ERR "End of tree marker overwritten: %08x\n",
                  be32_to_cpu(((__be32 *)mem)[size / 4]));
        xfree((void *)mem);
        return -EINVAL;
    }

    *allnextp = NULL;

    dt_dprintk(" <- unflatten_device_tree()\n");

    return 0;
}

static void dt_alias_add(struct dt_alias_prop *ap,
                         struct dt_device_node *np,
                         int id, const char *stem, int stem_len)
{
    ap->np = np;
    ap->id = id;
    strlcpy(ap->stem, stem, stem_len + 1);
    list_add_tail(&ap->link, &aliases_lookup);
    dt_dprintk("adding DT alias:%s: stem=%s id=%d node=%s\n",
               ap->alias, ap->stem, ap->id, dt_node_full_name(np));
}

/**
 * dt_alias_scan - Scan all properties of 'aliases' node
 *
 * The function scans all the properties of 'aliases' node and populate
 * the the global lookup table with the properties.  It returns the
 * number of alias_prop found, or error code in error case.
 */
static void __init dt_alias_scan(void)
{
    const struct dt_property *pp;
    const struct dt_device_node *aliases;

    aliases = dt_find_node_by_path("/aliases");
    if ( !aliases )
        return;

    dt_for_each_property_node( aliases, pp )
    {
        const char *start = pp->name;
        const char *end = start + strlen(start);
        struct dt_device_node *np;
        struct dt_alias_prop *ap;
        int id, len;

        /* Skip those we do not want to proceed */
        if ( !strcmp(pp->name, "name") ||
             !strcmp(pp->name, "phandle") ||
             !strcmp(pp->name, "linux,phandle") )
            continue;

        np = dt_find_node_by_path(pp->value);
        if ( !np )
            continue;

        /* walk the alias backwards to extract the id and work out
         * the 'stem' string */
        while ( isdigit(*(end-1)) && end > start )
            end--;
        len = end - start;

        id = simple_strtoll(end, NULL, 10);

        /* Allocate an alias_prop with enough space for the stem */
        ap = _xmalloc(sizeof(*ap) + len + 1, 4);
        if ( !ap )
            continue;
        ap->alias = start;
        dt_alias_add(ap, np, id, start, len);
    }
}


void __init dt_unflatten_host_device_tree(void)
{
    int error = unflatten_device_tree(device_tree_flattened, &dt_host);

    if ( error )
        panic("unflatten_device_tree failed with error %d\n", error);

    dt_alias_scan();
}


/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: common_device_tree.c === */
/* === BEGIN INLINED: device_tree.c === */
#include <xen_xen_config.h>
/*
 * Code to passthrough a device tree node to a guest
 *
 * Julien Grall <julien.grall@linaro.org>
 * Copyright (c) 2014 Linaro Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <xen_device_tree.h>
#include <xen_guest_access.h>
#include <xen_iommu.h>
#include <xen_lib.h>
#include <xen_sched.h>
#include <xsm_xsm.h>

#include <asm_iommu_fwspec.h>

static spinlock_t dtdevs_lock = SPIN_LOCK_UNLOCKED;

int iommu_assign_dt_device(struct domain *d, struct dt_device_node *dev)
{
    int rc = -EBUSY;
    struct domain_iommu *hd = dom_iommu(d);

    ASSERT(system_state < SYS_STATE_active || rw_is_locked(&dt_host_lock));

    if ( !is_iommu_enabled(d) )
        return -EINVAL;

    if ( !dt_device_is_protected(dev) )
        return -EINVAL;

    spin_lock(&dtdevs_lock);

    if ( !list_empty(&dev->domain_list) )
        goto fail;

    /* The flag field doesn't matter to DT device. */
    rc = hd->platform_ops->assign_device(d, 0, dt_to_dev(dev), 0);

    if ( rc )
        goto fail;

    list_add(&dev->domain_list, &hd->dt_devices);
    dt_device_set_used_by(dev, d->domain_id);

fail:
    spin_unlock(&dtdevs_lock);

    return rc;
}

int iommu_deassign_dt_device(struct domain *d, struct dt_device_node *dev)
{
    const struct domain_iommu *hd = dom_iommu(d);
    int rc;

    ASSERT(rw_is_locked(&dt_host_lock));

    if ( !is_iommu_enabled(d) )
        return -EINVAL;

    if ( !dt_device_is_protected(dev) )
        return -EINVAL;

    spin_lock(&dtdevs_lock);

    rc = hd->platform_ops->reassign_device(d, NULL, 0, dt_to_dev(dev));
    if ( rc )
        goto fail;

    list_del_init(&dev->domain_list);
    dt_device_set_used_by(dev, DOMID_IO);

fail:
    spin_unlock(&dtdevs_lock);

    return rc;
}

static bool iommu_dt_device_is_assigned_locked(const struct dt_device_node *dev)
{
    bool assigned = false;

    ASSERT(spin_is_locked(&dtdevs_lock));

    if ( !dt_device_is_protected(dev) )
        return 0;

    assigned = !list_empty(&dev->domain_list);

    return assigned;
}

int iommu_dt_domain_init(struct domain *d)
{
    INIT_LIST_HEAD(&dom_iommu(d)->dt_devices);

    return 0;
}

int iommu_release_dt_devices(struct domain *d)
{
    const struct domain_iommu *hd = dom_iommu(d);
    struct dt_device_node *dev, *_dev;
    int rc;

    if ( !is_iommu_enabled(d) )
        return 0;

    read_lock(&dt_host_lock);

    list_for_each_entry_safe(dev, _dev, &hd->dt_devices, domain_list)
    {
        rc = iommu_deassign_dt_device(d, dev);
        if ( rc )
        {
            dprintk(XENLOG_ERR, "Failed to deassign %s in domain %u\n",
                    dt_node_full_name(dev), d->domain_id);
            read_unlock(&dt_host_lock);

            return rc;
        }
    }

    read_unlock(&dt_host_lock);

    return 0;
}


int iommu_add_dt_device(struct dt_device_node *np)
{
    const struct iommu_ops *ops = iommu_get_ops();
    struct dt_phandle_args iommu_spec;
    struct device *dev = dt_to_dev(np);
    int rc = 1, index = 0;

    ASSERT(system_state < SYS_STATE_active || rw_is_locked(&dt_host_lock));

    if ( !iommu_enabled )
        return 1;

    if ( !ops )
        return -EINVAL;

    /*
     * The device may already have been registered. As there is no harm in
     * it just return success early.
     */
    if ( dev_iommu_fwspec_get(dev) )
        return 0;

    spin_lock(&dtdevs_lock);

    /*
     * According to the Documentation/devicetree/bindings/iommu/iommu.txt
     * from Linux.
     */
    while ( !dt_parse_phandle_with_args(np, "iommus", "#iommu-cells",
                                        index, &iommu_spec) )
    {
        /*
         * The driver which supports generic IOMMU DT bindings must have
         * these callback implemented.
         */
        if ( !ops->add_device || !ops->dt_xlate )
        {
            rc = -EINVAL;
            goto fail;
        }

        if ( !dt_device_is_available(iommu_spec.np) )
            break;

        rc = iommu_fwspec_init(dev, &iommu_spec.np->dev);
        if ( rc )
            break;

        /*
         * Provide DT IOMMU specifier which describes the IOMMU master
         * interfaces of that device (device IDs, etc) to the driver.
         * The driver is responsible to decide how to interpret them.
         */
        rc = ops->dt_xlate(dev, &iommu_spec);
        if ( rc )
            break;

        index++;
    }

    /*
     * Add master device to the IOMMU if latter is present and available.
     * The driver is responsible to mark that device as protected.
     */
    if ( !rc )
        rc = ops->add_device(0, dev);

    if ( rc < 0 )
        iommu_fwspec_free(dev);

 fail:
    spin_unlock(&dtdevs_lock);
    return rc;
}

int iommu_do_dt_domctl(struct xen_domctl *domctl, struct domain *d,
                       XEN_GUEST_HANDLE_PARAM(xen_domctl_t) u_domctl)
{
    int ret;
    struct dt_device_node *dev;

    read_lock(&dt_host_lock);

    switch ( domctl->cmd )
    {
    case XEN_DOMCTL_assign_device:
        ASSERT(d);
        /* fall through */
    case XEN_DOMCTL_test_assign_device:
        ret = -ENODEV;
        if ( domctl->u.assign_device.dev != XEN_DOMCTL_DEV_DT )
            break;

        ret = -EINVAL;
        if ( (d && d->is_dying) || domctl->u.assign_device.flags )
            break;

        ret = dt_find_node_by_gpath(domctl->u.assign_device.u.dt.path,
                                    domctl->u.assign_device.u.dt.size,
                                    &dev);
        if ( ret )
            break;

        ret = xsm_assign_dtdevice(XSM_HOOK, d, dt_node_full_name(dev));
        if ( ret )
            break;

        if ( domctl->cmd == XEN_DOMCTL_test_assign_device )
        {
            spin_lock(&dtdevs_lock);

            if ( iommu_dt_device_is_assigned_locked(dev) )
            {
                printk(XENLOG_G_ERR "%s already assigned.\n",
                       dt_node_full_name(dev));
                ret = -EINVAL;
            }

            spin_unlock(&dtdevs_lock);
            break;
        }

        if ( d == dom_io )
        {
            ret = -EINVAL;
            break;
        }

        ret = iommu_add_dt_device(dev);
        if ( ret < 0 )
        {
            printk(XENLOG_G_ERR "Failed to add %s to the IOMMU\n",
                   dt_node_full_name(dev));
            break;
        }

        ret = iommu_assign_dt_device(d, dev);

        if ( ret )
            printk(XENLOG_G_ERR "XEN_DOMCTL_assign_dt_device: assign \"%s\""
                   " to dom%u failed (%d)\n",
                   dt_node_full_name(dev), d->domain_id, ret);
        break;

    case XEN_DOMCTL_deassign_device:
        ret = -ENODEV;
        if ( domctl->u.assign_device.dev != XEN_DOMCTL_DEV_DT )
            break;

        ret = -EINVAL;
        if ( domctl->u.assign_device.flags )
            break;

        ret = dt_find_node_by_gpath(domctl->u.assign_device.u.dt.path,
                                    domctl->u.assign_device.u.dt.size,
                                    &dev);
        if ( ret )
            break;

        ret = xsm_deassign_dtdevice(XSM_HOOK, d, dt_node_full_name(dev));
        if ( ret )
            break;

        if ( d == dom_io )
        {
            ret = -EINVAL;
            break;
        }

        ret = iommu_deassign_dt_device(d, dev);

        if ( ret )
            printk(XENLOG_G_ERR "XEN_DOMCTL_assign_dt_device: assign \"%s\""
                   " to dom%u failed (%d)\n",
                   dt_node_full_name(dev), d->domain_id, ret);
        break;

    default:
        ret = -ENOSYS;
        break;
    }

    read_unlock(&dt_host_lock);

    return ret;
}

/* === END INLINED: device_tree.c === */
/* === BEGIN INLINED: bootfdt.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Early Device Tree
 *
 * Copyright (C) 2012-2014 Citrix Systems, Inc.
 */
#include <xen_types.h>
#include <xen_lib.h>
#include <xen_kernel.h>
#include <xen_init.h>
#include <xen_efi.h>
#include <xen_device_tree.h>
#include <xen_lib.h>
#include <xen_libfdt_libfdt-xen.h>
#include <xen_sort.h>
#include <xsm_xsm.h>
#include <asm_setup.h>
#include <asm_static-shmem.h>

static void __init __maybe_unused build_assertions(void)
{
    /*
     * Check that no padding is between struct membanks "bank" flexible array
     * member and struct meminfo "bank" member
     */
    BUILD_BUG_ON((offsetof(struct membanks, bank) !=
                 offsetof(struct meminfo, bank)));
    /* Ensure "struct membanks" is 8-byte aligned */
    BUILD_BUG_ON(alignof(struct membanks) != 8);
}

static bool __init device_tree_node_is_available(const void *fdt, int node)
{
    const char *status;
    int len;

    status = fdt_getprop(fdt, node, "status", &len);
    if ( !status )
        return true;

    if ( len > 0 )
    {
        if ( !strcmp(status, "ok") || !strcmp(status, "okay") )
            return true;
    }

    return false;
}

static bool __init device_tree_node_matches(const void *fdt, int node,
                                            const char *match)
{
    const char *name;
    size_t match_len;

    name = fdt_get_name(fdt, node, NULL);
    match_len = strlen(match);

    /* Match both "match" and "match@..." patterns but not
       "match-foo". */
    return strncmp(name, match, match_len) == 0
        && (name[match_len] == '@' || name[match_len] == '\0');
}

static bool __init device_tree_node_compatible(const void *fdt, int node,
                                               const char *match)
{
    int len, l;
    const void *prop;

    prop = fdt_getprop(fdt, node, "compatible", &len);
    if ( prop == NULL )
        return false;

    while ( len > 0 ) {
        if ( !dt_compat_cmp(prop, match) )
            return true;
        l = strlen(prop) + 1;
        prop += l;
        len -= l;
    }

    return false;
}

void __init device_tree_get_reg(const __be32 **cell, uint32_t address_cells,
                                uint32_t size_cells, paddr_t *start,
                                paddr_t *size)
{
    uint64_t dt_start, dt_size;

    /*
     * dt_next_cell will return uint64_t whereas paddr_t may not be 64-bit.
     * Thus, there is an implicit cast from uint64_t to paddr_t.
     */
    dt_start = dt_next_cell(address_cells, cell);
    dt_size = dt_next_cell(size_cells, cell);

    if ( dt_start != (paddr_t)dt_start )
    {
        printk("Physical address greater than max width supported\n");
        WARN();
    }

    if ( dt_size != (paddr_t)dt_size )
    {
        printk("Physical size greater than max width supported\n");
        WARN();
    }

    /*
     * Xen will truncate the address/size if it is greater than the maximum
     * supported width and it will give an appropriate warning.
     */
    *start = dt_start;
    *size = dt_size;
}

static int __init device_tree_get_meminfo(const void *fdt, int node,
                                          const char *prop_name,
                                          u32 address_cells, u32 size_cells,
                                          struct membanks *mem,
                                          enum membank_type type)
{
    const struct fdt_property *prop;
    unsigned int i, banks;
    const __be32 *cell;
    u32 reg_cells = address_cells + size_cells;
    paddr_t start, size;

    if ( !device_tree_node_is_available(fdt, node) )
        return 0;

    if ( address_cells < 1 || size_cells < 1 )
    {
        printk("fdt: property `%s': invalid #address-cells or #size-cells",
               prop_name);
        return -EINVAL;
    }

    prop = fdt_get_property(fdt, node, prop_name, NULL);
    if ( !prop )
        return -ENOENT;

    cell = (const __be32 *)prop->data;
    banks = fdt32_to_cpu(prop->len) / (reg_cells * sizeof (u32));

    for ( i = 0; i < banks && mem->nr_banks < mem->max_banks; i++ )
    {
        device_tree_get_reg(&cell, address_cells, size_cells, &start, &size);
        if ( mem == bootinfo_get_reserved_mem() &&
             check_reserved_regions_overlap(start, size) )
            return -EINVAL;
        /* Some DT may describe empty bank, ignore them */
        if ( !size )
            continue;
        mem->bank[mem->nr_banks].start = start;
        mem->bank[mem->nr_banks].size = size;
        mem->bank[mem->nr_banks].type = type;
        mem->nr_banks++;
    }

    if ( i < banks )
    {
        printk("Warning: Max number of supported memory regions reached.\n");
        return -ENOSPC;
    }

    return 0;
}

u32 __init device_tree_get_u32(const void *fdt, int node,
                               const char *prop_name, u32 dflt)
{
    const struct fdt_property *prop;

    prop = fdt_get_property(fdt, node, prop_name, NULL);
    if ( !prop || prop->len < sizeof(u32) )
        return dflt;

    return fdt32_to_cpu(*(uint32_t*)prop->data);
}

/**
 * device_tree_for_each_node - iterate over all device tree sub-nodes
 * @fdt: flat device tree.
 * @node: parent node to start the search from
 * @func: function to call for each sub-node.
 * @data: data to pass to @func.
 *
 * Any nodes nested at DEVICE_TREE_MAX_DEPTH or deeper are ignored.
 *
 * Returns 0 if all nodes were iterated over successfully.  If @func
 * returns a value different from 0, that value is returned immediately.
 */
int __init device_tree_for_each_node(const void *fdt, int node,
                                     device_tree_node_func func,
                                     void *data)
{
    /*
     * We only care about relative depth increments, assume depth of
     * node is 0 for simplicity.
     */
    int depth = 0;
    const int first_node = node;
    u32 address_cells[DEVICE_TREE_MAX_DEPTH];
    u32 size_cells[DEVICE_TREE_MAX_DEPTH];
    int ret;

    do {
        const char *name = fdt_get_name(fdt, node, NULL);
        u32 as, ss;

        if ( depth >= DEVICE_TREE_MAX_DEPTH )
        {
            printk("Warning: device tree node `%s' is nested too deep\n",
                   name);
            continue;
        }

        as = depth > 0 ? address_cells[depth-1] : DT_ROOT_NODE_ADDR_CELLS_DEFAULT;
        ss = depth > 0 ? size_cells[depth-1] : DT_ROOT_NODE_SIZE_CELLS_DEFAULT;

        address_cells[depth] = device_tree_get_u32(fdt, node,
                                                   "#address-cells", as);
        size_cells[depth] = device_tree_get_u32(fdt, node,
                                                "#size-cells", ss);

        /* skip the first node */
        if ( node != first_node )
        {
            ret = func(fdt, node, name, depth, as, ss, data);
            if ( ret != 0 )
                return ret;
        }

        node = fdt_next_node(fdt, node, &depth);
    } while ( node >= 0 && depth > 0 );

    return 0;
}

static int __init process_memory_node(const void *fdt, int node,
                                      const char *name, int depth,
                                      u32 address_cells, u32 size_cells,
                                      struct membanks *mem)
{
    return device_tree_get_meminfo(fdt, node, "reg", address_cells, size_cells,
                                   mem, MEMBANK_DEFAULT);
}

static int __init process_reserved_memory_node(const void *fdt, int node,
                                               const char *name, int depth,
                                               u32 address_cells,
                                               u32 size_cells,
                                               void *data)
{
    int rc = process_memory_node(fdt, node, name, depth, address_cells,
                                 size_cells, data);

    if ( rc == -ENOSPC )
        panic("Max number of supported reserved-memory regions reached.\n");
    else if ( rc != -ENOENT )
        return rc;
    return 0;
}

static int __init process_reserved_memory(const void *fdt, int node,
                                          const char *name, int depth,
                                          u32 address_cells, u32 size_cells)
{
    return device_tree_for_each_node(fdt, node,
                                     process_reserved_memory_node,
                                     bootinfo_get_reserved_mem());
}

static void __init process_multiboot_node(const void *fdt, int node,
                                          const char *name,
                                          u32 address_cells, u32 size_cells)
{
    static int __initdata kind_guess = 0;
    const struct fdt_property *prop;
    const __be32 *cell;
    bootmodule_kind kind;
    paddr_t start, size;
    int len;
    /* sizeof("/chosen/") + DT_MAX_NAME + '/' + DT_MAX_NAME + '/0' => 92 */
    char path[92];
    int parent_node, ret;
    bool domU;

    parent_node = fdt_parent_offset(fdt, node);
    ASSERT(parent_node >= 0);

    /* Check that the node is under "/chosen" (first 7 chars of path) */
    ret = fdt_get_path(fdt, node, path, sizeof (path));
    if ( ret != 0 || strncmp(path, "/chosen", 7) )
        return;

    prop = fdt_get_property(fdt, node, "reg", &len);
    if ( !prop )
        panic("node %s missing `reg' property\n", name);

    if ( len < dt_cells_to_size(address_cells + size_cells) )
        panic("fdt: node `%s': `reg` property length is too short\n",
                    name);

    cell = (const __be32 *)prop->data;
    device_tree_get_reg(&cell, address_cells, size_cells, &start, &size);

    if ( fdt_node_check_compatible(fdt, node, "xen,linux-zimage") == 0 ||
         fdt_node_check_compatible(fdt, node, "multiboot,kernel") == 0 )
        kind = BOOTMOD_KERNEL;
    else if ( fdt_node_check_compatible(fdt, node, "xen,linux-initrd") == 0 ||
              fdt_node_check_compatible(fdt, node, "multiboot,ramdisk") == 0 )
        kind = BOOTMOD_RAMDISK;
    else if ( fdt_node_check_compatible(fdt, node, "xen,xsm-policy") == 0 )
        kind = BOOTMOD_XSM;
    else if ( fdt_node_check_compatible(fdt, node, "multiboot,device-tree") == 0 )
        kind = BOOTMOD_GUEST_DTB;
    else
        kind = BOOTMOD_UNKNOWN;

    /**
     * Guess the kind of these first two unknowns respectively:
     * (1) The first unknown must be kernel.
     * (2) Detect the XSM Magic from the 2nd unknown:
     *     a. If it's XSM, set the kind as XSM, and that also means we
     *     won't load ramdisk;
     *     b. if it's not XSM, set the kind as ramdisk.
     *     So if user want to load ramdisk, it must be the 2nd unknown.
     * We also detect the XSM Magic for the following unknowns,
     * then set its kind according to the return value of has_xsm_magic.
     */
    if ( kind == BOOTMOD_UNKNOWN )
    {
        switch ( kind_guess++ )
        {
        case 0: kind = BOOTMOD_KERNEL; break;
        case 1: kind = BOOTMOD_RAMDISK; break;
        default: break;
        }
        if ( kind_guess > 1 && has_xsm_magic(start) )
            kind = BOOTMOD_XSM;
    }

    domU = fdt_node_check_compatible(fdt, parent_node, "xen,domain") == 0;
    add_boot_module(kind, start, size, domU);

    prop = fdt_get_property(fdt, node, "bootargs", &len);
    if ( !prop )
        return;
    add_boot_cmdline(fdt_get_name(fdt, parent_node, &len), prop->data,
                     kind, start, domU);
}

static int __init process_chosen_node(const void *fdt, int node,
                                      const char *name,
                                      u32 address_cells, u32 size_cells)
{
    const struct fdt_property *prop;
    paddr_t start, end;
    int len;

    if ( fdt_get_property(fdt, node, "xen,static-heap", NULL) )
    {
        int rc;

        printk("Checking for static heap in /chosen\n");

        rc = device_tree_get_meminfo(fdt, node, "xen,static-heap",
                                     address_cells, size_cells,
                                     bootinfo_get_reserved_mem(),
                                     MEMBANK_STATIC_HEAP);
        if ( rc )
            return rc;

        bootinfo.static_heap = true;
    }

    printk("Checking for initrd in /chosen\n");

    prop = fdt_get_property(fdt, node, "linux,initrd-start", &len);
    if ( !prop )
        /* No initrd present. */
        return 0;
    if ( len != sizeof(u32) && len != sizeof(u64) )
    {
        printk("linux,initrd-start property has invalid length %d\n", len);
        return -EINVAL;
    }
    start = dt_read_paddr((const void *)&prop->data, dt_size_to_cells(len));

    prop = fdt_get_property(fdt, node, "linux,initrd-end", &len);
    if ( !prop )
    {
        printk("linux,initrd-end not present but -start was\n");
        return -EINVAL;
    }
    if ( len != sizeof(u32) && len != sizeof(u64) )
    {
        printk("linux,initrd-end property has invalid length %d\n", len);
        return -EINVAL;
    }
    end = dt_read_paddr((const void *)&prop->data, dt_size_to_cells(len));

    if ( start >= end )
    {
        printk("linux,initrd limits invalid: %"PRIpaddr" >= %"PRIpaddr"\n",
                  start, end);
        return -EINVAL;
    }

    printk("Initrd %"PRIpaddr"-%"PRIpaddr"\n", start, end);

    add_boot_module(BOOTMOD_RAMDISK, start, end-start, false);

    return 0;
}

static int __init process_domain_node(const void *fdt, int node,
                                      const char *name,
                                      u32 address_cells, u32 size_cells)
{
    const struct fdt_property *prop;

    printk("Checking for \"xen,static-mem\" in domain node\n");

    prop = fdt_get_property(fdt, node, "xen,static-mem", NULL);
    if ( !prop )
        /* No "xen,static-mem" present. */
        return 0;

    return device_tree_get_meminfo(fdt, node, "xen,static-mem", address_cells,
                                   size_cells, bootinfo_get_reserved_mem(),
                                   MEMBANK_STATIC_DOMAIN);
}

static int __init early_scan_node(const void *fdt,
                                  int node, const char *name, int depth,
                                  u32 address_cells, u32 size_cells,
                                  void *data)
{
    int rc = 0;

    /*
     * If Xen has been booted via UEFI, the memory banks are
     * populated. So we should skip the parsing.
     */
    if ( !efi_enabled(EFI_BOOT) &&
         device_tree_node_matches(fdt, node, "memory") )
        rc = process_memory_node(fdt, node, name, depth,
                                 address_cells, size_cells, bootinfo_get_mem());
    else if ( depth == 1 && !dt_node_cmp(name, "reserved-memory") )
        rc = process_reserved_memory(fdt, node, name, depth,
                                     address_cells, size_cells);
    else if ( depth <= 3 && (device_tree_node_compatible(fdt, node, "xen,multiboot-module" ) ||
              device_tree_node_compatible(fdt, node, "multiboot,module" )))
        process_multiboot_node(fdt, node, name, address_cells, size_cells);
    else if ( depth == 1 && device_tree_node_matches(fdt, node, "chosen") )
        rc = process_chosen_node(fdt, node, name, address_cells, size_cells);
    else if ( depth == 2 && device_tree_node_compatible(fdt, node, "xen,domain") )
        rc = process_domain_node(fdt, node, name, address_cells, size_cells);
    else if ( depth <= 3 && device_tree_node_compatible(fdt, node, "xen,domain-shared-memory-v1") )
        rc = process_shm_node(fdt, node, address_cells, size_cells);

    if ( rc < 0 )
        printk("fdt: node `%s': parsing failed\n", name);
    return rc;
}

static void __init early_print_info(void)
{
    const struct membanks *mi = bootinfo_get_mem();
    const struct membanks *mem_resv = bootinfo_get_reserved_mem();
    struct bootmodules *mods = &bootinfo.modules;
    struct bootcmdlines *cmds = &bootinfo.cmdlines;
    unsigned int i;

    for ( i = 0; i < mi->nr_banks; i++ )
        printk("RAM: %"PRIpaddr" - %"PRIpaddr"\n",
                mi->bank[i].start,
                mi->bank[i].start + mi->bank[i].size - 1);
    printk("\n");
    for ( i = 0 ; i < mods->nr_mods; i++ )
        printk("MODULE[%d]: %"PRIpaddr" - %"PRIpaddr" %-12s\n",
                i,
                mods->module[i].start,
                mods->module[i].start + mods->module[i].size,
                boot_module_kind_as_string(mods->module[i].kind));

    for ( i = 0; i < mem_resv->nr_banks; i++ )
    {
        printk(" RESVD[%u]: %"PRIpaddr" - %"PRIpaddr"\n", i,
               mem_resv->bank[i].start,
               mem_resv->bank[i].start + mem_resv->bank[i].size - 1);
    }
    early_print_info_shmem();
    printk("\n");
    for ( i = 0 ; i < cmds->nr_mods; i++ )
        printk("CMDLINE[%"PRIpaddr"]:%s %s\n", cmds->cmdline[i].start,
               cmds->cmdline[i].dt_name,
               &cmds->cmdline[i].cmdline[0]);
    printk("\n");
}

/* This function assumes that memory regions are not overlapped */
static int __init cmp_memory_node(const void *key, const void *elem)
{
    const struct membank *handler0 = key;
    const struct membank *handler1 = elem;

    if ( handler0->start < handler1->start )
        return -1;

    if ( handler0->start >= (handler1->start + handler1->size) )
        return 1;

    return 0;
}

static void __init swap_memory_node(void *_a, void *_b, size_t size)
{
    struct membank *a = _a, *b = _b;

    SWAP(*a, *b);
}

/**
 * boot_fdt_info - initialize bootinfo from a DTB
 * @fdt: flattened device tree binary
 *
 * Returns the size of the DTB.
 */
size_t __init boot_fdt_info(const void *fdt, paddr_t paddr)
{
    struct membanks *reserved_mem = bootinfo_get_reserved_mem();
    struct membanks *mem = bootinfo_get_mem();
    unsigned int i;
    int nr_rsvd;
    int ret;

    ret = fdt_check_header(fdt);
    if ( ret < 0 )
        panic("No valid device tree\n");

    add_boot_module(BOOTMOD_FDT, paddr, fdt_totalsize(fdt), false);

    nr_rsvd = fdt_num_mem_rsv(fdt);
    if ( nr_rsvd < 0 )
        panic("Parsing FDT memory reserve map failed (%d)\n", nr_rsvd);

    for ( i = 0; i < nr_rsvd; i++ )
    {
        struct membank *bank;
        paddr_t s, sz;

        if ( fdt_get_mem_rsv_paddr(device_tree_flattened, i, &s, &sz) < 0 )
            continue;

        if ( reserved_mem->nr_banks < reserved_mem->max_banks )
        {
            bank = &reserved_mem->bank[reserved_mem->nr_banks];
            bank->start = s;
            bank->size = sz;
            bank->type = MEMBANK_FDT_RESVMEM;
            reserved_mem->nr_banks++;
        }
        else
            panic("Cannot allocate reserved memory bank\n");
    }

    ret = device_tree_for_each_node(fdt, 0, early_scan_node, NULL);
    if ( ret )
        panic("Early FDT parsing failed (%d)\n", ret);

    /*
     * On Arm64 setup_directmap_mappings() expects to be called with the lowest
     * bank in memory first. There is no requirement that the DT will provide
     * the banks sorted in ascending order. So sort them through.
     */
    sort(mem->bank, mem->nr_banks, sizeof(struct membank),
         cmp_memory_node, swap_memory_node);

    early_print_info();

    return fdt_totalsize(fdt);
}

const __init char *boot_fdt_cmdline(const void *fdt)
{
    int node;
    const struct fdt_property *prop;

    node = fdt_path_offset(fdt, "/chosen");
    if ( node < 0 )
        return NULL;

    prop = fdt_get_property(fdt, node, "xen,xen-bootargs", NULL);
    if ( prop == NULL )
    {
        struct bootcmdline *dom0_cmdline =
            boot_cmdline_find_by_kind(BOOTMOD_KERNEL);

        if (fdt_get_property(fdt, node, "xen,dom0-bootargs", NULL) ||
            ( dom0_cmdline && dom0_cmdline->cmdline[0] ) )
            prop = fdt_get_property(fdt, node, "bootargs", NULL);
    }
    if ( prop == NULL )
        return NULL;

    return prop->data;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: bootfdt.c === */
/* === BEGIN INLINED: device.c === */
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * xen/arch/arm/device.c
 *
 * Helpers to use a device retrieved via the device tree.
 *
 * Julien Grall <julien.grall@linaro.org>
 * Copyright (C) 2013 Linaro Limited.
 */

#include <asm_generic_device.h>
#include <asm_setup.h>
#include <xen_errno.h>
#include <xen_init.h>
#include <xen_iocap.h>
#include <xen_lib.h>

extern const struct device_desc _sdevice[], _edevice[];

#ifdef CONFIG_ACPI
extern const struct acpi_device_desc _asdevice[], _aedevice[];
#endif

int __init device_init(struct dt_device_node *dev, enum device_class class,
                       const void *data)
{
    const struct device_desc *desc;

    ASSERT(dev != NULL);

    if ( !dt_device_is_available(dev) || dt_device_for_passthrough(dev) )
        return  -ENODEV;

    for ( desc = _sdevice; desc != _edevice; desc++ )
    {
        if ( desc->class != class )
            continue;

        if ( dt_match_node(desc->dt_match, dev) )
        {
            ASSERT(desc->init != NULL);

            return desc->init(dev, data);
        }

    }

    return -EBADF;
}

#ifdef CONFIG_ACPI
int __init acpi_device_init(enum device_class class, const void *data, int class_type)
{
    const struct acpi_device_desc *desc;

    for ( desc = _asdevice; desc != _aedevice; desc++ )
    {
        if ( ( desc->class != class ) || ( desc->class_type != class_type ) )
            continue;

        ASSERT(desc->init != NULL);

        return desc->init(data);
    }

    return -EBADF;
}
#endif

enum device_class device_get_class(const struct dt_device_node *dev)
{
    const struct device_desc *desc;

    ASSERT(dev != NULL);

    for ( desc = _sdevice; desc != _edevice; desc++ )
    {
        if ( dt_match_node(desc->dt_match, dev) )
            return desc->class;
    }

    return DEVICE_UNKNOWN;
}

int map_irq_to_domain(struct domain *d, unsigned int irq,
                      bool need_mapping, const char *devname)
{
    int res;

    res = irq_permit_access(d, irq);
    if ( res )
    {
        printk(XENLOG_ERR "Unable to permit to %pd access to IRQ %u\n", d, irq);
        return res;
    }

    if ( need_mapping )
    {
        /*
         * Checking the return of vgic_reserve_virq is not
         * necessary. It should not fail except when we try to map
         * the IRQ twice. This can legitimately happen if the IRQ is shared
         */
        vgic_reserve_virq(d, irq);

        res = route_irq_to_guest(d, irq, irq, devname);
        if ( res < 0 )
        {
            printk(XENLOG_ERR "Unable to map IRQ%u to %pd\n", irq, d);
            return res;
        }
    }

    dt_dprintk("  - IRQ: %u\n", irq);
    return 0;
}

int map_range_to_domain(const struct dt_device_node *dev,
                        uint64_t addr, uint64_t len, void *data)
{
    struct map_range_data *mr_data = data;
    struct domain *d = mr_data->d;
    int res;

    if ( (addr != (paddr_t)addr) || (((paddr_t)~0 - addr) < len) )
    {
        printk(XENLOG_ERR "%s: [0x%"PRIx64", 0x%"PRIx64"] exceeds the maximum allowed PA width (%u bits)",
               dt_node_full_name(dev), addr, (addr + len), PADDR_BITS);
        return -ERANGE;
    }

    /*
     * reserved-memory regions are RAM carved out for a special purpose.
     * They are not MMIO and therefore a domain should not be able to
     * manage them via the IOMEM interface.
     */
    if ( strncasecmp(dt_node_full_name(dev), "/reserved-memory/",
                     strlen("/reserved-memory/")) != 0 )
    {
        res = iomem_permit_access(d, paddr_to_pfn(addr),
                paddr_to_pfn(PAGE_ALIGN(addr + len - 1)));
        if ( res )
        {
            printk(XENLOG_ERR "Unable to permit to dom%d access to"
                    " 0x%"PRIx64" - 0x%"PRIx64"\n",
                    d->domain_id,
                    addr & PAGE_MASK, PAGE_ALIGN(addr + len) - 1);
            return res;
        }
    }

    if ( !mr_data->skip_mapping )
    {
        res = map_regions_p2mt(d,
                               gaddr_to_gfn(addr),
                               PFN_UP(len),
                               maddr_to_mfn(addr),
                               mr_data->p2mt);

        if ( res < 0 )
        {
            printk(XENLOG_ERR "Unable to map 0x%"PRIx64
                   " - 0x%"PRIx64" in domain %d\n",
                   addr & PAGE_MASK, PAGE_ALIGN(addr + len) - 1,
                   d->domain_id);
            return res;
        }
    }

    dt_dprintk("  - MMIO: %010"PRIx64" - %010"PRIx64" P2MType=%x\n",
               addr, addr + len, mr_data->p2mt);

    if ( mr_data->iomem_ranges )
    {
        res = rangeset_add_range(mr_data->iomem_ranges,
                                 paddr_to_pfn(addr),
                                 paddr_to_pfn_aligned(addr + len - 1));
        if ( res )
            return res;
    }

    return 0;
}

/*
 * map_device_irqs_to_domain retrieves the interrupts configuration from
 * a device tree node and maps those interrupts to the target domain.
 *
 * Returns:
 *   < 0 error
 *   0   success
 */
int map_device_irqs_to_domain(struct domain *d,
                              struct dt_device_node *dev,
                              bool need_mapping,
                              struct rangeset *irq_ranges)
{
    unsigned int i, nirq;
    int res, irq;
    struct dt_raw_irq rirq;

    nirq = dt_number_of_irq(dev);

    /* Give permission and map IRQs */
    for ( i = 0; i < nirq; i++ )
    {
        res = dt_device_get_raw_irq(dev, i, &rirq);
        if ( res )
        {
            printk(XENLOG_ERR "Unable to retrieve irq %u for %s\n",
                   i, dt_node_full_name(dev));
            return res;
        }

        /*
         * Don't map IRQ that have no physical meaning
         * ie: IRQ whose controller is not the GIC
         */
        if ( rirq.controller != dt_interrupt_controller )
        {
            dt_dprintk("irq %u not connected to primary controller. Connected to %s\n",
                      i, dt_node_full_name(rirq.controller));
            continue;
        }

        irq = platform_get_irq(dev, i);
        if ( irq < 0 )
        {
            printk(XENLOG_ERR "Unable to get irq %u for %s\n",
                   i, dt_node_full_name(dev));
            return irq;
        }

        res = map_irq_to_domain(d, irq, need_mapping, dt_node_name(dev));
        if ( res )
            return res;

        if ( irq_ranges )
        {
            res = rangeset_add_singleton(irq_ranges, irq);
            if ( res )
                return res;
        }
    }

    return 0;
}

static int map_dt_irq_to_domain(const struct dt_device_node *dev,
                                const struct dt_irq *dt_irq,
                                void *data)
{
    struct map_range_data *mr_data = data;
    struct domain *d = mr_data->d;
    unsigned int irq = dt_irq->irq;
    int res;

    if ( irq < NR_LOCAL_IRQS )
    {
        printk(XENLOG_ERR "%s: IRQ%u is not a SPI\n", dt_node_name(dev), irq);
        return -EINVAL;
    }

    /* Setup the IRQ type */
    res = irq_set_spi_type(irq, dt_irq->type);
    if ( res )
    {
        printk(XENLOG_ERR "%s: Unable to setup IRQ%u to %pd\n",
               dt_node_name(dev), irq, d);
        return res;
    }

    res = map_irq_to_domain(d, irq, !mr_data->skip_mapping, dt_node_name(dev));
    if ( res )
        return res;

    if ( mr_data->irq_ranges )
        res = rangeset_add_singleton(mr_data->irq_ranges, irq);

    return res;
}

/*
 * For a node which describes a discoverable bus (such as a PCI bus)
 * then we may need to perform additional mappings in order to make
 * the child resources available to domain 0.
 */
static int map_device_children(const struct dt_device_node *dev,
                               struct map_range_data *mr_data)
{
    if ( dt_device_type_is_equal(dev, "pci") )
    {
        int ret;

        dt_dprintk("Mapping children of %s to guest\n",
                   dt_node_full_name(dev));

        ret = dt_for_each_irq_map(dev, &map_dt_irq_to_domain, mr_data);
        if ( ret < 0 )
            return ret;

        ret = dt_for_each_range(dev, &map_range_to_domain, mr_data);
        if ( ret < 0 )
            return ret;
    }

    return 0;
}

/*
 * For a given device node:
 *  - Give permission to the guest to manage IRQ and MMIO range
 *  - Retrieve the IRQ configuration (i.e edge/level) from device tree
 * When the device is not marked for guest passthrough:
 *  - Try to call iommu_add_dt_device to protect the device by an IOMMU
 *  - Assign the device to the guest if it's protected by an IOMMU
 *  - Map the IRQs and iomem regions to DOM0
 */
int handle_device(struct domain *d, struct dt_device_node *dev, p2m_type_t p2mt,
                  struct rangeset *iomem_ranges, struct rangeset *irq_ranges)
{
    unsigned int naddr;
    unsigned int i;
    int res;
    paddr_t addr, size;
    bool own_device = !dt_device_for_passthrough(dev);
    /*
     * We want to avoid mapping the MMIO in dom0 for the following cases:
     *   - The device is owned by dom0 (i.e. it has been flagged for
     *     passthrough).
     *   - PCI host bridges with driver in Xen. They will later be mapped by
     *     pci_host_bridge_mappings().
     */
    struct map_range_data mr_data = {
        .d = d,
        .p2mt = p2mt,
        .skip_mapping = !own_device ||
                        (is_pci_passthrough_enabled() &&
                        (device_get_class(dev) == DEVICE_PCI_HOSTBRIDGE)),
        .iomem_ranges = iomem_ranges,
        .irq_ranges = irq_ranges
    };

    naddr = dt_number_of_address(dev);

    dt_dprintk("%s passthrough = %d naddr = %u\n",
               dt_node_full_name(dev), own_device, naddr);

    if ( own_device )
    {
        dt_dprintk("Check if %s is behind the IOMMU and add it\n",
                   dt_node_full_name(dev));

        res = iommu_add_dt_device(dev);
        if ( res < 0 )
        {
            printk(XENLOG_ERR "Failed to add %s to the IOMMU\n",
                   dt_node_full_name(dev));
            return res;
        }

        if ( dt_device_is_protected(dev) )
        {
            dt_dprintk("%s setup iommu\n", dt_node_full_name(dev));
            res = iommu_assign_dt_device(d, dev);
            if ( res )
            {
                printk(XENLOG_ERR "Failed to setup the IOMMU for %s\n",
                       dt_node_full_name(dev));
                return res;
            }
        }
    }

    res = map_device_irqs_to_domain(d, dev, own_device, irq_ranges);
    if ( res < 0 )
        return res;

    /* Give permission and map MMIOs */
    for ( i = 0; i < naddr; i++ )
    {
        res = dt_device_get_paddr(dev, i, &addr, &size);
        if ( res )
        {
            printk(XENLOG_ERR "Unable to retrieve address %u for %s\n",
                   i, dt_node_full_name(dev));
            return res;
        }

        res = map_range_to_domain(dev, addr, size, &mr_data);
        if ( res )
            return res;
    }

    res = map_device_children(dev, &mr_data);
    if ( res )
        return res;

    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: device.c === */
/* === BEGIN INLINED: fdt.c === */
#include <xen_xen_config.h>
// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 */
#include "libfdt_env.h"

#include <xen_libfdt_fdt.h>
#include <xen_libfdt_libfdt.h>

#include "libfdt_internal.h"

/*
 * Minimal sanity check for a read-only tree. fdt_ro_probe_() checks
 * that the given buffer contains what appears to be a flattened
 * device tree with sane information in its header.
 */
int32_t fdt_ro_probe_(const void *fdt)
{
	uint32_t totalsize = fdt_totalsize(fdt);

	if (can_assume(VALID_DTB))
		return totalsize;

	/* The device tree must be at an 8-byte aligned address */
	if ((uintptr_t)fdt & 7)
		return -FDT_ERR_ALIGNMENT;

	if (fdt_magic(fdt) == FDT_MAGIC) {
		/* Complete tree */
		if (!can_assume(LATEST)) {
			if (fdt_version(fdt) < FDT_FIRST_SUPPORTED_VERSION)
				return -FDT_ERR_BADVERSION;
			if (fdt_last_comp_version(fdt) >
					FDT_LAST_SUPPORTED_VERSION)
				return -FDT_ERR_BADVERSION;
		}
	} else if (fdt_magic(fdt) == FDT_SW_MAGIC) {
		/* Unfinished sequential-write blob */
		if (!can_assume(VALID_INPUT) && fdt_size_dt_struct(fdt) == 0)
			return -FDT_ERR_BADSTATE;
	} else {
		return -FDT_ERR_BADMAGIC;
	}

	if (totalsize < INT32_MAX)
		return totalsize;
	else
		return -FDT_ERR_TRUNCATED;
}

static int check_off_(uint32_t hdrsize, uint32_t totalsize, uint32_t off)
{
	return (off >= hdrsize) && (off <= totalsize);
}

static int check_block_(uint32_t hdrsize, uint32_t totalsize,
			uint32_t base, uint32_t size)
{
	if (!check_off_(hdrsize, totalsize, base))
		return 0; /* block start out of bounds */
	if ((base + size) < base)
		return 0; /* overflow */
	if (!check_off_(hdrsize, totalsize, base + size))
		return 0; /* block end out of bounds */
	return 1;
}

size_t fdt_header_size_(uint32_t version)
{
	if (version <= 1)
		return FDT_V1_SIZE;
	else if (version <= 2)
		return FDT_V2_SIZE;
	else if (version <= 3)
		return FDT_V3_SIZE;
	else if (version <= 16)
		return FDT_V16_SIZE;
	else
		return FDT_V17_SIZE;
}

size_t fdt_header_size(const void *fdt)
{
	return can_assume(LATEST) ? FDT_V17_SIZE :
		fdt_header_size_(fdt_version(fdt));
}

int fdt_check_header(const void *fdt)
{
	size_t hdrsize;

	/* The device tree must be at an 8-byte aligned address */
	if ((uintptr_t)fdt & 7)
		return -FDT_ERR_ALIGNMENT;

	if (fdt_magic(fdt) != FDT_MAGIC)
		return -FDT_ERR_BADMAGIC;
	if (!can_assume(LATEST)) {
		if ((fdt_version(fdt) < FDT_FIRST_SUPPORTED_VERSION)
		    || (fdt_last_comp_version(fdt) >
			FDT_LAST_SUPPORTED_VERSION))
			return -FDT_ERR_BADVERSION;
		if (fdt_version(fdt) < fdt_last_comp_version(fdt))
			return -FDT_ERR_BADVERSION;
	}
	hdrsize = fdt_header_size(fdt);
	if (!can_assume(VALID_DTB)) {

		if ((fdt_totalsize(fdt) < hdrsize)
		    || (fdt_totalsize(fdt) > INT_MAX))
			return -FDT_ERR_TRUNCATED;

		/* Bounds check memrsv block */
		if (!check_off_(hdrsize, fdt_totalsize(fdt),
				fdt_off_mem_rsvmap(fdt)))
			return -FDT_ERR_TRUNCATED;
	}

	if (!can_assume(VALID_DTB)) {
		/* Bounds check structure block */
		if (!can_assume(LATEST) && fdt_version(fdt) < 17) {
			if (!check_off_(hdrsize, fdt_totalsize(fdt),
					fdt_off_dt_struct(fdt)))
				return -FDT_ERR_TRUNCATED;
		} else {
			if (!check_block_(hdrsize, fdt_totalsize(fdt),
					  fdt_off_dt_struct(fdt),
					  fdt_size_dt_struct(fdt)))
				return -FDT_ERR_TRUNCATED;
		}

		/* Bounds check strings block */
		if (!check_block_(hdrsize, fdt_totalsize(fdt),
				  fdt_off_dt_strings(fdt),
				  fdt_size_dt_strings(fdt)))
			return -FDT_ERR_TRUNCATED;
	}

	return 0;
}

const void *fdt_offset_ptr(const void *fdt, int offset, unsigned int len)
{
	unsigned int uoffset = offset;
	unsigned int absoffset = offset + fdt_off_dt_struct(fdt);

	if (offset < 0)
		return NULL;

	if (!can_assume(VALID_INPUT))
		if ((absoffset < uoffset)
		    || ((absoffset + len) < absoffset)
		    || (absoffset + len) > fdt_totalsize(fdt))
			return NULL;

	if (can_assume(LATEST) || fdt_version(fdt) >= 0x11)
		if (((uoffset + len) < uoffset)
		    || ((offset + len) > fdt_size_dt_struct(fdt)))
			return NULL;

	return fdt_offset_ptr_(fdt, offset);
}

uint32_t fdt_next_tag(const void *fdt, int startoffset, int *nextoffset)
{
	const fdt32_t *tagp, *lenp;
	uint32_t tag;
	int offset = startoffset;
	const char *p;

	*nextoffset = -FDT_ERR_TRUNCATED;
	tagp = fdt_offset_ptr(fdt, offset, FDT_TAGSIZE);
	if (!can_assume(VALID_DTB) && !tagp)
		return FDT_END; /* premature end */
	tag = fdt32_to_cpu(*tagp);
	offset += FDT_TAGSIZE;

	*nextoffset = -FDT_ERR_BADSTRUCTURE;
	switch (tag) {
	case FDT_BEGIN_NODE:
		/* skip name */
		do {
			p = fdt_offset_ptr(fdt, offset++, 1);
		} while (p && (*p != '\0'));
		if (!can_assume(VALID_DTB) && !p)
			return FDT_END; /* premature end */
		break;

	case FDT_PROP:
		lenp = fdt_offset_ptr(fdt, offset, sizeof(*lenp));
		if (!can_assume(VALID_DTB) && !lenp)
			return FDT_END; /* premature end */
		/* skip-name offset, length and value */
		offset += sizeof(struct fdt_property) - FDT_TAGSIZE
			+ fdt32_to_cpu(*lenp);
		if (!can_assume(LATEST) &&
		    fdt_version(fdt) < 0x10 && fdt32_to_cpu(*lenp) >= 8 &&
		    ((offset - fdt32_to_cpu(*lenp)) % 8) != 0)
			offset += 4;
		break;

	case FDT_END:
	case FDT_END_NODE:
	case FDT_NOP:
		break;

	default:
		return FDT_END;
	}

	if (!fdt_offset_ptr(fdt, startoffset, offset - startoffset))
		return FDT_END; /* premature end */

	*nextoffset = FDT_TAGALIGN(offset);
	return tag;
}

int fdt_check_node_offset_(const void *fdt, int offset)
{
	if (!can_assume(VALID_INPUT)
	    && ((offset < 0) || (offset % FDT_TAGSIZE)))
		return -FDT_ERR_BADOFFSET;

	if (fdt_next_tag(fdt, offset, &offset) != FDT_BEGIN_NODE)
		return -FDT_ERR_BADOFFSET;

	return offset;
}

int fdt_check_prop_offset_(const void *fdt, int offset)
{
	if (!can_assume(VALID_INPUT)
	    && ((offset < 0) || (offset % FDT_TAGSIZE)))
		return -FDT_ERR_BADOFFSET;

	if (fdt_next_tag(fdt, offset, &offset) != FDT_PROP)
		return -FDT_ERR_BADOFFSET;

	return offset;
}

int fdt_next_node(const void *fdt, int offset, int *depth)
{
	int nextoffset = 0;
	uint32_t tag;

	if (offset >= 0)
		if ((nextoffset = fdt_check_node_offset_(fdt, offset)) < 0)
			return nextoffset;

	do {
		offset = nextoffset;
		tag = fdt_next_tag(fdt, offset, &nextoffset);

		switch (tag) {
		case FDT_PROP:
		case FDT_NOP:
			break;

		case FDT_BEGIN_NODE:
			if (depth)
				(*depth)++;
			break;

		case FDT_END_NODE:
			if (depth && ((--(*depth)) < 0))
				return nextoffset;
			break;

		case FDT_END:
			if ((nextoffset >= 0)
			    || ((nextoffset == -FDT_ERR_TRUNCATED) && !depth))
				return -FDT_ERR_NOTFOUND;
			else
				return nextoffset;
		}
	} while (tag != FDT_BEGIN_NODE);

	return offset;
}

int fdt_first_subnode(const void *fdt, int offset)
{
	int depth = 0;

	offset = fdt_next_node(fdt, offset, &depth);
	if (offset < 0 || depth != 1)
		return -FDT_ERR_NOTFOUND;

	return offset;
}

int fdt_next_subnode(const void *fdt, int offset)
{
	int depth = 1;

	/*
	 * With respect to the parent, the depth of the next subnode will be
	 * the same as the last.
	 */
	do {
		offset = fdt_next_node(fdt, offset, &depth);
		if (offset < 0 || depth < 1)
			return -FDT_ERR_NOTFOUND;
	} while (depth > 1);

	return offset;
}

const char *fdt_find_string_(const char *strtab, int tabsize, const char *s)
{
	int len = strlen(s) + 1;
	const char *last = strtab + tabsize - len;
	const char *p;

	for (p = strtab; p <= last; p++)
		if (memcmp(p, s, len) == 0)
			return p;
	return NULL;
}

int fdt_move(const void *fdt, void *buf, int bufsize)
{
	if (!can_assume(VALID_INPUT) && bufsize < 0)
		return -FDT_ERR_NOSPACE;

	FDT_RO_PROBE(fdt);

	if (fdt_totalsize(fdt) > (unsigned int)bufsize)
		return -FDT_ERR_NOSPACE;

	memmove(buf, fdt, fdt_totalsize(fdt));
	return 0;
}

/* === END INLINED: fdt.c === */
/* === BEGIN INLINED: fdt_ro.c === */
#include <xen_xen_config.h>
// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 */
#include "libfdt_env.h"

#include <xen_libfdt_fdt.h>
#include <xen_libfdt_libfdt.h>

#include "libfdt_internal.h"

static int fdt_nodename_eq_(const void *fdt, int offset,
			    const char *s, int len)
{
	int olen;
	const char *p = fdt_get_name(fdt, offset, &olen);

	if (!p || olen < len)
		/* short match */
		return 0;

	if (memcmp(p, s, len) != 0)
		return 0;

	if (p[len] == '\0')
		return 1;
	else if (!memchr(s, '@', len) && (p[len] == '@'))
		return 1;
	else
		return 0;
}

const char *fdt_get_string(const void *fdt, int stroffset, int *lenp)
{
	int32_t totalsize;
	uint32_t absoffset;
	size_t len;
	int err;
	const char *s, *n;

	if (can_assume(VALID_INPUT)) {
		s = (const char *)fdt + fdt_off_dt_strings(fdt) + stroffset;

		if (lenp)
			*lenp = strlen(s);
		return s;
	}
	totalsize = fdt_ro_probe_(fdt);
	err = totalsize;
	if (totalsize < 0)
		goto fail;

	err = -FDT_ERR_BADOFFSET;
	absoffset = stroffset + fdt_off_dt_strings(fdt);
	if (absoffset >= (unsigned)totalsize)
		goto fail;
	len = totalsize - absoffset;

	if (fdt_magic(fdt) == FDT_MAGIC) {
		if (stroffset < 0)
			goto fail;
		if (can_assume(LATEST) || fdt_version(fdt) >= 17) {
			if ((unsigned)stroffset >= fdt_size_dt_strings(fdt))
				goto fail;
			if ((fdt_size_dt_strings(fdt) - stroffset) < len)
				len = fdt_size_dt_strings(fdt) - stroffset;
		}
	} else if (fdt_magic(fdt) == FDT_SW_MAGIC) {
		unsigned int sw_stroffset = -stroffset;

		if ((stroffset >= 0) ||
		    (sw_stroffset > fdt_size_dt_strings(fdt)))
			goto fail;
		if (sw_stroffset < len)
			len = sw_stroffset;
	} else {
		err = -FDT_ERR_INTERNAL;
		goto fail;
	}

	s = (const char *)fdt + absoffset;
	n = memchr(s, '\0', len);
	if (!n) {
		/* missing terminating NULL */
		err = -FDT_ERR_TRUNCATED;
		goto fail;
	}

	if (lenp)
		*lenp = n - s;
	return s;

fail:
	if (lenp)
		*lenp = err;
	return NULL;
}

const char *fdt_string(const void *fdt, int stroffset)
{
	return fdt_get_string(fdt, stroffset, NULL);
}

static int fdt_string_eq_(const void *fdt, int stroffset,
			  const char *s, int len)
{
	int slen;
	const char *p = fdt_get_string(fdt, stroffset, &slen);

	return p && (slen == len) && (memcmp(p, s, len) == 0);
}

int fdt_find_max_phandle(const void *fdt, uint32_t *phandle)
{
	uint32_t max = 0;
	int offset = -1;

	while (true) {
		uint32_t value;

		offset = fdt_next_node(fdt, offset, NULL);
		if (offset < 0) {
			if (offset == -FDT_ERR_NOTFOUND)
				break;

			return offset;
		}

		value = fdt_get_phandle(fdt, offset);

		if (value > max)
			max = value;
	}

	if (phandle)
		*phandle = max;

	return 0;
}


static const struct fdt_reserve_entry *fdt_mem_rsv(const void *fdt, int n)
{
	unsigned int offset = n * sizeof(struct fdt_reserve_entry);
	unsigned int absoffset = fdt_off_mem_rsvmap(fdt) + offset;

	if (!can_assume(VALID_INPUT)) {
		if (absoffset < fdt_off_mem_rsvmap(fdt))
			return NULL;
		if (absoffset > fdt_totalsize(fdt) -
		    sizeof(struct fdt_reserve_entry))
			return NULL;
	}
	return fdt_mem_rsv_(fdt, n);
}

int fdt_get_mem_rsv(const void *fdt, int n, uint64_t *address, uint64_t *size)
{
	const struct fdt_reserve_entry *re;

	FDT_RO_PROBE(fdt);
	re = fdt_mem_rsv(fdt, n);
	if (!can_assume(VALID_INPUT) && !re)
		return -FDT_ERR_BADOFFSET;

	*address = fdt64_ld_(&re->address);
	*size = fdt64_ld_(&re->size);
	return 0;
}

int fdt_num_mem_rsv(const void *fdt)
{
	int i;
	const struct fdt_reserve_entry *re;

	for (i = 0; (re = fdt_mem_rsv(fdt, i)) != NULL; i++) {
		if (fdt64_ld_(&re->size) == 0)
			return i;
	}
	return -FDT_ERR_TRUNCATED;
}

static int nextprop_(const void *fdt, int offset)
{
	uint32_t tag;
	int nextoffset;

	do {
		tag = fdt_next_tag(fdt, offset, &nextoffset);

		switch (tag) {
		case FDT_END:
			if (nextoffset >= 0)
				return -FDT_ERR_BADSTRUCTURE;
			else
				return nextoffset;

		case FDT_PROP:
			return offset;
		}
		offset = nextoffset;
	} while (tag == FDT_NOP);

	return -FDT_ERR_NOTFOUND;
}

int fdt_subnode_offset_namelen(const void *fdt, int offset,
			       const char *name, int namelen)
{
	int depth;

	FDT_RO_PROBE(fdt);

	for (depth = 0;
	     (offset >= 0) && (depth >= 0);
	     offset = fdt_next_node(fdt, offset, &depth))
		if ((depth == 1)
		    && fdt_nodename_eq_(fdt, offset, name, namelen))
			return offset;

	if (depth < 0)
		return -FDT_ERR_NOTFOUND;
	return offset; /* error */
}


int fdt_path_offset_namelen(const void *fdt, const char *path, int namelen)
{
	const char *end = path + namelen;
	const char *p = path;
	int offset = 0;

	FDT_RO_PROBE(fdt);

	/* see if we have an alias */
	if (*path != '/') {
		const char *q = memchr(path, '/', end - p);

		if (!q)
			q = end;

		p = fdt_get_alias_namelen(fdt, p, q - p);
		if (!p)
			return -FDT_ERR_BADPATH;
		offset = fdt_path_offset(fdt, p);

		p = q;
	}

	while (p < end) {
		const char *q;

		while (*p == '/') {
			p++;
			if (p == end)
				return offset;
		}
		q = memchr(p, '/', end - p);
		if (! q)
			q = end;

		offset = fdt_subnode_offset_namelen(fdt, offset, p, q-p);
		if (offset < 0)
			return offset;

		p = q;
	}

	return offset;
}

int fdt_path_offset(const void *fdt, const char *path)
{
	return fdt_path_offset_namelen(fdt, path, strlen(path));
}

const char *fdt_get_name(const void *fdt, int nodeoffset, int *len)
{
	const struct fdt_node_header *nh = fdt_offset_ptr_(fdt, nodeoffset);
	const char *nameptr;
	int err;

	if (((err = fdt_ro_probe_(fdt)) < 0)
	    || ((err = fdt_check_node_offset_(fdt, nodeoffset)) < 0))
			goto fail;

	nameptr = nh->name;

	if (!can_assume(LATEST) && fdt_version(fdt) < 0x10) {
		/*
		 * For old FDT versions, match the naming conventions of V16:
		 * give only the leaf name (after all /). The actual tree
		 * contents are loosely checked.
		 */
		const char *leaf;
		leaf = strrchr(nameptr, '/');
		if (leaf == NULL) {
			err = -FDT_ERR_BADSTRUCTURE;
			goto fail;
		}
		nameptr = leaf+1;
	}

	if (len)
		*len = strlen(nameptr);

	return nameptr;

 fail:
	if (len)
		*len = err;
	return NULL;
}

int fdt_first_property_offset(const void *fdt, int nodeoffset)
{
	int offset;

	if ((offset = fdt_check_node_offset_(fdt, nodeoffset)) < 0)
		return offset;

	return nextprop_(fdt, offset);
}

int fdt_next_property_offset(const void *fdt, int offset)
{
	if ((offset = fdt_check_prop_offset_(fdt, offset)) < 0)
		return offset;

	return nextprop_(fdt, offset);
}

static const struct fdt_property *fdt_get_property_by_offset_(const void *fdt,
						              int offset,
						              int *lenp)
{
	int err;
	const struct fdt_property *prop;

	if (!can_assume(VALID_INPUT) &&
	    (err = fdt_check_prop_offset_(fdt, offset)) < 0) {
		if (lenp)
			*lenp = err;
		return NULL;
	}

	prop = fdt_offset_ptr_(fdt, offset);

	if (lenp)
		*lenp = fdt32_ld_(&prop->len);

	return prop;
}

const struct fdt_property *fdt_get_property_by_offset(const void *fdt,
						      int offset,
						      int *lenp)
{
	/* Prior to version 16, properties may need realignment
	 * and this API does not work. fdt_getprop_*() will, however. */

	if (!can_assume(LATEST) && fdt_version(fdt) < 0x10) {
		if (lenp)
			*lenp = -FDT_ERR_BADVERSION;
		return NULL;
	}

	return fdt_get_property_by_offset_(fdt, offset, lenp);
}

static const struct fdt_property *fdt_get_property_namelen_(const void *fdt,
						            int offset,
						            const char *name,
						            int namelen,
							    int *lenp,
							    int *poffset)
{
	for (offset = fdt_first_property_offset(fdt, offset);
	     (offset >= 0);
	     (offset = fdt_next_property_offset(fdt, offset))) {
		const struct fdt_property *prop;

		prop = fdt_get_property_by_offset_(fdt, offset, lenp);
		if (!can_assume(LIBFDT_FLAWLESS) && !prop) {
			offset = -FDT_ERR_INTERNAL;
			break;
		}
		if (fdt_string_eq_(fdt, fdt32_ld_(&prop->nameoff),
				   name, namelen)) {
			if (poffset)
				*poffset = offset;
			return prop;
		}
	}

	if (lenp)
		*lenp = offset;
	return NULL;
}


const struct fdt_property *fdt_get_property_namelen(const void *fdt,
						    int offset,
						    const char *name,
						    int namelen, int *lenp)
{
	/* Prior to version 16, properties may need realignment
	 * and this API does not work. fdt_getprop_*() will, however. */
	if (!can_assume(LATEST) && fdt_version(fdt) < 0x10) {
		if (lenp)
			*lenp = -FDT_ERR_BADVERSION;
		return NULL;
	}

	return fdt_get_property_namelen_(fdt, offset, name, namelen, lenp,
					 NULL);
}


const struct fdt_property *fdt_get_property(const void *fdt,
					    int nodeoffset,
					    const char *name, int *lenp)
{
	return fdt_get_property_namelen(fdt, nodeoffset, name,
					strlen(name), lenp);
}

const void *fdt_getprop_namelen(const void *fdt, int nodeoffset,
				const char *name, int namelen, int *lenp)
{
	int poffset;
	const struct fdt_property *prop;

	prop = fdt_get_property_namelen_(fdt, nodeoffset, name, namelen, lenp,
					 &poffset);
	if (!prop)
		return NULL;

	/* Handle realignment */
	if (!can_assume(LATEST) && fdt_version(fdt) < 0x10 &&
	    (poffset + sizeof(*prop)) % 8 && fdt32_ld_(&prop->len) >= 8)
		return prop->data + 4;
	return prop->data;
}


const void *fdt_getprop(const void *fdt, int nodeoffset,
			const char *name, int *lenp)
{
	return fdt_getprop_namelen(fdt, nodeoffset, name, strlen(name), lenp);
}

uint32_t fdt_get_phandle(const void *fdt, int nodeoffset)
{
	const fdt32_t *php;
	int len;

	/* FIXME: This is a bit sub-optimal, since we potentially scan
	 * over all the properties twice. */
	php = fdt_getprop(fdt, nodeoffset, "phandle", &len);
	if (!php || (len != sizeof(*php))) {
		php = fdt_getprop(fdt, nodeoffset, "linux,phandle", &len);
		if (!php || (len != sizeof(*php)))
			return 0;
	}

	return fdt32_ld_(php);
}

const char *fdt_get_alias_namelen(const void *fdt,
				  const char *name, int namelen)
{
	int aliasoffset;

	aliasoffset = fdt_path_offset(fdt, "/aliases");
	if (aliasoffset < 0)
		return NULL;

	return fdt_getprop_namelen(fdt, aliasoffset, name, namelen, NULL);
}


int fdt_get_path(const void *fdt, int nodeoffset, char *buf, int buflen)
{
	int pdepth = 0, p = 0;
	int offset, depth, namelen;
	const char *name;

	FDT_RO_PROBE(fdt);

	if (buflen < 2)
		return -FDT_ERR_NOSPACE;

	for (offset = 0, depth = 0;
	     (offset >= 0) && (offset <= nodeoffset);
	     offset = fdt_next_node(fdt, offset, &depth)) {
		while (pdepth > depth) {
			do {
				p--;
			} while (buf[p-1] != '/');
			pdepth--;
		}

		if (pdepth >= depth) {
			name = fdt_get_name(fdt, offset, &namelen);
			if (!name)
				return namelen;
			if ((p + namelen + 1) <= buflen) {
				memcpy(buf + p, name, namelen);
				p += namelen;
				buf[p++] = '/';
				pdepth++;
			}
		}

		if (offset == nodeoffset) {
			if (pdepth < (depth + 1))
				return -FDT_ERR_NOSPACE;

			if (p > 1) /* special case so that root path is "/", not "" */
				p--;
			buf[p] = '\0';
			return 0;
		}
	}

	if ((offset == -FDT_ERR_NOTFOUND) || (offset >= 0))
		return -FDT_ERR_BADOFFSET;
	else if (offset == -FDT_ERR_BADOFFSET)
		return -FDT_ERR_BADSTRUCTURE;

	return offset; /* error from fdt_next_node() */
}

int fdt_supernode_atdepth_offset(const void *fdt, int nodeoffset,
				 int supernodedepth, int *nodedepth)
{
	int offset, depth;
	int supernodeoffset = -FDT_ERR_INTERNAL;

	FDT_RO_PROBE(fdt);

	if (supernodedepth < 0)
		return -FDT_ERR_NOTFOUND;

	for (offset = 0, depth = 0;
	     (offset >= 0) && (offset <= nodeoffset);
	     offset = fdt_next_node(fdt, offset, &depth)) {
		if (depth == supernodedepth)
			supernodeoffset = offset;

		if (offset == nodeoffset) {
			if (nodedepth)
				*nodedepth = depth;

			if (supernodedepth > depth)
				return -FDT_ERR_NOTFOUND;
			else
				return supernodeoffset;
		}
	}

	if (!can_assume(VALID_INPUT)) {
		if ((offset == -FDT_ERR_NOTFOUND) || (offset >= 0))
			return -FDT_ERR_BADOFFSET;
		else if (offset == -FDT_ERR_BADOFFSET)
			return -FDT_ERR_BADSTRUCTURE;
	}

	return offset; /* error from fdt_next_node() */
}

int fdt_node_depth(const void *fdt, int nodeoffset)
{
	int nodedepth;
	int err;

	err = fdt_supernode_atdepth_offset(fdt, nodeoffset, 0, &nodedepth);
	if (err)
		return (can_assume(LIBFDT_FLAWLESS) || err < 0) ? err :
			-FDT_ERR_INTERNAL;
	return nodedepth;
}

int fdt_parent_offset(const void *fdt, int nodeoffset)
{
	int nodedepth = fdt_node_depth(fdt, nodeoffset);

	if (nodedepth < 0)
		return nodedepth;
	return fdt_supernode_atdepth_offset(fdt, nodeoffset,
					    nodedepth - 1, NULL);
}



int fdt_stringlist_contains(const char *strlist, int listlen, const char *str)
{
	int len = strlen(str);
	const char *p;

	while (listlen >= len) {
		if (memcmp(str, strlist, len+1) == 0)
			return 1;
		p = memchr(strlist, '\0', listlen);
		if (!p)
			return 0; /* malformed strlist.. */
		listlen -= (p-strlist) + 1;
		strlist = p + 1;
	}
	return 0;
}




int fdt_node_check_compatible(const void *fdt, int nodeoffset,
			      const char *compatible)
{
	const void *prop;
	int len;

	prop = fdt_getprop(fdt, nodeoffset, "compatible", &len);
	if (!prop)
		return len;

	return !fdt_stringlist_contains(prop, len, compatible);
}


/* === END INLINED: fdt_ro.c === */
/* === BEGIN INLINED: fdt_rw.c === */
#include <xen_xen_config.h>
// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 */
#include "libfdt_env.h"

#include <xen_libfdt_fdt.h>
#include <xen_libfdt_libfdt.h>

#include "libfdt_internal.h"

static int fdt_blocks_misordered_(const void *fdt,
				  int mem_rsv_size, int struct_size)
{
	return (fdt_off_mem_rsvmap(fdt) < FDT_ALIGN(sizeof(struct fdt_header), 8))
		|| (fdt_off_dt_struct(fdt) <
		    (fdt_off_mem_rsvmap(fdt) + mem_rsv_size))
		|| (fdt_off_dt_strings(fdt) <
		    (fdt_off_dt_struct(fdt) + struct_size))
		|| (fdt_totalsize(fdt) <
		    (fdt_off_dt_strings(fdt) + fdt_size_dt_strings(fdt)));
}

static int fdt_rw_probe_(void *fdt)
{
	if (can_assume(VALID_DTB))
		return 0;
	FDT_RO_PROBE(fdt);

	if (!can_assume(LATEST) && fdt_version(fdt) < 17)
		return -FDT_ERR_BADVERSION;
	if (fdt_blocks_misordered_(fdt, sizeof(struct fdt_reserve_entry),
				   fdt_size_dt_struct(fdt)))
		return -FDT_ERR_BADLAYOUT;
	if (!can_assume(LATEST) && fdt_version(fdt) > 17)
		fdt_set_version(fdt, 17);

	return 0;
}

#define FDT_RW_PROBE(fdt) \
	{ \
		int err_; \
		if ((err_ = fdt_rw_probe_(fdt)) != 0) \
			return err_; \
	}

static inline unsigned int fdt_data_size_(void *fdt)
{
	return fdt_off_dt_strings(fdt) + fdt_size_dt_strings(fdt);
}

static int fdt_splice_(void *fdt, void *splicepoint, int oldlen, int newlen)
{
	char *p = splicepoint;
	unsigned int dsize = fdt_data_size_(fdt);
	size_t soff = p - (char *)fdt;

	if ((oldlen < 0) || (soff + oldlen < soff) || (soff + oldlen > dsize))
		return -FDT_ERR_BADOFFSET;
	if ((p < (char *)fdt) || (dsize + newlen < (unsigned)oldlen))
		return -FDT_ERR_BADOFFSET;
	if (dsize - oldlen + newlen > fdt_totalsize(fdt))
		return -FDT_ERR_NOSPACE;
	memmove(p + newlen, p + oldlen, ((char *)fdt + dsize) - (p + oldlen));
	return 0;
}

static int fdt_splice_mem_rsv_(void *fdt, struct fdt_reserve_entry *p,
			       int oldn, int newn)
{
	int delta = (newn - oldn) * sizeof(*p);
	int err;
	err = fdt_splice_(fdt, p, oldn * sizeof(*p), newn * sizeof(*p));
	if (err)
		return err;
	fdt_set_off_dt_struct(fdt, fdt_off_dt_struct(fdt) + delta);
	fdt_set_off_dt_strings(fdt, fdt_off_dt_strings(fdt) + delta);
	return 0;
}

static int fdt_splice_struct_(void *fdt, void *p,
			      int oldlen, int newlen)
{
	int delta = newlen - oldlen;
	int err;

	if ((err = fdt_splice_(fdt, p, oldlen, newlen)))
		return err;

	fdt_set_size_dt_struct(fdt, fdt_size_dt_struct(fdt) + delta);
	fdt_set_off_dt_strings(fdt, fdt_off_dt_strings(fdt) + delta);
	return 0;
}

/* Must only be used to roll back in case of error */
static void fdt_del_last_string_(void *fdt, const char *s)
{
	int newlen = strlen(s) + 1;

	fdt_set_size_dt_strings(fdt, fdt_size_dt_strings(fdt) - newlen);
}

static int fdt_splice_string_(void *fdt, int newlen)
{
	void *p = (char *)fdt
		+ fdt_off_dt_strings(fdt) + fdt_size_dt_strings(fdt);
	int err;

	if ((err = fdt_splice_(fdt, p, 0, newlen)))
		return err;

	fdt_set_size_dt_strings(fdt, fdt_size_dt_strings(fdt) + newlen);
	return 0;
}

/**
 * fdt_find_add_string_() - Find or allocate a string
 *
 * @fdt: pointer to the device tree to check/adjust
 * @s: string to find/add
 * @allocated: Set to 0 if the string was found, 1 if not found and so
 *	allocated. Ignored if can_assume(NO_ROLLBACK)
 * @return offset of string in the string table (whether found or added)
 */
static int fdt_find_add_string_(void *fdt, const char *s, int *allocated)
{
	char *strtab = (char *)fdt + fdt_off_dt_strings(fdt);
	const char *p;
	char *new;
	int len = strlen(s) + 1;
	int err;

	if (!can_assume(NO_ROLLBACK))
		*allocated = 0;

	p = fdt_find_string_(strtab, fdt_size_dt_strings(fdt), s);
	if (p)
		/* found it */
		return (p - strtab);

	new = strtab + fdt_size_dt_strings(fdt);
	err = fdt_splice_string_(fdt, len);
	if (err)
		return err;

	if (!can_assume(NO_ROLLBACK))
		*allocated = 1;

	memcpy(new, s, len);
	return (new - strtab);
}



static int fdt_resize_property_(void *fdt, int nodeoffset, const char *name,
				int len, struct fdt_property **prop)
{
	int oldlen;
	int err;

	*prop = fdt_get_property_w(fdt, nodeoffset, name, &oldlen);
	if (!*prop)
		return oldlen;

	if ((err = fdt_splice_struct_(fdt, (*prop)->data, FDT_TAGALIGN(oldlen),
				      FDT_TAGALIGN(len))))
		return err;

	(*prop)->len = cpu_to_fdt32(len);
	return 0;
}

static int fdt_add_property_(void *fdt, int nodeoffset, const char *name,
			     int len, struct fdt_property **prop)
{
	int proplen;
	int nextoffset;
	int namestroff;
	int err;
	int allocated;

	if ((nextoffset = fdt_check_node_offset_(fdt, nodeoffset)) < 0)
		return nextoffset;

	namestroff = fdt_find_add_string_(fdt, name, &allocated);
	if (namestroff < 0)
		return namestroff;

	*prop = fdt_offset_ptr_w_(fdt, nextoffset);
	proplen = sizeof(**prop) + FDT_TAGALIGN(len);

	err = fdt_splice_struct_(fdt, *prop, 0, proplen);
	if (err) {
		/* Delete the string if we failed to add it */
		if (!can_assume(NO_ROLLBACK) && allocated)
			fdt_del_last_string_(fdt, name);
		return err;
	}

	(*prop)->tag = cpu_to_fdt32(FDT_PROP);
	(*prop)->nameoff = cpu_to_fdt32(namestroff);
	(*prop)->len = cpu_to_fdt32(len);
	return 0;
}









static void fdt_packblocks_(const char *old, char *new,
			    int mem_rsv_size,
			    int struct_size,
			    int strings_size)
{
	int mem_rsv_off, struct_off, strings_off;

	mem_rsv_off = FDT_ALIGN(sizeof(struct fdt_header), 8);
	struct_off = mem_rsv_off + mem_rsv_size;
	strings_off = struct_off + struct_size;

	memmove(new + mem_rsv_off, old + fdt_off_mem_rsvmap(old), mem_rsv_size);
	fdt_set_off_mem_rsvmap(new, mem_rsv_off);

	memmove(new + struct_off, old + fdt_off_dt_struct(old), struct_size);
	fdt_set_off_dt_struct(new, struct_off);
	fdt_set_size_dt_struct(new, struct_size);

	memmove(new + strings_off, old + fdt_off_dt_strings(old), strings_size);
	fdt_set_off_dt_strings(new, strings_off);
	fdt_set_size_dt_strings(new, fdt_size_dt_strings(old));
}



/* === END INLINED: fdt_rw.c === */
/* fdt_sw.c compiled separately - conflicts with fdt_rw.c on fdt_del_last_string_/fdt_find_add_string_ */
/* === BEGIN INLINED: fdt_wip.c === */
#include <xen_xen_config.h>
// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 */
#include "libfdt_env.h"

#include <xen_libfdt_fdt.h>
#include <xen_libfdt_libfdt.h>

#include "libfdt_internal.h"

int fdt_setprop_inplace_namelen_partial(void *fdt, int nodeoffset,
					const char *name, int namelen,
					uint32_t idx, const void *val,
					int len)
{
	void *propval;
	int proplen;

	propval = fdt_getprop_namelen_w(fdt, nodeoffset, name, namelen,
					&proplen);
	if (!propval)
		return proplen;

	if ((unsigned)proplen < (len + idx))
		return -FDT_ERR_NOSPACE;

	memcpy((char *)propval + idx, val, len);
	return 0;
}

int fdt_setprop_inplace(void *fdt, int nodeoffset, const char *name,
			const void *val, int len)
{
	const void *propval;
	int proplen;

	propval = fdt_getprop(fdt, nodeoffset, name, &proplen);
	if (!propval)
		return proplen;

	if (proplen != len)
		return -FDT_ERR_NOSPACE;

	return fdt_setprop_inplace_namelen_partial(fdt, nodeoffset, name,
						   strlen(name), 0,
						   val, len);
}

static void fdt_nop_region_(void *start, int len)
{
	fdt32_t *p;

	for (p = start; (char *)p < ((char *)start + len); p++)
		*p = cpu_to_fdt32(FDT_NOP);
}


int fdt_node_end_offset_(void *fdt, int offset)
{
	int depth = 0;

	while ((offset >= 0) && (depth >= 0))
		offset = fdt_next_node(fdt, offset, &depth);

	return offset;
}


/* === END INLINED: fdt_wip.c === */
