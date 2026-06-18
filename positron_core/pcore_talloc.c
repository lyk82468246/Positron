/*
 * pcore_talloc.c - a minimal hierarchical allocator implementing the subset of
 * Samba talloc (netsurf/utils/talloc.h) that the ported NetSurf box / layout /
 * redraw code actually uses.
 *
 * The real utils/talloc.c is ~1000 lines of pools, references, chunk magic and
 * leak reporting - almost none of which the box tree needs, and much of which
 * is hostile to MSVC9 / WinCE. So that file is EXCLUDED from the build and this
 * shim provides only the contract the box code relies on:
 *
 *   - hierarchical allocation (a child is freed when its parent is freed)
 *   - destructors (talloc_set_destructor; box uses one to detach on free)
 *   - reparenting (talloc_steal)
 *   - realloc with link fix-up (talloc_realloc / _array)
 *   - memdup / strdup / strndup / zero / array helpers
 *
 * We keep the real talloc.h so the ported .c files compile unmodified; only the
 * backing functions its macros expand to are implemented here.
 *
 * Each allocation carries a header (struct tc) immediately before the user
 * pointer; the user pointer IS a talloc context. C89, over malloc/realloc/free.
 */

#include <stdlib.h>
#include <string.h>

#include "utils/talloc.h"

typedef struct tc {
    struct tc *parent;       /* owning context, or NULL */
    struct tc *child;        /* first child */
    struct tc *next;         /* next sibling in parent's child list */
    struct tc *prev;         /* previous sibling */
    int (*destructor)(void *);
    const char *name;
    size_t size;
    /* user data follows immediately after this header */
} tc;

#define TC_FROM_PTR(p)  (((tc *) (void *) (p)) - 1)
#define PTR_FROM_TC(c)  ((void *) ((c) + 1))

/* Link `c` as the first child of `parent` (parent may be NULL = top level). */
static void tc_link(tc *c, tc *parent)
{
    c->parent = parent;
    c->prev = NULL;
    c->next = (parent != NULL) ? parent->child : NULL;
    if (c->next != NULL) {
        c->next->prev = c;
    }
    if (parent != NULL) {
        parent->child = c;
    }
}

/* Detach `c` from its parent's child list. */
static void tc_unlink(tc *c)
{
    if (c->prev != NULL) {
        c->prev->next = c->next;
    } else if (c->parent != NULL) {
        c->parent->child = c->next;
    }
    if (c->next != NULL) {
        c->next->prev = c->prev;
    }
    c->parent = NULL;
    c->next = NULL;
    c->prev = NULL;
}

void *_talloc(const void *context, size_t size)
{
    tc *c = (tc *) malloc(sizeof(tc) + size);
    if (c == NULL) {
        return NULL;
    }
    c->child = NULL;
    c->destructor = NULL;
    c->name = NULL;
    c->size = size;
    tc_link(c, (context != NULL) ? TC_FROM_PTR(context) : NULL);
    return PTR_FROM_TC(c);
}

void *talloc_named_const(const void *context, size_t size, const char *name)
{
    void *p = _talloc(context, size);
    if (p != NULL) {
        TC_FROM_PTR(p)->name = name;
    }
    return p;
}

void *_talloc_zero(const void *ctx, size_t size, const char *name)
{
    void *p = talloc_named_const(ctx, size, name);
    if (p != NULL) {
        memset(p, 0, size);
    }
    return p;
}

void *_talloc_array(const void *ctx, size_t el_size, unsigned count,
        const char *name)
{
    return talloc_named_const(ctx, el_size * count, name);
}

void *_talloc_zero_array(const void *ctx, size_t el_size, unsigned count,
        const char *name)
{
    return _talloc_zero(ctx, el_size * count, name);
}

int talloc_free(void *ptr)
{
    tc *c;

    if (ptr == NULL) {
        return -1;
    }
    c = TC_FROM_PTR(ptr);

    if (c->destructor != NULL) {
        if (c->destructor(ptr) == -1) {
            return -1;   /* destructor refused the free */
        }
        c->destructor = NULL;
    }

    while (c->child != NULL) {
        tc *first = c->child;
        if (talloc_free(PTR_FROM_TC(first)) == -1) {
            /* child refused; detach it so we don't spin */
            tc_unlink(first);
        }
    }

    tc_unlink(c);
    free(c);
    return 0;
}

