// Dynarmic A32 backend: same API as emu.c (see emu.h), selected with `make EMU=dynarmic`.
//
// Memory: guest addresses are identity-mapped in the host (emu_map_identity), and Dynarmic reads
// them through a 4 KiB page table whose entry for a mapped page is that page's own host address.
// Unmapped pages stay null, so stray accesses reach the callbacks below and are reported like
// Unicorn's UC_HOOK_MEM_UNMAPPED. CODBOZ_DYN_FASTMEM=1 instead points fastmem at host address 0
// (guest VA == host VA): faster, but the guest can then touch any low host page.
//
// Imports: thunk `svc #index; bx lr` -> CallSVC(index) -> g_thunks[index].fn(), as in hook_intr.
//
// Nested guest calls (imports calling guest callbacks): Jit::Run is not reentrant, so each nesting
// level runs on its own Jit. The caller's Jit keeps its registers untouched while the callee runs.
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>
#include <dynarmic/interface/exclusive_monitor.h>

#include <sys/mman.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>

extern "C" {
#include "emu.h"
#include "log.h"
uint64_t monotonic_us(void);
}

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

namespace {

using Dynarmic::A32::UserConfig;
using PageTable = std::array<std::uint8_t *, UserConfig::NUM_PAGE_TABLE_ENTRIES>;

constexpr uint32_t MAX_THUNKS = (GUEST_THUNK_SIZE - 16) / 8;
constexpr uint32_t RETURN_SVC = 0x00ffffffu; // the svc at GUEST_RETURN_MAGIC
constexpr int MAX_DEPTH = 16;

struct Thunk {
    const char *name;
    import_fn fn;
};

PageTable *g_pages;
Thunk g_thunks[MAX_THUNKS];
uint32_t g_thunk_count;
uint32_t *g_thunk_code;

// State of the import being serviced; saved across nested guest calls (as in emu.c).
uint32_t g_args[4];
uint32_t g_sp;
const char *g_current;
int g_depth;   // guest_call nesting, reported by emu_dump_state
int g_level;   // Jits currently inside Run(): g_jits[g_level - 1] is the one in a callback
bool g_fault;  // a fatal guest error halted the current Run
int g_trace;
uint32_t g_perf_by_thunk[8192];
uint32_t g_arena_next = GUEST_ARENA_BASE;

inline std::uint8_t *page(uint32_t va) { return (*g_pages)[va >> 12]; }

class Env;
std::unique_ptr<Dynarmic::ExclusiveMonitor> g_monitor;
std::unique_ptr<Env> g_envs[MAX_DEPTH];

Dynarmic::A32::Jit &current_jit();

class Env final : public Dynarmic::A32::UserCallbacks {
public:
    std::unique_ptr<Dynarmic::A32::Jit> jit;

    void fault(const char *what, uint32_t va, int size) {
        if (g_fault) return;
        log_error("invalid %s at 0x%08x (size %d) during %s", what, va, size,
                  g_current ? g_current : "guest code");
        g_fault = true;
        emu_dump_state("memory fault");
        jit->HaltExecution();
    }

    // Reached for unmapped pages, for exclusive accesses (through the monitor) and for accesses
    // that straddle a page boundary.
    template <typename T> T read(uint32_t va) {
        if (page(va) && page(va + sizeof(T) - 1)) {
            T v;
            std::memcpy(&v, G2H(va), sizeof v);
            return v;
        }
        fault("read", va, sizeof(T));
        return T{};
    }
    template <typename T> void write(uint32_t va, T v) {
        if (page(va) && page(va + sizeof(T) - 1)) {
            std::memcpy(G2H(va), &v, sizeof v);
            return;
        }
        fault("write", va, sizeof(T));
    }
    template <typename T> bool write_exclusive(uint32_t va, T v, T expected) {
        // Single guest thread: a plain compare-and-store is enough.
        if (read<T>(va) != expected) return false;
        write<T>(va, v);
        return true;
    }

