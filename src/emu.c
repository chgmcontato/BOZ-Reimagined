#include "emu.h"
#include "host.h"

struct perf_counters g_perf;
uint32_t g_perf_by_thunk[8192];
#include "log.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unicorn/unicorn.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#endif

enum { MAX_THUNKS = (GUEST_THUNK_SIZE - 16) / 8 };

struct thunk {
    const char *name;
    import_fn fn;
};

static uc_engine *g_uc;
static struct thunk g_thunks[MAX_THUNKS];
static uint32_t g_thunk_count;
static uint32_t *g_thunk_code;

/* State of the import being serviced; saved across nested guest calls. */
static uint32_t g_args[4];
static uint32_t g_sp;
static const char *g_current;
static int g_depth;

static int g_trace;
static uint32_t g_arena_next = GUEST_ARENA_BASE;

static const int g_ctx_regs[] = {
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,  UC_ARM_REG_R3,  UC_ARM_REG_R4,
    UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,  UC_ARM_REG_R8,  UC_ARM_REG_R9,
    UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR,
    UC_ARM_REG_PC, UC_ARM_REG_CPSR,
};
enum { CTX_REG_COUNT = sizeof(g_ctx_regs) / sizeof(g_ctx_regs[0]) };

static uint32_t reg(int id) {
    uint32_t value = 0;
    uc_reg_read(g_uc, id, &value);
    return value;
}

static void set_reg(int id, uint32_t value) {
    uc_reg_write(g_uc, id, &value);
}

static void *host_map_fixed(uint32_t addr, uint32_t size) {
#ifdef _WIN32
    void *p = VirtualAlloc((void *)(uintptr_t)addr, size, MEM_RESERVE | MEM_COMMIT,
                           PAGE_EXECUTE_READWRITE);
    return p == (void *)(uintptr_t)addr ? p : NULL;
#else
    void *p = mmap((void *)(uintptr_t)addr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        return NULL;
    }
    if (p != (void *)(uintptr_t)addr) {
        munmap(p, size);
        return NULL;
    }
    return p;
#endif
}

bool emu_map_identity(uint32_t addr, uint32_t size) {
    size = (size + 0xfffu) & ~0xfffu;
    void *host = host_map_fixed(addr, size);
    if (!host) {
        log_error("unable to reserve guest range 0x%08x+0x%x in the host", addr, size);
        return false;
    }
    uint32_t perms = UC_PROT_ALL;
    if (addr == GUEST_HEAP_BASE && getenv("CODBOZ_HEAP_RW")) {
        perms = UC_PROT_READ | UC_PROT_WRITE;
    }
    uc_err err = uc_mem_map_ptr(g_uc, addr, size, perms, host);
    if (err != UC_ERR_OK) {
        log_error("uc_mem_map_ptr 0x%08x: %s", addr, uc_strerror(err));
        return false;
    }
    if (addr == GUEST_HEAP_BASE) {
        log_info("guest heap permissions: %s (CODBOZ_HEAP_RW %s)",
                 perms == UC_PROT_ALL ? "RWX" : "RW",
                 perms == UC_PROT_ALL ? "unset" : "set");
    }
    return true;
}

