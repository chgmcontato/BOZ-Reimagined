// Headless Dynarmic probe (experimental-dynarmic only; not part of the stable build).
//
// Loads boz.s3e with the port's own loader (src/loader.c), at the same identity mapping the
// Unicorn backend uses, then runs the entry point on Dynarmic's A32 JIT until the guest makes
// its N-th import call (SVC into the thunk page). No window, no SDL, no GL.
//
//   usage: dynarmic_probe build/game/original/assets/boz.s3e.unpacked [imports=1]
//
// Each import is "answered" with r0=0 so the probe can walk a little further than the first
// SVC; this is only to show the dispatch path, not a correct emulation.
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>

#include <sys/mman.h>

#include <array>
#include <cinttypes>
#include <optional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include "emu.h"
#include "loader.h"
}

using PageTable = std::array<std::uint8_t *, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>;

static std::unique_ptr<PageTable> g_pages = std::make_unique<PageTable>();
static std::vector<std::pair<uint32_t, uint32_t>> g_maps; // for munmap at exit (no leaks)
static std::vector<std::string> g_thunk_names;
static constexpr uint32_t kReturnSvc = 0x00ffffffu;

// --- the one host hook src/loader.c needs: same contract as emu.c's emu_map_identity ---
extern "C" bool emu_map_identity(uint32_t addr, uint32_t size) {
    size = (size + 0xfffu) & ~0xfffu;
    void *p = mmap(reinterpret_cast<void *>(uintptr_t{addr}), size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED || p != reinterpret_cast<void *>(uintptr_t{addr})) {
        if (p != MAP_FAILED) munmap(p, size);
        std::fprintf(stderr, "probe: cannot reserve 0x%08x+0x%x\n", addr, size);
        return false;
    }
    // Identity map: the page table entry for a guest page is that same host address.
    for (uint32_t off = 0; off < size; off += 0x1000u)
        (*g_pages)[(addr + off) >> 12] = static_cast<std::uint8_t *>(p) + off;
    g_maps.emplace_back(addr, size);
    return true;
}

static uint32_t resolve(const char *symbol) {
    uint32_t index = static_cast<uint32_t>(g_thunk_names.size());
    if (index * 8u + 8u > GUEST_THUNK_SIZE - 16u) return 0;
    g_thunk_names.emplace_back(symbol);
    auto *code = static_cast<uint32_t *>(G2H(GUEST_THUNK_BASE));
    code[index * 2] = 0xef000000u | index; // svc #index
    code[index * 2 + 1] = 0xe12fff1eu;     // bx lr
    return GUEST_THUNK_BASE + index * 8u;
}

class Env final : public Dynarmic::A32::UserCallbacks {
public:
    Dynarmic::A32::Jit *jit = nullptr;
    uint64_t ticks = 0;
    int imports_left = 1;
    bool faulted = false;

    // Only reached when the page table has no entry (unmapped): report it like hook_unmapped.
    template <typename T> T bad(const char *what, uint32_t va) {
        std::fprintf(stderr, "probe: unmapped %s at 0x%08x (pc=%08x)\n", what, va, jit->Regs()[15]);
        faulted = true;
        jit->HaltExecution();
        return T{};
    }
    std::uint8_t MemoryRead8(uint32_t va) override { return bad<std::uint8_t>("read8", va); }
    std::uint16_t MemoryRead16(uint32_t va) override { return bad<std::uint16_t>("read16", va); }
    std::uint32_t MemoryRead32(uint32_t va) override { return bad<std::uint32_t>("read32", va); }
    std::uint64_t MemoryRead64(uint32_t va) override { return bad<std::uint64_t>("read64", va); }
    void MemoryWrite8(uint32_t va, std::uint8_t) override { bad<int>("write8", va); }
    void MemoryWrite16(uint32_t va, std::uint16_t) override { bad<int>("write16", va); }
    void MemoryWrite32(uint32_t va, std::uint32_t) override { bad<int>("write32", va); }
    void MemoryWrite64(uint32_t va, std::uint64_t) override { bad<int>("write64", va); }
    std::optional<std::uint32_t> MemoryReadCode(uint32_t va) override {
        if (std::uint8_t *page = (*g_pages)[va >> 12]) {
            std::uint32_t w;
            std::memcpy(&w, page + (va & 0xfffu), 4);
            return w;
        }
        bad<int>("fetch", va);
        return std::nullopt;
    }

