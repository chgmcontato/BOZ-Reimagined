# BOZ Reimagined Mobile (Android ARM64): Etapa 2, prova de viabilidade

O runtime do PC (`src/`) compilado para Android ARM64, rodando o **mesmo `boz.s3e` ARM32 do
Reimagined TV** no Dynarmic. O objetivo desta etapa é chegar ao menu principal no Galaxy S24 FE.
Menus touch, HUD de toque, controle e co-op ficam para as etapas seguintes.

> **Estado: Etapa 2 ABERTA.** No S24 FE, o primeiro APK instalou e abriu, mas os shaders falharam
> (ver [Primeiro boot no S24 FE](#primeiro-boot-no-s24-fe)). A correção chega à partida Solo no Mesa
> OpenGL ES, mas ainda não foi validada no aparelho. A Etapa 2 só fecha quando o S24 FE mostrar o
> menu principal e carregar uma partida Solo real.

## Como está organizado

```
mobile/
  native/CMakeLists.txt   runtime + Dynarmic + SDL2 → libmain.so (Android) ou boz-runtime (teste)
  android/                projeto Gradle: SetupActivity (escolhe os arquivos), BozActivity (SDL)
  collect-logs.sh         coleta de logs depois de um teste
src/
  platform_android.c      log → logcat/arquivo, SDL_main com pilha de 16 MiB, diagnóstico de memória
  gles_compat.c           desenho do runtime (ui.c) em OpenGL ES 3
```

O PC não usa nada disso. As mudanças em arquivos compartilhados ficam atrás de `CODBOZ_GUEST_BASE`,
`CODBOZ_GLES` ou `__ANDROID__`. No build do PC, a saída do pré-processador de todo o `src/` é igual
à de antes, a não ser por uma constante nova em `emu.h` (`GUEST_LOWEST_ADDR`). O launcher do PC
renderiza igual pixel a pixel.

## Diagnóstico

### Memória: o mapeamento 1:1 foi substituído

No PC, o endereço de 32 bits do jogo é o próprio endereço do processo. As regiões ficam de
`0x40000000` a `0x90000000` (pilha, arena, thunks, imagem em `0x4a000000`, heap de 1 GiB em
`0x50000000`), e o executável é linkado com `-no-pie`. No Android isso não é seguro: o processo
nasce do zygote com o ART já carregado, e o heap e a boot image do ART ficam abaixo de 4 GiB.

Solução (`CODBOZ_GUEST_BASE`, só no Android):

- `emu_init` reserva 4 GiB de espaço de endereços (`PROT_NONE`, `MAP_NORESERVE`) em qualquer lugar,
  e `g_guest_base` aponta para o início. Cada região do jogo é mapeada dentro dessa reserva.
- `G2H(a) = g_guest_base + a` e `H2G` faz o inverso. Valores abaixo de `0x40000000` passam sem
  mudança, exatamente como no mapa 1:1. São NULL e offsets de VBO (`glVertexAttribPointer`,
  `glDrawElements` com buffer ligado), e nada do jogo é mapeado ali.
- O Dynarmic já lia a memória por uma tabela de páginas (endereço do host por página de 4 KiB), que
  só passa a apontar para dentro da reserva. Com fastmem, o ponteiro vira `g_guest_base`.
- Revisão dos usos: `H2G` só recebe memória da arena ou do heap do jogo (15 usos conferidos).
  Ponteiros de GL passam por `G2H` (gerado em `gl_thunks.c`), e nenhum ponteiro do host é entregue
  ao jogo.
- Páginas de 16 KiB: todas as regiões começam alinhadas a 1 MiB e o runtime não usa `mprotect`.

No primeiro boot, o log mostra o que ocupava os 4 GiB baixos e se algo caía na faixa do PC
(`memory: ... inside 0x40000000-0x90000000`). É a medição real do conflito no S24 FE.

### Dynarmic no ARM64

- O submódulo é o fork do Azahar, com backend `arm64` que traduz A32. O Azahar (emulador de 3DS,
  CPU A32) usa esse mesmo fork em Android ARM64.
- **Achado:** o backend arm64 exige `code_cache_size <= 128 MiB` (alcance dos branches) e aborta
  com mais. O PC pedia 256 MiB; em hosts aarch64 passou a pedir 128 MiB.
- Teste `tools/test_guest_memory.c`: código ARM e Thumb de verdade (import por SVC com argumento
  na pilha, chamada aninhada guest → host → guest em outro Jit, `ldrex/strex`, VFP, 20 000
  chamadas), nos dois mapas e com ou sem fastmem. Passou em x86-64 e em aarch64 (qemu-user).
- A cache de código usa memória RWX anônima, como os outros emuladores com JIT no Android. Não foi
  medido no aparelho.

### Renderização: OpenGL ES

- O jogo é OpenGL ES 2.0. É uma hipótese forte, a confirmar na linha
  `game requested an OpenGL ES N context` do log: as otimizações que levaram o PC de 9 para 43 FPS
  caem em `glUseProgram` e `glVertexAttribPointer`, que são API do ES2.
- No Android, o runtime cria um contexto **ES 3.0** pelo SDL. As chamadas e os shaders GLSL ES do
  jogo vão direto ao driver, sem a tradução para GLSL 1.20 que o PC faz. ES 3 também dá ao runtime
  a superfície lógica 1280×720 (renderbuffers RGBA8 e DEPTH24_STENCIL8, `glBlitFramebuffer`), e o
  ETC1 é enviado como ETC2, que o ES 3 decodifica.
- O desenho do próprio runtime (`ui.c`: launcher, menus Reimagined, overlays) usa GL de desktop
  (`glBegin`, matrizes, `glPushAttrib`). `src/gles_compat.c` implementa só o que o `ui.c` usa
  (quads, fans, triângulos, matrizes, salvar e restaurar estado e atributos 0 a 2) com um shader
  pequeno. Rodando sobre o Mesa em ES 3.2, o launcher sai **igual ao do PC pixel a pixel**.
- SDL2 2.30.9 no Android: `SDLActivity` + `libSDL2.so`. O `main()` do PC vira `boz_main` e roda
  numa thread com 16 MiB de pilha (a `SDLThread` tem a pilha padrão do Java, cerca de 1 MiB).

## Primeiro boot no S24 FE

Log de 2026-11-03 (Android 16, ANGLE sobre Vulkan, Samsung Xclipse 940), confrontado com o código e
reproduzido no Mesa com os dados reais do jogo.

| Sintoma no log | Causa confirmada |
|---|---|
| `glShaderSource with 165 strings`, `'if': unexpected end of file` | O caminho GLES tinha uma tabela fixa de 64 strings. O jogo manda uma string por linha (165 e 182), então o shader chegava cortado dentro de um `#if`. Bug nosso, só do Android: o PC sempre juntou tudo num buffer. O log não aponta para o driver nem para o ANGLE. |
| `Vertex shader is not compiled` | Consequência direta do corte. |
| `GL error 0x0502` | No A/B com o corte de volta, o `KHR_debug` aponta `glUseProgram`, `glGetAttribLocation` e `glGetUniformLocation` em programas não vinculados. Sem o corte: nenhum erro desse tipo até a partida, no Mesa. |
| `glVertexAttribPointer guest/s 0` | O jogo não desenhava com atributos porque não tinha programa válido. Com a correção, cerca de 2000/s. |
| `game requested an OpenGL ES 5 context` | Inofensivo, igual no PC. O jogo chama `s3eGLGetInt(0)` (em `0x4a2c766c`) e calcula `major = v >> 8`. Este runtime devolve a largura ali: 1280 = `0x500`, logo "5". O valor só alimenta `g_es_version >= 2`; o contexto real é o ES 3 criado pelo SDL. O comportamento foi mantido; agora a linha do log explica isso. |

Os shaders `ReplaceGXShaderVertex` e `ReplaceGXShaderFragment` são do próprio jogo: GLSL ES 1.00, sem
`#version` nem `#extension`, com o mesmo corpo e `#define`s diferentes por material. Não precisam de
conversão no ES: vão ao driver como estão, só unidos. O PC continua convertendo para GLSL 1.20.

**Outro erro, não relacionado:** ao entrar na partida, `glCompressedTexImage2D` recusa
`GL_PALETTE8_RGB5_A1_OES` (`0x8b99`). O formato também aparece no log do PC. Fica registrado para
investigar; a partida renderiza no Mesa mesmo assim.

**Diagnóstico novo:** cada shader é registrado no log (strings, bytes, linhas, hash) e gravado inteiro
em `<dados>/shader-dump/<hash>.glsl`. O limite é por arquivos e bytes totais; um shader acima dele não é
gravado, nunca é cortado. Erros de GL saem na hora com o texto do driver e a chamada do jogo
(`GL debug: in glUseProgram, frame N: ...`).

## Assinatura de teste

O Android só atualiza um app por cima de outro com a mesma chave. O runner do Actions gera uma chave de
debug nova a cada execução, por isso cada APK exigia desinstalar, o que apaga o OBB importado.

O workflow aceita uma chave fixa só para testes, nos secrets `BOZ_TEST_KEYSTORE_B64` e
`BOZ_TEST_KEYSTORE_PASSWORD`. Ela nunca entra no repositório nem no artifact, e o workflow não roda em
PRs. Sem os secrets, o build avisa e usa uma chave descartável. Para criar (uma vez, no seu PC):

```bash
K=~/.config/boz-mobile-test; mkdir -p "$K"; chmod 700 "$K"; umask 077
head -c 32 /dev/urandom | base64 -w0 | tr -d '/+=' > "$K/password"
keytool -genkeypair -storetype PKCS12 -keystore "$K/boz-mobile-test.p12" -alias boz-mobile-test \
  -keyalg RSA -keysize 3072 -validity 3650 -dname 'CN=BOZ Mobile Test, O=Development only' \
  -storepass:file "$K/password" -keypass:file "$K/password"
base64 -w0 "$K/boz-mobile-test.p12" | gh secret set BOZ_TEST_KEYSTORE_B64 -R chgmcontato/boz-reimagined
gh secret set BOZ_TEST_KEYSTORE_PASSWORD -R chgmcontato/boz-reimagined < "$K/password"
```

A cópia em `~/.config/boz-mobile-test` é o único backup: secrets do GitHub não podem ser lidos de volta.
Use essa chave só em builds de teste de `com.carlos.boz.mobile`, nunca para publicar o app. O
certificado aparece no resumo de cada execução; se ele mudar, o Android recusa a atualização.

## Compilar (no seu PC)

Precisa de: JDK 17, Android SDK com **NDK 27.2.12479018** e **CMake 3.22.1** (Android Studio →
SDK Manager → SDK Tools), e os submódulos:

```bash
git submodule update --init --recursive        # Dynarmic e as dependências dele
cd mobile/android
echo "sdk.dir=$HOME/Android/Sdk" > local.properties   # ou abra a pasta no Android Studio
./gradlew assembleRelease                      # Windows: gradlew.bat assembleRelease
# → app/build/outputs/apk/release/app-release.apk
```

- Também há o workflow `.github/workflows/mobile-android.yml` (GitHub Actions, só na branch de
  desenvolvimento). Ele compila com o SDK e o NDK reais e confere o APK: bibliotecas
  `libmain.so`, `libSDL2.so` e `libc++_shared.so` só em arm64-v8a, alinhamento de 16 KiB,
  assinatura, dependências e símbolos `SDL_main`/JNI do `libmain.so`. O APK fica como artifact da
  execução por 14 dias.
- Na primeira configuração, o Gradle baixa o SDL2 2.30.9 e os headers do Boost 1.86.0 das releases
  do GitHub, confere o SHA-256 e extrai em `mobile/android/.deps/` (cerca de 260 MB, ignorado pelo
  git).
- Use **`assembleRelease`**: no `debug`, o código nativo sai em `-O0` e o JIT fica lento. O release
  é assinado com a chave de debug, então instala sem keystore (não serve para distribuir).
- O pacote é `com.carlos.boz.mobile` e instala ao lado do jogo original e do TV, sem conflito.

## Instalar e rodar no S24 FE

1. **Arquivos do jogo (seus):** o `BOZ-TV.apk` do Reimagined (o mesmo que o PC instala no modo
   Reimagined; gere com `xdelta3 -d -s base.apk boz-reimagined-tv-v0.6.xdelta BOZ-TV.apk`) e o OBB
   original `blackops_etc.dz` (526.783.202 bytes). Copie os dois para o celular (Downloads, por
   exemplo).
2. `adb install -r mobile/android/app/build/outputs/apk/release/app-release.apk`
3. Abra **BOZ Reimagined** → **Choose BOZ-TV.apk** → **Choose OBB** (cada arquivo é copiado para a
   pasta do app; o OBB leva um tempo) → **PLAY**.
4. No primeiro PLAY o runtime instala o jogo a partir do APK (como o `--install` do PC) e abre o
   jogo direto, sem o launcher.

Nos menus, o toque funciona como o mouse do PC. Na partida, os controles de toque são do runtime
(`src/touch_controls.c`, só no Android): joystick flutuante à esquerda, arrasto à direita para a câmera
e botões FIRE (também gira a câmera), AIM, RELOAD, USE, GREN, KNIFE, SWAP, TACT e CROUCH, mais a pausa no
canto superior esquerdo. Eles somem ao usar um controle Bluetooth e voltam no próximo toque. O botão
Voltar do Android fecha o jogo.

### Diagnóstico gravado no log (builds desta etapa)

| Linha no log | O que mede |
|---|---|
| `memory: N mappings below 4 GiB ..., M inside 0x40000000-0x90000000; page size P` | mapa do processo ao carregar (o conflito do 1:1) |
| `guest address space at 0x...` | a reserva de 4 GiB (base + endereço) |
| `dynarmic: A32 JIT, page table` | Dynarmic arm64 inicializado |
| `OpenGL OpenGL ES 3.x ... on <GPU>` | contexto GL real |
| `shader N source: S strings, B bytes, L lines, hash H` | o texto entregue ao driver (arquivo em `shader-dump/`) |
| `GL debug: callback on`, `GL debug: in <chamada>, frame N: <texto>` | erro de GL com a origem exata |
| `compressed texture format 0x....` | formatos de textura que o jogo envia |
| `shader N failed to compile`, `GL program N failed to link`, `GL error 0x....` | shaders e GL (até 40 erros) |
| `invalid read/write/fetch at 0x........` + registradores | acesso do **jogo** a memória não mapeada (Dynarmic) |
| `crash: signal N at host ... = GUEST 0x........` | acesso do **runtime** a uma página do jogo não mapeada: falha da tradução base + endereço |
| `crash: signal N ... outside the guest space` | crash nativo fora da memória do jogo (o tombstone vem logo depois) |
| `perf: N fps \| per frame ...`, `perf: scene map=... round=...` | FPS e tempo por quadro, a cada segundo |
| `memory: rss ... kB, peak rss ... kB, virtual ... kB` | memória do processo, a cada 15 s |

### O que observar e me mandar

| Checagem | Onde |
|---|---|
| O app abre, a cópia dos arquivos termina | tela de setup |
| `memory: N mappings below 4 GiB ..., M inside 0x40000000-0x90000000` | log (diagnóstico do 1:1) |
| `guest address space at 0x...` e `dynarmic: A32 JIT, page table` | log |
| `OpenGL OpenGL ES 3.x ... on <GPU>` e nenhum `failed to compile`/`failed to link` | log |
| `installed` / `mode reimagined` / `boz.s3e mapped at 0x4a000000` | log |
| Título → menu principal Reimagined, com texturas, sem travar | tela (print) |
| Som no menu | ouvido |
| Toque nos menus (Single Player, por exemplo) | tela |
| Com controle: entrar numa partida e jogar 1–2 rounds; FPS (F3 no PC; aqui o contador nativo) | tela |

## Coleta de logs

Logo depois de reproduzir o problema, com o celular no `adb`:

```bash
bash mobile/collect-logs.sh            # ou: bash mobile/collect-logs.sh <serial>
```

O script gera `boz-mobile-logs-<data>.tar.gz` com:
- o logcat do runtime (tag `BozRuntime`) e do SDL;
- o buffer de crash e o `exit-info`;
- o log da execução: `/sdcard/Android/data/com.carlos.boz.mobile/files/boz-runtime.log`, reescrito
  a cada PLAY;
- os dados do aparelho: modelo, Android, ABI e tamanho de página.

Num crash nativo, o que importa é o bloco inteiro do `DEBUG`/`libc` (registradores `x0`…`x30`,
`pc`, backtrace). Os `.so` vão com símbolos para o backtrace sair legível.

Sem PC por perto, o arquivo `boz-runtime.log` pode ser aberto com um gerenciador de arquivos que
tenha acesso a `Android/data`, mas no Android 16 isso costuma exigir o `adb`.

## Testes feitos sem o aparelho

Ambiente: Linux x86-64, `aarch64-linux-gnu-gcc` 13, `qemu-aarch64` 8.2, Mesa 25.2 (llvmpipe).

| Teste | Resultado |
|---|---|
| `tools/test_guest_memory.c` x86-64, mapa 1:1 e base fixa, com e sem fastmem | ✅ passa |
| Dynarmic compilado para aarch64 (`Target architecture: arm64`) | ✅ |
| `tools/test_guest_memory.c` aarch64 (backend arm64) sob qemu, 4 combinações | ✅ passa (depois do ajuste da cache de 128 MiB) |
| Runtime inteiro (flavor Android: base fixa + GLES) linkado para aarch64 Linux, PIE | ✅; sob qemu chega à reserva de 4 GiB e ao Dynarmic, para no GL (não há GL ARM64 aqui) |
| Flavor Android em x86-64 + Xvfb + Mesa **OpenGL ES 3.2**: launcher | ✅ igual ao PC (2 pixels com diferença de 1 nível) |
| Java (`SetupActivity`, `BozActivity` + Java do SDL) contra o framework da API 34 | ✅ compila |
| Trechos `__ANDROID__` (`platform_android.c`, `main.c`, `gl.c`, `ui.c`) | ✅ `-fsyntax-only` com headers falsos do NDK |
| Leitura do `/proc/self/maps` (diagnóstico de memória) | ✅ |
| **PC (Makefile) antes e depois**: build, launcher, testes `gl_state`, `timer_cache`, `pvrtc`, `updater` | ✅ igual |
| `tools/test_gles_shaders.c` no Mesa ES 3.2: corte em 64 falha, 165/182 strings compilam, vinculam e desenham | ✅ também roda no Actions |
| Os 62 shaders reais capturados de uma partida, compilados de novo um a um | ✅ 0 falhas |
| Fluxo real no Mesa, oculto (flavor Android, BOZ-TV v0.6): título → menu → Single Player → Kino, partida com zumbi | ✅ sem falha de shader |
| `gl.c` do PC pré-processado, antes e depois desta correção | ✅ idêntico |
| Gradle/AGP, NDK, APK | ✅ no Actions e no PC |
| Aparelho | ⬜ a validar no S24 FE |

Para repetir (com os caminhos do Boost e do SDL extraídos):

```bash
# flavor Android num desktop Linux (SDL2 do sistema, OpenGL ES do Mesa)
cmake -S mobile/native -B /tmp/boz-gles -G Ninja -DBOZ_BOOST_INCLUDE_DIR=<.../boost-1.86.0>
cmake --build /tmp/boz-gles && SDL_VIDEO_X11_FORCE_EGL=1 /tmp/boz-gles/boz-runtime --data /tmp/boz-data

# teste do núcleo do emulador (aarch64 sob qemu): ver o cabeçalho de tools/test_guest_memory.c
```

## Problemas conhecidos e limitações desta etapa

- **Não validado no aparelho.** O Mesa não é o driver do S24 FE (ANGLE + Xclipse), que pode recusar o
  que o Mesa aceita. Nada acima prova que o jogo roda no aparelho: é o próximo passo.
- **Controles de toque novos:** posições e sensibilidade são um primeiro ajuste, sem opção de configurar.
  O HUD nativo do jogo continua escondido pelos patches da TV.
- **Nome do jogador:** não há tela para isso no Mobile ainda; o jogo usa o padrão do launcher.
- **Voltar** fecha o jogo (comportamento padrão do SDL). **Saves** ficam na pasta do app e somem se
  ele for desinstalado.
- **Proporção:** o modo Reimagined desenha em 16:9 com faixas pretas nas laterais (19.5:9 no S24
  FE). Áreas seguras e layout adaptado ficam para a Etapa 6.
- **Co-op:** o multicast lock já é pedido e o ZeroConf do PC está no build, mas não foi testado
  nada de rede. A proteção de rede local do Android 16 pode interferir (Etapa 3).
- O jogo é ES 2.0 de fato: usa shaders GLSL ES 1.00 e `glUseProgram`. O número da linha "game asked
  EGL" não indica a versão (ver o primeiro boot).
- Pré-existente no PC, não corrigido aqui: num clone novo o `make` pode regerar
  `src/gl_thunks.c` a partir de `tools/gen_gl.py`, que está defasado (falta `gl_skip_draws`), e o
  link falha. Contorno: `git checkout src/gl_thunks.c && touch src/gl_thunks.c`. O build Mobile
  (CMake) não regera esse arquivo.

## Próximos passos

1. **Você:** compilar, instalar, fazer o teste da tabela acima e mandar o `.tar.gz` do
   `collect-logs.sh` com prints.
2. Corrigir o que o primeiro boot mostrar (GL, áudio, caminhos, desempenho). Medir o FPS no CoTD.
3. Quando o menu e uma partida rodarem: a Etapa 3, crossplay mínimo (descoberta na LAN do
   Android 16, Mobile ↔ PC, Mobile ↔ TV).
4. Em paralelo, como você pediu, só investigação: as diferenças atuais do Match Paused entre PC e
   TV, sem mudar nada.