    std::optional<std::uint32_t> MemoryReadCode(uint32_t va) override {
        if (page(va)) {
            std::uint32_t w;
            std::memcpy(&w, G2H(va), 4);
            return w;
        }
        fault("fetch", va, 4);
        return std::nullopt;
    }
    std::uint8_t MemoryRead8(uint32_t va) override { return read<std::uint8_t>(va); }
    std::uint16_t MemoryRead16(uint32_t va) override { return read<std::uint16_t>(va); }
    std::uint32_t MemoryRead32(uint32_t va) override { return read<std::uint32_t>(va); }
    std::uint64_t MemoryRead64(uint32_t va) override { return read<std::uint64_t>(va); }
    void MemoryWrite8(uint32_t va, std::uint8_t v) override { write(va, v); }
    void MemoryWrite16(uint32_t va, std::uint16_t v) override { write(va, v); }
    void MemoryWrite32(uint32_t va, std::uint32_t v) override { write(va, v); }
    void MemoryWrite64(uint32_t va, std::uint64_t v) override { write(va, v); }
    bool MemoryWriteExclusive8(uint32_t va, std::uint8_t v, std::uint8_t e) override {
        return write_exclusive(va, v, e);
    }
    bool MemoryWriteExclusive16(uint32_t va, std::uint16_t v, std::uint16_t e) override {
        return write_exclusive(va, v, e);
    }
    bool MemoryWriteExclusive32(uint32_t va, std::uint32_t v, std::uint32_t e) override {
        return write_exclusive(va, v, e);
    }
    bool MemoryWriteExclusive64(uint32_t va, std::uint64_t v, std::uint64_t e) override {
        return write_exclusive(va, v, e);
    }

    void InterpreterFallback(uint32_t pc, size_t n) override {
        log_error("dynarmic: no interpreter fallback (pc=%08x, %zu instructions)", pc, n);
        g_fault = true;
        emu_dump_state("interpreter fallback");
        jit->HaltExecution();
    }

    void CallSVC(std::uint32_t swi) override {
        auto &r = jit->Regs();
        if (swi == RETURN_SVC) { // reached GUEST_RETURN_MAGIC: the host-initiated call returned
            jit->HaltExecution();
            return;
        }
        // r[15] is already the instruction after the svc, like Unicorn's PC in hook_intr.
        if (swi >= g_thunk_count || r[15] - 4 < GUEST_THUNK_BASE) {
            g_fault = true;
            emu_dump_state("stray SVC");
            jit->HaltExecution();
            return;
        }

        uint32_t saved_args[4];
        uint32_t saved_sp = g_sp;
        const char *saved_current = g_current;
        std::memcpy(saved_args, g_args, sizeof saved_args);

        std::memcpy(g_args, r.data(), sizeof g_args);
        g_sp = r[13];
        g_current = g_thunks[swi].name;
        if (g_trace) {
            std::fprintf(stderr, "[trace] %s(%08x, %08x, %08x, %08x) lr=%08x\n", g_current,
                         g_args[0], g_args[1], g_args[2], g_args[3], r[14]);
        }
        static int depth;
        uint64_t t0 = depth == 0 ? monotonic_us() : 0;
        depth++;
        g_thunks[swi].fn();
        depth--;
        g_perf.calls++;
        g_perf_by_thunk[swi]++;
        if (depth == 0) g_perf.host_us += monotonic_us() - t0;

        std::memcpy(g_args, saved_args, sizeof saved_args);
        g_sp = saved_sp;
        g_current = saved_current;
    }

    void ExceptionRaised(uint32_t pc, Dynarmic::A32::Exception e) override {
        using E = Dynarmic::A32::Exception;
        switch (e) {
        case E::SendEvent: case E::SendEventLocal: case E::WaitForInterrupt:
        case E::WaitForEvent: case E::Yield: case E::PreloadData:
        case E::PreloadDataWithIntentToWrite: case E::PreloadInstruction:
            return; // hints: no-ops, as under Unicorn
        default:
            break;
        }
        log_error("dynarmic: guest exception %d at %08x during %s", static_cast<int>(e), pc,
                  g_current ? g_current : "guest code");
        g_fault = true;
        emu_dump_state("unexpected CPU exception");
        jit->HaltExecution();
    }

