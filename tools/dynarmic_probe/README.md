# Dynarmic: avaliação e protótipo (branch `experimental-dynarmic`)

O upstream `merryhime/dynarmic` saiu do GitHub. Fork usado: `azahar-emu/dynarmic`
(a466015, set/2026). O A32 (ARMv4T..v8, ARM+Thumb, VFP/NEON) é suportado e é o que o Citra/Azahar usam.

## Unicorn hoje (`src/emu.c`) → Dynarmic

| Unicorn | Uso no port | Dynarmic A32 |
|---|---|---|
| `uc_open` + `uc_ctl_set_cpu_model(A15)` | 1x | `Jit{UserConfig{.arch_version = v7}}` |
| `uc_mem_map_ptr` (identity, low 4 GiB) | toda região | `page_table[va>>12] = va & ~0xfff` (idem) **ou** `fastmem_pointer = 0` |
| `UC_HOOK_INTR` → `hook_intr` (SVC #n no thunk) | todos os imports | `UserCallbacks::CallSVC(n)`: mesmo corpo |
| `UC_HOOK_MEM_UNMAPPED` | diagnóstico | callbacks `MemoryRead*/Write*` (só chamados fora da page table) |
| `uc_reg_read/write(_batch)` | `arg()`, `ret()`, `guest_call` | `jit.Regs()[i]`, `ExtRegs()`, `Cpsr()/SetCpsr()` |
| `uc_emu_start(fn, MAGIC)` | `emu_run`, `guest_call` | `Regs()[15]=fn; LR=MAGIC; Run()`; `svc` em MAGIC → `HaltExecution()` |
| `uc_emu_stop` | faltas | `HaltExecution()` |
| CPACR/FPEXC (C1_C0_2) | liga VFP | desnecessário (VFP sempre ativo) |
| `CODBOZ_TCG_MB`, `CODBOZ_TLB_VIRTUAL`, `CODBOZ_HEAP_RW` | tuning | não existem: somem |

## O que fica / o que muda
- **Fica igual:** `loader.c`, `gheap.c`, todos os `imp_*` (usam só `arg()/ret()/G2H`), o modelo
  de thunks (`svc #idx; bx lr`), o layout de `emu.h` (`GUEST_*`).
- **Reescrever:** só `emu.c` → `emu_dynarmic.cpp` (~400 linhas) que mantém a API C de `emu.h`.
- **Atenção:**
  - `guest_call` reentrante (import que chama o guest, que chama import...): no Dynarmic, `Run()`
    não pode ser chamado de dentro de um callback. Solução: salvar `Regs/ExtRegs/Cpsr/Fpscr`,
    **retornar do callback com halt**, e montar o loop de chamada aninhada fora do `Run()`, ou
    usar um segundo `Jit` (mais simples; a cache de código não é compartilhada).
  - Código gerado em runtime ou patches (`patch_frame_cap`, `reimagined_patch_waves`): chamar
    `InvalidateCacheRange` depois de escrever no código do guest.
  - Exclusivos (`ldrex/strex`): implementar `MemoryWriteExclusive*` (single thread: só comparar e gravar).
- **Fastmem:** com o mapeamento identidade, `fastmem_pointer = 0` faz o JIT emitir acesso direto
  `[guest_va]`. Isso é o caminho mais rápido, mas qualquer endereço baixo do host (inclusive a imagem
  `-no-pie`) fica alcançável pelo guest; a page table só deixa passar páginas mapeadas. Recomendo
  page table primeiro (correção) e medir fastmem depois.

## Protótipo
`probe.cpp` carrega o `boz.s3e` com o `loader.c` real, executa o entry no JIT e para no N-ésimo
import (cada um responde r0=0). Build: ver o cabeçalho de `CMakeLists.txt`.