    void InterpreterFallback(uint32_t pc, size_t n) override {
        std::fprintf(stderr, "probe: interpreter fallback at %08x (%zu insns)\n", pc, n);
        faulted = true;
        jit->HaltExecution();
    }

    // The whole import dispatch lives here: this replaces emu.c's UC_HOOK_INTR hook_intr().
    void CallSVC(std::uint32_t swi) override {
        auto &r = jit->Regs();
        if (swi == kReturnSvc) { // guest returned to GUEST_RETURN_MAGIC
            std::printf("guest entry returned r0=%08x after %" PRIu64 " ticks\n", r[0], ticks);
            jit->HaltExecution();
            return;
        }
        const char *name = swi < g_thunk_names.size() ? g_thunk_names[swi].c_str() : "?";
        std::printf("SVC #%u %-32s r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x sp=%08x  [%" PRIu64
                    " ticks]\n",
                    swi, name, r[0], r[1], r[2], r[3], r[14], r[13], ticks);
        r[0] = 0; // ret(0): host stub
        if (--imports_left <= 0) jit->HaltExecution();
    }

    void ExceptionRaised(uint32_t pc, Dynarmic::A32::Exception e) override {
        using E = Dynarmic::A32::Exception;
        if (e == E::Yield || e == E::PreloadData || e == E::PreloadDataWithIntentToWrite ||
            e == E::PreloadInstruction || e == E::SendEvent || e == E::SendEventLocal ||
            e == E::WaitForEvent || e == E::WaitForInterrupt) {
            return; // hints: the Unicorn backend treats them as no-ops too
        }
        std::fprintf(stderr, "probe: exception %d at %08x\n", static_cast<int>(e), pc);
        faulted = true;
        jit->HaltExecution();
    }

    void AddTicks(std::uint64_t t) override { ticks += t; }
    std::uint64_t GetTicksRemaining() override { return 1'000'000; }
};

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s boz.s3e.unpacked [imports]\n", argv[0]);
        return 2;
    }
    // Same fixed regions emu_init() maps (stack, host arena, thunk page; heap left out: the
    // probe never reaches s3eMalloc with a real allocator).
    if (!emu_map_identity(GUEST_STACK_BASE, GUEST_STACK_SIZE) ||
        !emu_map_identity(GUEST_THUNK_BASE, GUEST_THUNK_SIZE))
        return 1;
    *static_cast<uint32_t *>(G2H(GUEST_RETURN_MAGIC)) = 0xef000000u | kReturnSvc;

    s3e_image image{};
    if (!s3e_image_load(argv[1], &image) || !s3e_image_map(&image, resolve)) {
        std::fprintf(stderr, "probe: could not load %s\n", argv[1]);
        return 1;
    }
    std::printf("boz.s3e mapped at 0x%08x, entry 0x%08x, %zu imports -> %zu thunks\n", image.base,
                image.entry, image.symbol_count, g_thunk_names.size());

    Env env;
    env.imports_left = argc > 2 ? std::atoi(argv[2]) : 1;
    Dynarmic::A32::UserConfig cfg{};
    cfg.callbacks = &env;
    cfg.arch_version = Dynarmic::A32::ArchVersion::v7; // boz.s3e targets ARMv7-A + VFP/NEON
    cfg.page_table = g_pages.get();
    cfg.absolute_offset_page_table = false; // page_table[va>>12][va & 0xfff]
    // fastmem_pointer = 0 would also work (identity map, guest VA == host VA); see the report.
    Dynarmic::A32::Jit jit{cfg};
    env.jit = &jit;

    auto &r = jit.Regs();
    r[13] = GUEST_STACK_BASE + GUEST_STACK_SIZE - 64;
    r[14] = GUEST_RETURN_MAGIC;
    r[15] = image.entry & ~1u;
    jit.SetCpsr(0x10u | ((image.entry & 1u) ? 0x20u : 0u)); // USR mode, Thumb if bit 0
    jit.SetFpscr(0x03000000u);                               // default-NaN + flush-to-zero

    std::uint32_t first = 0;
    std::memcpy(&first, G2H(image.entry & ~1u), 4);
    std::printf("first instruction @%08x: %08x\n", image.entry & ~1u, first);

    jit.Run();
    std::printf("halted: pc=%08x cpsr=%08x ticks=%" PRIu64 "%s\n", r[15], jit.Cpsr(), env.ticks,
                env.faulted ? " (fault)" : "");

    s3e_image_free(&image);
    for (auto [a, s] : g_maps) munmap(G2H(a), s);
    return env.faulted ? 1 : 0;
}