    void AddTicks(std::uint64_t) override {}
    std::uint64_t GetTicksRemaining() override { return 1u << 30; }
};

Env &env_at(int level) {
    if (level >= MAX_DEPTH) {
        log_error("dynarmic: guest calls nested deeper than %d", MAX_DEPTH);
        std::abort();
    }
    if (!g_envs[level]) {
        auto env = std::make_unique<Env>();
        UserConfig cfg{};
        cfg.callbacks = env.get();
        cfg.arch_version = Dynarmic::A32::ArchVersion::v7; // Cortex-A15 class, as under Unicorn
        cfg.global_monitor = g_monitor.get();
        cfg.processor_id = 0;
        cfg.page_table = g_pages;
        cfg.absolute_offset_page_table = false; // host = page_table[va >> 12] + (va & 0xfff)
        if (const char *fm = std::getenv("CODBOZ_DYN_FASTMEM"); fm && std::strcmp(fm, "1") == 0)
            cfg.fastmem_pointer = 0;
        cfg.enable_cycle_counting = false; // run until halted
        cfg.code_cache_size = level == 0 ? 256u << 20 : 32u << 20;
        env->jit = std::make_unique<Dynarmic::A32::Jit>(cfg);
        if (level == 0) {
            log_info("dynarmic: A32 JIT, %s", cfg.fastmem_pointer ? "fastmem (identity)" : "page table");
        }
        g_envs[level] = std::move(env);
    }
    return *g_envs[level];
}

Dynarmic::A32::Jit &current_jit() {
    // Inside an import: the Jit that raised the SVC. Outside any Run: the main Jit.
    return *env_at(g_level > 0 ? g_level - 1 : 0).jit;
}

// Run `jit` from `pc` until it returns to GUEST_RETURN_MAGIC (or faults).
bool run_until_return(Env &env, uint32_t pc) {
    auto &jit = *env.jit;
    jit.Regs()[14] = GUEST_RETURN_MAGIC;
    jit.Regs()[15] = pc & ~1u;
    jit.SetCpsr((jit.Cpsr() & ~0x3fu) | 0x10u | ((pc & 1u) ? 0x20u : 0u)); // USR, T from bit 0
    g_level++;
    for (;;) {
        jit.Run();
        if (g_fault) break;
        if (jit.Regs()[15] == GUEST_RETURN_MAGIC + 4) break; // halted by RETURN_SVC
        // Any other halt (cache invalidation request): keep going.
    }
    g_level--;
    return !g_fault;
}

} // namespace

extern "C" {
struct perf_counters g_perf;
}

extern "C" bool emu_map_identity(uint32_t addr, uint32_t size) {
    size = (size + 0xfffu) & ~0xfffu;
    void *p = mmap(G2H(addr), size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED || p != G2H(addr)) {
        if (p != MAP_FAILED) munmap(p, size);
        log_error("unable to reserve guest range 0x%08x+0x%x in the host", addr, size);
        return false;
    }
    if (!g_pages) {
        g_pages = new PageTable{};
    }
    // Identity map: a page's entry is its own host address; null = unmapped.
    for (uint32_t off = 0; off < size; off += 0x1000u) {
        (*g_pages)[(addr + off) >> 12] = static_cast<std::uint8_t *>(p) + off;
    }
    // Code may already be cached for a range that is (re)mapped.
    for (auto &env : g_envs) {
        if (env) env->jit->InvalidateCacheRange(addr, size);
    }
    return true;
}

