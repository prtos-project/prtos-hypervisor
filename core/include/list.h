/*
 * FILE: list.h
 *
 * List
 *
 * www.prtos.org
 */

#ifndef _PRTOS_LIST_H_
#define _PRTOS_LIST_H_

#ifndef __ASSEMBLY__
#include <assert.h>
#include <spinlock.h>

struct dyn_list;

struct dyn_list_node {
    struct dyn_list *list;
    struct dyn_list_node *prev, *next;
};

struct dyn_list {
    struct dyn_list_node *head;
    prtos_s32_t num_of_elems;
    spin_lock_t lock;
};

static inline void dyn_list_init(struct dyn_list *l) {
    l->lock = SPINLOCK_INIT;
    spin_lock(&l->lock);
    l->num_of_elems = 0;
    l->head = 0;
    spin_unlock(&l->lock);
}

static inline prtos_s32_t dyn_list_insert_head(struct dyn_list *l, struct dyn_list_node *e) {
    if (e->list) {
        ASSERT(e->list == l);
        return 0;
    }
    ASSERT(!e->next && !e->prev);
    spin_lock(&l->lock);
    if (l->head) {
        ASSERT_LOCK(l->num_of_elems > 0, &l->lock);
        e->next = l->head;
        e->prev = l->head->prev;
        l->head->prev->next = e;
        l->head->prev = e;
    } else {
        ASSERT_LOCK(!l->num_of_elems, &l->lock);
        e->prev = e->next = e;
    }
    l->head = e;
    l->num_of_elems++;
    e->list = l;
    spin_unlock(&l->lock);
    ASSERT(l->num_of_elems >= 0);
    return 0;
}

static inline prtos_s32_t dyn_list_insert_tail(struct dyn_list *l, struct dyn_list_node *e) {
    if (e->list) {
        ASSERT(e->list == l);
        return 0;
    }
    ASSERT(!e->next && !e->prev);
    spin_lock(&l->lock);
    if (l->head) {
        e->next = l->head;
        e->prev = l->head->prev;
        l->head->prev->next = e;
        l->head->prev = e;
    } else {
        e->prev = e->next = e;
        l->head = e;
    }
    l->num_of_elems++;
    e->list = l;
    spin_unlock(&l->lock);
    ASSERT(l->num_of_elems >= 0);

    return 0;
}

static inline void *dyn_list_remove_head(struct dyn_list *l) {
    struct dyn_list_node *e = 0;
    spin_lock(&l->lock);
    if (l->head) {
        e = l->head;
        l->head = e->next;
        e->prev->next = e->next;
        e->next->prev = e->prev;
        e->prev = e->next = 0;
        e->list = 0;
        l->num_of_elems--;
        if (!l->num_of_elems) l->head = 0;
    }
    spin_unlock(&l->lock);
    ASSERT(l->num_of_elems >= 0);

    return e;
}

static inline void *dyn_list_remove_tail(struct dyn_list *l) {
    struct dyn_list_node *e = 0;
    spin_lock(&l->lock);
    if (l->head) {
        e = l->head->prev;
        e->prev->next = e->next;
        e->next->prev = e->prev;
        e->prev = e->next = 0;
        e->list = 0;
        l->num_of_elems--;
        if (!l->num_of_elems) l->head = 0;
    }
    spin_unlock(&l->lock);
    ASSERT(l->num_of_elems >= 0);

    return e;
}

static inline prtos_s32_t dyn_list_remove_element(struct dyn_list *l, struct dyn_list_node *e) {
    ASSERT(e->list == l);
    ASSERT(e->prev && e->next);
    spin_lock(&l->lock);
    e->prev->next = e->next;
    e->next->prev = e->prev;
    if (l->head == e) l->head = e->next;
    e->prev = e->next = 0;
    e->list = 0;
    l->num_of_elems--;
    if (!l->num_of_elems) l->head = 0;
    spin_unlock(&l->lock);
    ASSERT(l->num_of_elems >= 0);

    return 0;
}

#define DYNLIST_FOR_EACH_ELEMENT_BEGIN(_l, _element, _cond) \
    do {                                                    \
        prtos_s32_t __e;                                    \
        struct dyn_list_node *__n;                          \
        spin_lock(&(_l)->lock);                             \
        for (__e = (_l)->num_of_elems, __n = (_l)->head, _element = (void *)__n; __e && (_cond); __e--, __n = __n->next, _element = (void *)__n) {
#define DYNLIST_FOR_EACH_ELEMENT_END(_l) \
    }                                    \
    spin_unlock(&(_l)->lock);            \
    }                                    \
    while (0)

#define DYNLIST_FOR_EACH_ELEMENT_EXIT(_l) spin_unlock(&(_l)->lock)

#endif
#endif