void talloc_free_children(void *ptr)
{
    tc *c;

    if (ptr == NULL) {
        return;
    }
    c = TC_FROM_PTR(ptr);
    while (c->child != NULL) {
        tc *first = c->child;
        if (talloc_free(PTR_FROM_TC(first)) == -1) {
            tc_unlink(first);
        }
    }
}

void *_talloc_realloc(const void *context, void *ptr, size_t size,
        const char *name)
{
    tc *oldc;
    tc *newc;
    tc *ch;

    if (ptr == NULL) {
        return talloc_named_const(context, size, name);
    }
    if (size == 0) {
        talloc_free(ptr);
        return NULL;
    }

    oldc = TC_FROM_PTR(ptr);
    newc = (tc *) realloc(oldc, sizeof(tc) + size);
    if (newc == NULL) {
        return NULL;   /* old block left intact */
    }
    newc->size = size;

    /* realloc may have moved the block; fix every pointer that referenced it. */
    if (newc->prev != NULL) {
        newc->prev->next = newc;
    } else if (newc->parent != NULL) {
        newc->parent->child = newc;
    }
    if (newc->next != NULL) {
        newc->next->prev = newc;
    }
    for (ch = newc->child; ch != NULL; ch = ch->next) {
        ch->parent = newc;
    }
    return PTR_FROM_TC(newc);
}

void *_talloc_realloc_array(const void *ctx, void *ptr, size_t el_size,
        unsigned count, const char *name)
{
    return _talloc_realloc(ctx, ptr, el_size * count, name);
}

void *_talloc_memdup(const void *t, const void *p, size_t size,
        const char *name)
{
    void *n = talloc_named_const(t, size, name);
    if (n != NULL && p != NULL) {
        memcpy(n, p, size);
    }
    return n;
}

char *talloc_strdup(const void *t, const char *p)
{
    size_t len;
    char *n;

    if (p == NULL) {
        return NULL;
    }
    len = strlen(p);
    n = (char *) talloc_named_const(t, len + 1, "talloc_strdup");
    if (n != NULL) {
        memcpy(n, p, len + 1);
    }
    return n;
}

char *talloc_strndup(const void *t, const char *p, size_t n)
{
    size_t len = 0;
    char *r;

    if (p == NULL) {
        return NULL;
    }
    while (len < n && p[len] != '\0') {
        len++;
    }
    r = (char *) talloc_named_const(t, len + 1, "talloc_strndup");
    if (r != NULL) {
        memcpy(r, p, len);
        r[len] = '\0';
    }
    return r;
}

void _talloc_set_destructor(const void *ptr, int (*destructor)(void *))
{
    if (ptr != NULL) {
        TC_FROM_PTR(ptr)->destructor = destructor;
    }
}

void *_talloc_steal(const void *new_ctx, const void *ptr)
{
    tc *c;

    if (ptr == NULL) {
        return NULL;
    }
    c = TC_FROM_PTR(ptr);
    tc_unlink(c);
    tc_link(c, (new_ctx != NULL) ? TC_FROM_PTR(new_ctx) : NULL);
    return (void *) ptr;
}

void talloc_set_name_const(const void *ptr, const char *name)
{
    if (ptr != NULL) {
        TC_FROM_PTR(ptr)->name = name;
    }
}

const char *talloc_get_name(const void *ptr)
{
    return (ptr != NULL) ? TC_FROM_PTR(ptr)->name : NULL;
}

size_t talloc_get_size(const void *ctx)
{
    return (ctx != NULL) ? TC_FROM_PTR(ctx)->size : 0;
}

void *talloc_parent(const void *ptr)
{
    tc *c;

    if (ptr == NULL) {
        return NULL;
    }
    c = TC_FROM_PTR(ptr);
    return (c->parent != NULL) ? PTR_FROM_TC(c->parent) : NULL;
}

/* talloc_init / talloc_new: a named top-level context (size 0 is fine). */
void *talloc_init(const char *fmt, ...)
{
    (void) fmt;
    return talloc_named_const(NULL, 0, "talloc_init");
}