extern "C" bool emu_init(void) {
    g_trace = std::getenv("CODBOZ_TRACE") != nullptr;
    if (!emu_map_identity(GUEST_STACK_BASE, GUEST_STACK_SIZE) ||
        !emu_map_identity(GUEST_ARENA_BASE, GUEST_ARENA_SIZE) ||
        !emu_map_identity(GUEST_THUNK_BASE, GUEST_THUNK_SIZE) ||
        !emu_map_identity(GUEST_HEAP_BASE, GUEST_HEAP_SIZE)) {
        return false;
    }
    g_monitor = std::make_unique<Dynarmic::ExclusiveMonitor>(1);
    g_thunk_code = static_cast<uint32_t *>(G2H(GUEST_THUNK_BASE));
    *static_cast<uint32_t *>(G2H(GUEST_RETURN_MAGIC)) = 0xef000000u | RETURN_SVC;

    auto &jit = *env_at(0).jit;
    jit.SetCpsr(0x10u);
    jit.SetFpscr(0); // VFP/NEON are always enabled in Dynarmic (no CPACR/FPEXC)
    jit.Regs()[13] = GUEST_STACK_BASE + GUEST_STACK_SIZE - 64;
    gheap_init();
    return true;
}

extern "C" void emu_shutdown(void) {
    for (auto &env : g_envs) env.reset();
    g_monitor.reset();
}

extern "C" uint32_t emu_add_thunk(const char *name, import_fn fn) {
    if (g_thunk_count >= MAX_THUNKS) return 0;
    uint32_t index = g_thunk_count++;
    g_thunks[index] = {name, fn};
    g_thunk_code[index * 2] = 0xef000000u | index; // svc #index
    g_thunk_code[index * 2 + 1] = 0xe12fff1eu;     // bx lr
    for (auto &env : g_envs) {
        if (env) env->jit->InvalidateCacheRange(GUEST_THUNK_BASE + index * 8, 8);
    }
    return GUEST_THUNK_BASE + index * 8;
}

extern "C" const char *emu_thunk_name(uint32_t index) {
    return index < g_thunk_count ? g_thunks[index].name : nullptr;
}

extern "C" const char *current_import(void) { return g_current; }

extern "C" uint32_t arg(int index) {
    if (index < 4) return g_args[index];
    return *static_cast<uint32_t *>(G2H(g_sp + static_cast<uint32_t>(index - 4) * 4u));
}

extern "C" uint64_t arg64(int index) {
    return static_cast<uint64_t>(arg(index)) | (static_cast<uint64_t>(arg(index + 1)) << 32);
}