static void hook_intr(uc_engine *uc, uint32_t intno, void *user) {
    (void)user;
    if (intno != 2) {
        emu_dump_state("unexpected CPU exception");
        log_error("exception %u", intno);
        uc_emu_stop(uc);
        return;
    }
    uint32_t pc = reg(UC_ARM_REG_PC);
    uint32_t index = *(uint32_t *)G2H(pc - 4) & 0x00ffffffu;
    if (pc - 4 < GUEST_THUNK_BASE || index >= g_thunk_count) {
        emu_dump_state("stray SVC");
        uc_emu_stop(uc);
        return;
    }

    uint32_t saved_args[4];
    uint32_t saved_sp = g_sp;
    const char *saved_current = g_current;
    memcpy(saved_args, g_args, sizeof(saved_args));

    g_args[0] = reg(UC_ARM_REG_R0);
    g_args[1] = reg(UC_ARM_REG_R1);
    g_args[2] = reg(UC_ARM_REG_R2);
    g_args[3] = reg(UC_ARM_REG_R3);
    g_sp = reg(UC_ARM_REG_SP);
    g_current = g_thunks[index].name;
    if (g_trace) {
        fprintf(stderr, "[trace] %s(%08x, %08x, %08x, %08x) lr=%08x\n", g_current, g_args[0],
                g_args[1], g_args[2], g_args[3], reg(UC_ARM_REG_LR));
    }
    static int depth;
    uint64_t t0 = depth == 0 ? monotonic_us() : 0;
    depth++;
    g_thunks[index].fn();
    depth--;
    g_perf.calls++;
    g_perf_by_thunk[index]++;
    if (depth == 0) {
        g_perf.host_us += monotonic_us() - t0;
    }

    memcpy(g_args, saved_args, sizeof(saved_args));
    g_sp = saved_sp;
    g_current = saved_current;
}

static bool hook_unmapped(uc_engine *uc, uc_mem_type type, uint64_t address, int size,
                          int64_t value, void *user) {
    (void)user;
    (void)value;
    log_error("invalid %s at 0x%08llx (size %d) during %s",
              type == UC_MEM_WRITE_UNMAPPED ? "write"
              : type == UC_MEM_FETCH_UNMAPPED ? "fetch"
                                              : "read",
              (unsigned long long)address, size, g_current ? g_current : "guest code");
    emu_dump_state("memory fault");
    uc_emu_stop(uc);
    return false;
}

bool emu_init(void) {
    g_trace = getenv("CODBOZ_TRACE") != NULL;
    uc_err err = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &g_uc);
    if (err != UC_ERR_OK) {
        log_error("uc_open: %s", uc_strerror(err));
        return false;
    }
    uc_ctl_set_cpu_model(g_uc, UC_CPU_ARM_CORTEX_A15);

    /* The setter must precede memory/register APIs (the getter initializes TCG).
     * 64-bit Unicorn 2.1.4 defaults to 1 GiB; zero retains that library default. */
    uint32_t tcg_mb = 2048;
    const char *tcg_option = getenv("CODBOZ_TCG_MB");
    if (tcg_option) {
        char *end;
        errno = 0;
        unsigned long value = strtoul(tcg_option, &end, 10);
        if (errno || end == tcg_option || *end || value > 2048 || (value && value < 32)) {
            log_info("invalid CODBOZ_TCG_MB (use 0 or 32..2048); using %u MiB", tcg_mb);
        } else {
            tcg_mb = (uint32_t)value;
        }
    }
    err = uc_ctl_set_tcg_buffer_size(g_uc, tcg_mb * 1024u * 1024u);
    if (err != UC_ERR_OK) {
        log_error("setting TCG buffer size: %s", uc_strerror(err));
        return false;
    }

    /* Experimental identity translation: opt in only with the exact value 1.
     * Set this after TCG size, before mappings initialize the engine. Never
     * silently fall back after a failed request: that would invalidate an A/B. */
    const char *tlb_option = getenv("CODBOZ_TLB_VIRTUAL");
    if (tlb_option && strcmp(tlb_option, "1") == 0) {
        err = uc_ctl_tlb_mode(g_uc, UC_TLB_VIRTUAL);
        if (err != UC_ERR_OK) {
            log_error("tlb_mode: virtual configuration failed: %s; aborting test", uc_strerror(err));
            return false;
        }
        log_info("tlb_mode: virtual (experimental)");
    } else {
        log_info("tlb_mode: softmmu (default)");
    }

    if (!emu_map_identity(GUEST_STACK_BASE, GUEST_STACK_SIZE) ||
        !emu_map_identity(GUEST_ARENA_BASE, GUEST_ARENA_SIZE) ||
        !emu_map_identity(GUEST_THUNK_BASE, GUEST_THUNK_SIZE) ||
        !emu_map_identity(GUEST_HEAP_BASE, GUEST_HEAP_SIZE)) {
        return false;
    }
    uint32_t tcg_bytes = 0;
    err = uc_ctl_get_tcg_buffer_size(g_uc, &tcg_bytes);
    if (err == UC_ERR_OK) {
        log_info("TCG buffer: %u MiB (CODBOZ_TCG_MB=%u; 0 uses library default)",
                 tcg_bytes / (1024u * 1024u), tcg_mb);
    }
    g_thunk_code = G2H(GUEST_THUNK_BASE);
    /* The return trampoline is never executed: uc_emu_start stops on it. */
    *(uint32_t *)G2H(GUEST_RETURN_MAGIC) = 0xe12fff1eu;

    /* Enable VFP/NEON: CPACR full access to cp10/cp11, FPEXC.EN. */
    uint32_t cpacr = reg(UC_ARM_REG_C1_C0_2) | (0xfu << 20);
    set_reg(UC_ARM_REG_C1_C0_2, cpacr);
    set_reg(UC_ARM_REG_FPEXC, 0x40000000u);

    uc_hook hook;
    uc_hook_add(g_uc, &hook, UC_HOOK_INTR, (void *)hook_intr, NULL, 1, 0);
    uc_hook_add(g_uc, &hook, UC_HOOK_MEM_UNMAPPED, (void *)hook_unmapped, NULL, 1, 0);

    set_reg(UC_ARM_REG_SP, GUEST_STACK_BASE + GUEST_STACK_SIZE - 64);
    gheap_init();
    return true;
}

