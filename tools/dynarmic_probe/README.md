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

## Backend completo: `src/emu_dynarmic.cpp` (2026-11-01)

`make EMU=dynarmic CC=gcc CXX=g++` gera `build-dynarmic/codboz-pc` (o Unicorn segue padrão em
`build/codboz-pc`). Rodar com `build-dynarmic/codboz-pc --data build/game`.
Dynarmic é submódulo em `third_party/dynarmic` (azahar-emu, a466015). Precisa de headers do Boost
(`BOOST_INCLUDE_DIR=...`).

Decisões:
- `guest_call` aninhado: um `Jit` por nível (até 16), porque `Jit::Run` não é reentrante. O
  estado do chamador fica intacto no Jit dele; nada a salvar/restaurar.
- `ExclusiveMonitor` de 1 processador (obrigatório para `ldrex/strex` no x64 backend).
- Page table identidade (padrão); `CODBOZ_DYN_FASTMEM=1` liga fastmem com base 0.
- Retorno ao host: `svc #0xffffff` em `GUEST_RETURN_MAGIC` → `HaltExecution`.

Benchmark (Ascension, rodada 1, mesma rota `CODBOZ_SCRIPT`, ~44 s de gameplay, Radeon 610M,
amostras de 1 s só com `active=1 state=gameplay`, emu ponderado por frame):

| Backend | vsync | FPS médio | emu ms/frame |
|---|---|---:|---:|
| Unicorn 2.1.4 (TCG 2 GiB) | on | 27.9 | 35.1 |
| Dynarmic (page table) | on | 59.3 (teto do vsync) | 5.7 |
| Unicorn 2.1.4 | off | 28.3 | 34.7 |
| Dynarmic (page table) | off | 175.8 | 4.7 |
| Dynarmic (fastmem) | off | 203.4 | 4.0 |

Call of the Dead (rodada 1, mesma metodologia, sem vsync):

| Backend | FPS médio | emu ms/frame |
|---|---:|---:|
| Unicorn 2.1.4 | 37.8 | 25.9 |
| Dynarmic (page table) | 211.2 | 3.7 |
| Dynarmic + `CODBOZ_DYN_CODE_WATCH=1` | 197.7 | 4.0 |

Código auto-modificável: `CODBOZ_DYN_CODE_WATCH=1` tira da page table do JIT as páginas de onde ele
buscou instruções; escritas nelas passam pelos callbacks e invalidam o código se atingirem uma
palavra já traduzida. Em COTD (Reimagined), no modo Original e no modo iOS: **zero** escritas em
código traduzido (as escritas em 0x4a3db6xx são dados que dividem a página com código). Ou seja,
a limitação do Dynarmic frente ao Unicorn não afeta este jogo.

Coprocessadores: CP0–CP15 (menos CP10/11, VFP) viram registradores simples; CDP/LDC/STC viram
no-op registrado no log. Nenhum acesso aparece nos jogos testados; existem para não abortar.

Pendências: co-op não testado com o Dynarmic; checar a única diferença vista (uma corrida com
vsync terminou em game over antes do Esc, coerente com o jogo a 60 fps em vez de 28).