extern "C" float argf(int index) {
    uint32_t bits = arg(index);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

extern "C" void ret(uint32_t value) { current_jit().Regs()[0] = value; }

extern "C" void ret64(uint64_t value) {
    auto &r = current_jit().Regs();
    r[0] = static_cast<uint32_t>(value);
    r[1] = static_cast<uint32_t>(value >> 32);
}

extern "C" void retf(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    current_jit().Regs()[0] = bits;
}

extern "C" uint32_t guest_call(uint32_t fn, int argc, const uint32_t *argv) {
    if (!fn) return 0;
    // Callee runs on the next Jit up; the caller's registers stay where they are.
    Dynarmic::A32::Jit &parent = current_jit();
    Env &env = env_at(g_level);
    auto &jit = *env.jit;
    const auto saved_regs = jit.Regs(); // only matters when g_level == 0 (no Run active)
    const uint32_t saved_cpsr = jit.Cpsr();

    if (&jit != &parent) {
        jit.Regs() = parent.Regs();
        jit.SetFpscr(parent.Fpscr());
    }
    uint32_t sp = (parent.Regs()[13] - 64u) & ~7u;
    int stack_args = argc > 4 ? argc - 4 : 0;
    sp -= static_cast<uint32_t>((stack_args * 4 + 7) & ~7);
    for (int i = 0; i < stack_args; ++i) {
        *static_cast<uint32_t *>(G2H(sp + static_cast<uint32_t>(i) * 4u)) = argv[4 + i];
    }
    for (int i = 0; i < 4 && i < argc; ++i) jit.Regs()[i] = argv[i];
    jit.Regs()[13] = sp;

    g_depth++;
    bool ok = run_until_return(env, fn);
    g_depth--;
    if (!ok) {
        log_error("guest call 0x%08x failed", fn);
        std::exit(1);
    }
    uint32_t result = jit.Regs()[0];
    jit.Regs() = saved_regs;
    jit.SetCpsr(saved_cpsr);
    return result;
}

extern "C" uint32_t guest_call0(uint32_t fn) { return guest_call(fn, 0, nullptr); }

extern "C" uint32_t guest_call2(uint32_t fn, uint32_t a, uint32_t b) {
    uint32_t argv[2] = {a, b};
    return guest_call(fn, 2, argv);
}

extern "C" uint32_t guest_call3(uint32_t fn, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t argv[3] = {a, b, c};
    return guest_call(fn, 3, argv);
}

extern "C" int emu_run(uint32_t entry) {
    Env &env = env_at(0);
    if (!run_until_return(env, entry)) {
        log_error("emulation stopped");
        return 1;
    }
    return static_cast<int>(env.jit->Regs()[0]);
}

extern "C" void emu_dump_state(const char *why) {
    static const char *names[] = {"r0", "r1", "r2",  "r3",  "r4", "r5", "r6", "r7", "r8",
                                  "r9", "r10", "r11", "r12", "sp", "lr", "pc", "cpsr"};
    std::fprintf(stderr, "---- %s (depth %d, import %s)\n", why, g_depth,
                 g_current ? g_current : "-");
    auto &jit = current_jit();
    for (int i = 0; i < 17; ++i) {
        uint32_t v = i < 16 ? jit.Regs()[i] : jit.Cpsr();
        std::fprintf(stderr, "%5s=%08x%s", names[i], v, (i % 6 == 5) ? "\n" : " ");
    }
    std::fprintf(stderr, "\n");
}

extern "C" void *arena_alloc(size_t size) {
    uint32_t addr = (g_arena_next + 15u) & ~15u;
    if (size > GUEST_ARENA_BASE + GUEST_ARENA_SIZE - addr) {
        log_error("guest arena exhausted");
        std::abort();
    }
    g_arena_next = addr + static_cast<uint32_t>(size);
    std::memset(G2H(addr), 0, size);
    return G2H(addr);
}

extern "C" uint32_t arena_strdup(const char *text) {
    size_t len = std::strlen(text) + 1;
    auto *copy = static_cast<char *>(arena_alloc(len));
    std::memcpy(copy, text, len);
    return H2G(copy);
}

// Top guest->host calls since the last report (CODBOZ_PERF), then resets the counters.
extern "C" void emu_perf_top(char *out, int size) {
    uint32_t *counts = g_perf_by_thunk;
    out[0] = 0;
    for (uint32_t i = 0; i < g_thunk_count && i < 8192; ++i) {
        if (counts[i] && std::strncmp(g_thunks[i].name, "s3eTimer", 8) == 0) {
            int n = static_cast<int>(std::strlen(out));
            std::snprintf(out + n, static_cast<size_t>(size - n), "%s=%u ", g_thunks[i].name, counts[i]);
        }
    }
    for (int k = 0; k < 4; ++k) {
        uint32_t best = 0, bi = 0;
        for (uint32_t i = 0; i < g_thunk_count && i < 8192; ++i) {
            if (counts[i] > best) {
                best = counts[i];
                bi = i;
            }
        }
        if (!best) break;
        int n = static_cast<int>(std::strlen(out));
        std::snprintf(out + n, static_cast<size_t>(size - n), "%s%s=%u", n ? " " : "",
                      g_thunks[bi].name, best);
        counts[bi] = 0;
    }
    std::memset(counts, 0, sizeof g_perf_by_thunk);
}
