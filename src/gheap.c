/*
 * Guest heap: a boundary-tag allocator with segregated free lists over the
 * identity-mapped heap region. All pointers it returns are valid in both the
 * host and the emulated CPU.
 */
#include "emu.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

enum {
    ALIGN = 16,
    HDR = 16, /* keeps payloads 16-byte aligned */
    MIN_BLOCK = 32,
    BIN_COUNT = 64,
    FLAG_USED = 1u,
    FLAG_PREV_USED = 2u,
};

struct block {
    uint32_t prev_size; /* valid only when the previous block is free */
    uint32_t size;      /* block size including header, low bits are flags */
    uint32_t next_free;
    uint32_t prev_free;
};

static uint32_t g_bins[BIN_COUNT];
static uint32_t g_top;   /* start of the untouched tail */
static uint32_t g_limit; /* end of the heap */

static struct block *B(uint32_t addr) {
    return (struct block *)G2H(addr);
}

static uint32_t bsize(uint32_t addr) {
    return B(addr)->size & ~15u;
}

static int bin_index(uint32_t size) {
    /* exact 16-byte classes up to 512 bytes, then logarithmic */
    if (size <= 512) {
        return (int)(size / 16u) - 2;
    }
    int log2 = 31 - __builtin_clz(size);
    int bin = 30 + (log2 - 9) * 2 + (int)((size >> (log2 - 1)) & 1u);
    return bin < BIN_COUNT ? bin : BIN_COUNT - 1;
}

static void unlink_free(uint32_t addr) {
    struct block *b = B(addr);
    if (b->prev_free) {
        B(b->prev_free)->next_free = b->next_free;
    } else {
        g_bins[bin_index(bsize(addr))] = b->next_free;
    }
    if (b->next_free) {
        B(b->next_free)->prev_free = b->prev_free;
    }
}

static void link_free(uint32_t addr) {
    uint32_t size = bsize(addr);
    int bin = bin_index(size);
    struct block *b = B(addr);
    b->prev_free = 0;
    b->next_free = g_bins[bin];
    if (g_bins[bin]) {
        B(g_bins[bin])->prev_free = addr;
    }
    g_bins[bin] = addr;
    /* footer for backward coalescing */
    uint32_t next = addr + size;
    if (next < g_top) {
        B(next)->prev_size = size;
        B(next)->size &= ~FLAG_PREV_USED;
    }
}

static void set_next_prev_used(uint32_t addr) {
    uint32_t next = addr + bsize(addr);
    if (next < g_top) {
        B(next)->size |= FLAG_PREV_USED;
    }
}

void gheap_init(void) {
    memset(g_bins, 0, sizeof(g_bins));
    g_top = GUEST_HEAP_BASE + 0x1000u; /* keep a guard gap */
    g_limit = (uint32_t)GUEST_HEAP_BASE + (uint32_t)GUEST_HEAP_SIZE;
}

static uint32_t take_from_top(uint32_t need) {
    if (need > g_limit - g_top) {
        return 0;
    }
    uint32_t addr = g_top;
    g_top += need;
    B(addr)->size = need | FLAG_USED | FLAG_PREV_USED;
    return addr;
}

static void split(uint32_t addr, uint32_t need) {
    uint32_t size = bsize(addr);
    if (size - need >= MIN_BLOCK) {
        uint32_t flags = B(addr)->size & 15u;
        B(addr)->size = need | flags;
        uint32_t rest = addr + need;
        B(rest)->size = (size - need) | FLAG_PREV_USED;
        link_free(rest);
    }
}

uint32_t gmalloc(uint32_t size) {
    uint32_t need = (size + HDR + ALIGN - 1) & ~(uint32_t)(ALIGN - 1);
    if (need < MIN_BLOCK) {
        need = MIN_BLOCK;
    }
    for (int bin = bin_index(need); bin < BIN_COUNT; ++bin) {
        for (uint32_t addr = g_bins[bin]; addr; addr = B(addr)->next_free) {
            if (bsize(addr) >= need) {
                unlink_free(addr);
                B(addr)->size |= FLAG_USED;
                set_next_prev_used(addr);
                split(addr, need);
                return addr + HDR;
            }
            if (bin < 30) {
                break; /* exact classes: first entry decides */
            }
        }
    }
    uint32_t addr = take_from_top(need);
    if (!addr) {
        log_error("guest heap exhausted (%u bytes requested)", size);
        return 0;
    }
    return addr + HDR;
}

uint32_t gcalloc(uint32_t count, uint32_t size) {
    uint64_t total = (uint64_t)count * size;
    if (total > 0xffffffffu) {
        return 0;
    }
    uint32_t p = gmalloc((uint32_t)total);
    if (p) {
        memset(G2H(p), 0, (size_t)total);
    }
    return p;
}

void gfree(uint32_t ptr) {
    if (!ptr) {
        return;
    }
    uint32_t addr = ptr - HDR;
    if (addr < GUEST_HEAP_BASE || addr >= g_top || !(B(addr)->size & FLAG_USED)) {
        log_debug("ignoring free of invalid pointer 0x%08x", ptr);
        return;
    }
    uint32_t size = bsize(addr);
    uint32_t flags = B(addr)->size & FLAG_PREV_USED;

    uint32_t next = addr + size;
    if (next < g_top && !(B(next)->size & FLAG_USED)) {
        unlink_free(next);
        size += bsize(next);
    }
    if (!(flags & FLAG_PREV_USED)) {
        uint32_t prev = addr - B(addr)->prev_size;
        unlink_free(prev);
        size += bsize(prev);
        addr = prev;
        flags = B(prev)->size & FLAG_PREV_USED;
    }
    if (addr + size == g_top) {
        g_top = addr; /* give the tail back */
        return;
    }
    B(addr)->size = size | flags;
    link_free(addr);
}

uint32_t gsize(uint32_t ptr) {
    return ptr ? bsize(ptr - HDR) - HDR : 0;
}

uint32_t grealloc(uint32_t ptr, uint32_t size) {
    if (!ptr) {
        return gmalloc(size);
    }
    uint32_t have = gsize(ptr);
    if (size <= have) {
        return ptr;
    }
    uint32_t addr = ptr - HDR;
    uint32_t need = (size + HDR + ALIGN - 1) & ~(uint32_t)(ALIGN - 1);
    uint32_t next = addr + bsize(addr);
    if (next == g_top && need - bsize(addr) <= g_limit - g_top) {
        g_top = addr + need;
        B(addr)->size = need | (B(addr)->size & 15u);
        return ptr;
    }
    if (next < g_top && !(B(next)->size & FLAG_USED) && bsize(addr) + bsize(next) >= need) {
        unlink_free(next);
        B(addr)->size += bsize(next);
        set_next_prev_used(addr);
        split(addr, need);
        return ptr;
    }
    uint32_t fresh = gmalloc(size);
    if (fresh) {
        memcpy(G2H(fresh), G2H(ptr), have);
        gfree(ptr);
    }
    return fresh;
}