void emu_shutdown(void) {
    if (g_uc) {
        uc_close(g_uc);
        g_uc = NULL;
    }
}

uint32_t emu_add_thunk(const char *name, import_fn fn) {
    if (g_thunk_count >= MAX_THUNKS) {
        return 0;
    }
    uint32_t index = g_thunk_count++;
    g_thunks[index].name = name;
    g_thunks[index].fn = fn;
    g_thunk_code[index * 2] = 0xef000000u | index; /* svc #index */
    g_thunk_code[index * 2 + 1] = 0xe12fff1eu;     /* bx lr */
    return GUEST_THUNK_BASE + index * 8;
}

const char *emu_thunk_name(uint32_t index) {
    return index < g_thunk_count ? g_thunks[index].name : NULL;
}

const char *current_import(void) {
    return g_current;
}

uint32_t arg(int index) {
    if (index < 4) {
        return g_args[index];
    }
    return *(uint32_t *)G2H(g_sp + (uint32_t)(index - 4) * 4u);
}

uint64_t arg64(int index) {
    return (uint64_t)arg(index) | ((uint64_t)arg(index + 1) << 32);
}

float argf(int index) {
    uint32_t bits = arg(index);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void ret(uint32_t value) {
    set_reg(UC_ARM_REG_R0, value);
}

void ret64(uint64_t value) {
    set_reg(UC_ARM_REG_R0, (uint32_t)value);
    set_reg(UC_ARM_REG_R1, (uint32_t)(value >> 32));
}

void retf(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    set_reg(UC_ARM_REG_R0, bits);
}

uint32_t guest_call(uint32_t fn, int argc, const uint32_t *argv) {
    if (!fn) {
        return 0;
    }
    uint32_t saved[CTX_REG_COUNT];
    void *ptrs[CTX_REG_COUNT];
    for (int i = 0; i < CTX_REG_COUNT; ++i) {
        ptrs[i] = &saved[i];
    }
    uc_reg_read_batch(g_uc, (int *)g_ctx_regs, ptrs, CTX_REG_COUNT);

    uint32_t sp = (saved[13] - 64u) & ~7u;
    int stack_args = argc > 4 ? argc - 4 : 0;
    sp -= (uint32_t)((stack_args * 4 + 7) & ~7);
    for (int i = 0; i < stack_args; ++i) {
        *(uint32_t *)G2H(sp + (uint32_t)i * 4u) = argv[4 + i];
    }
    for (int i = 0; i < 4 && i < argc; ++i) {
        set_reg(UC_ARM_REG_R0 + i, argv[i]);
    }
    set_reg(UC_ARM_REG_SP, sp);
    set_reg(UC_ARM_REG_LR, GUEST_RETURN_MAGIC);

    g_depth++;
    uc_err err = uc_emu_start(g_uc, fn, GUEST_RETURN_MAGIC, 0, 0);
    g_depth--;
    if (err != UC_ERR_OK) {
        log_error("guest call 0x%08x failed: %s", fn, uc_strerror(err));
        emu_dump_state("guest call");
        exit(1);
    }
    uint32_t result = reg(UC_ARM_REG_R0);
    uc_reg_write_batch(g_uc, (int *)g_ctx_regs, ptrs, CTX_REG_COUNT);
    return result;
}

uint32_t guest_call0(uint32_t fn) {
    return guest_call(fn, 0, NULL);
}

uint32_t guest_call2(uint32_t fn, uint32_t a, uint32_t b) {
    uint32_t argv[2] = {a, b};
    return guest_call(fn, 2, argv);
}

uint32_t guest_call3(uint32_t fn, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t argv[3] = {a, b, c};
    return guest_call(fn, 3, argv);
}

int emu_run(uint32_t entry) {
    set_reg(UC_ARM_REG_LR, GUEST_RETURN_MAGIC);
    uc_err err = uc_emu_start(g_uc, entry, GUEST_RETURN_MAGIC, 0, 0);
    if (err != UC_ERR_OK) {
        log_error("emulation stopped: %s", uc_strerror(err));
        emu_dump_state("entry");
        return 1;
    }
    return (int)reg(UC_ARM_REG_R0);
}

void emu_dump_state(const char *why) {
    static const char *names[] = {"r0", "r1", "r2",  "r3",  "r4", "r5", "r6", "r7", "r8",
                                  "r9", "r10", "r11", "r12", "sp", "lr", "pc", "cpsr"};
    fprintf(stderr, "---- %s (depth %d, import %s)\n", why, g_depth, g_current ? g_current : "-");
    for (int i = 0; i < CTX_REG_COUNT; ++i) {
        fprintf(stderr, "%5s=%08x%s", names[i], reg(g_ctx_regs[i]), (i % 6 == 5) ? "\n" : " ");
    }
    fprintf(stderr, "\n");
}

void *arena_alloc(size_t size) {
    uint32_t addr = (g_arena_next + 15u) & ~15u;
    if (size > GUEST_ARENA_BASE + GUEST_ARENA_SIZE - addr) {
        log_error("guest arena exhausted");
        abort();
    }
    g_arena_next = addr + (uint32_t)size;
    memset(G2H(addr), 0, size);
    return G2H(addr);
}

uint32_t arena_strdup(const char *text) {
    size_t len = strlen(text) + 1;
    char *copy = arena_alloc(len);
    memcpy(copy, text, len);
    return H2G(copy);
}

/* Top guest->host calls since the last report (CODBOZ_PERF), then resets the counters. */
void emu_perf_top(char *out, int size) {
    out[0] = 0;
    for (uint32_t i = 0; i < g_thunk_count && i < 8192; ++i) {
        if (g_perf_by_thunk[i] && strncmp(g_thunks[i].name, "s3eTimer", 8) == 0) {
            int n = (int)strlen(out);
            snprintf(out + n, (size_t)(size - n), "%s=%u ", g_thunks[i].name, g_perf_by_thunk[i]);
        }
    }
    for (int k = 0; k < 4; ++k) {
        uint32_t best = 0, bi = 0;
        for (uint32_t i = 0; i < g_thunk_count && i < 8192; ++i) {
            if (g_perf_by_thunk[i] > best) {
                best = g_perf_by_thunk[i];
                bi = i;
            }
        }
        if (!best) {
            break;
        }
        int n = (int)strlen(out);
        snprintf(out + n, (size_t)(size - n), "%s%s=%u", n ? " " : "", g_thunks[bi].name, best);
        g_perf_by_thunk[bi] = 0;
    }
    memset(g_perf_by_thunk, 0, sizeof(g_perf_by_thunk));
}
