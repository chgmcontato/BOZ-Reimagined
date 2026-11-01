# COD Black Ops Zombies — port para Android TV ("BOZ Reimagined")

Documento único do projeto: estado atual, como o código está organizado e como a próxima conversa deve
trabalhar. Atualizado em 26/10/2026. É o único `.md` na raiz. A documentação antiga (RESUMO, relatórios,
`docs/`) foi apagada; ver [Documentação antiga](#documentação-antiga) para recuperar algo pelo git.

---

## 1. O projeto em uma página

- **Objetivo:** transformar o APK de celular do *Call of Duty: Black Ops Zombies 1.0.11* (Android) numa versão
  "de console" para a **TV TCL** (Android 11, armeabi-v7a, 1280×720, 60 Hz), jogada inteira com **controle
  padrão Xbox** (8BitDo Ultimate). Sem HUD de toque, com layout de botões do BO1 de console e menus
  redesenhados no estilo quadrinhos.
- **Engine:** Marmalade (s3e). O jogo é um binário fechado, `assets/boz.s3e` (LZMA, ARM/Thumb), sem código-fonte.
  Todo o trabalho é **engenharia reversa + patches binários** + uma camada Java/Android por cima.
- **Usuário:** Carlos (fala português; interface do jogo fica em **inglês**, tradução depois). Ele testa
  com o controle na TV; o agente constrói, instala, coleta log/print/Frida e analisa.
- **Repositório:** `~/Downloads/COD.BOZ/Port` (P maiúsculo), branch `reimagined-night-2026-10-25`.
  Último commit de código: ver `git log` (o de 26/10 traz Death Machine, pause, timer e reload).

## 2. Estado atual (26/10/2026)

| Item | Valor |
|---|---|
| APK na TV | build final sem Frida, 26/10 (SHA-256 `dff2994ecb56fc7817c8198a0e368fc03ec52085612e37a9955e8c25914bd400`) |
| OBB na TV | `/storage/emulated/0/Android/obb/com.activision.boz/blackops_etc.dz`, 526.783.202 bytes, MD5 `e1b853fb9c38fe6d0e286d485e7fd0c9` (cópia local idêntica: `menus-edit/tv-current/.base/blackops_etc.dz`). Contém Kino, Dead Ops, Ascension, CoTD e tutorial; **não** contém Moon |
| TV | `adb connect 192.168.14.9:5555` (já foi `.11`; se falhar, varrer a porta 5555 em `192.168.14.x`) |
| Perfil na TV | zerado em 25/10 (saves e COD Points renomeados para `.bak`); nome do P1 = HEFESTO |

Confirmado funcionando pelo usuário na TV: controle inteiro (analógicos, tiro, mira, granadas, recarregar
no X), menus navegáveis por D-pad com foco em cantoneiras, menus redesenhados e transições sem aparecer o SWF
original, editor de nome, nome no Player Profile e no lobby do co-op, Local Wi-Fi → Single Map → lobby,
seleção de personagem/mapa (4 mapas), Save & Quit/Continue, título Reimagined. Em 26/10: Death Machine só com energia ou QR, 1º reload da
partida, pause com dados (jogador, mapa, round, painel do SWF) e fundo do mapa, timer parando no pause e
continuando após Save & Quit → Continue, TIME PLAYED no Player Profile, pause pelo Start no Dead Ops, timer com a fonte do HUD e botão TIMER: ON/OFF no
pause.

## 3. Como trabalhar com o usuário (regras combinadas)

1. **Responder em português.** Textos dentro do jogo em inglês.
2. **Testes com controle são do usuário.** Nunca usar `adb shell input` para apertar botões. Pode: instalar,
   abrir o app (`monkey -p com.activision.boz -c android.intent.category.LAUNCHER 1`), tirar print, ler logcat,
   usar Frida. Peça ao usuário que navegue até a tela e diga "print".
3. **Reinstalar fecha o jogo** e volta ao título: avisar no texto quando fizer isso.
4. **Instalar sempre** com `adb -s 192.168.14.11:5555 install --no-incremental -r -g BOZ-TV.apk`.
   A instalação incremental já fez o Android **apagar o pacote e os dados** (24/10). Não usar
   `instalar-na-tv.sh`: ele faz `uninstall` (apaga saves) e usa o IP antigo.
5. **Sem commit** sem pedido do usuário. Mensagens em Conventional Commits, em português, com o rodapé de
   atribuição. Antes de apagar/sobrescrever algo, olhar o alvo e, se for difícil de desfazer, confirmar.
6. Fluxo que o usuário gosta: diagnóstico primeiro (o que existe, o que é possível), uma recomendação clara,
   depois implementar, gerar, instalar e pedir o teste. Pedidos grandes vêm com "Passo 0" e restrições —
   seguir à risca e parar quando ele pedir.
7. **Não mexer** sem pedido em: drops (Death Machine, COTD/DG2), ondas, FPS/timer.
8. **Não editar o SWF/OBB dos menus** para mudar visual: a única tentativa (`title_logo`) quebrou a textura do
   título na TV. Visual novo é sempre overlay Java. Trocar um menu Flash por Java deve ser registrado como
   "substituição".
9. Visual: estilo quadrinhos do BOZ original, nada de "cara de dashboard"/"vibecoded". Transições sutis.
10. UI mudou → tirar print na TV e guardar em `production/qa/evidence/AAAA-MM-DD-<nome>.png`.
11. Build final é **sem Frida**. Builds de teste com `FRIDA=1` não ficam na TV.
12. Afirmar só o que foi verificado; se um teste não rodou, dizer.

## 4. Build, instalação e diagnóstico

```bash
cd ~/Downloads/COD.BOZ/Port
bash build/build.sh                 # gera BOZ-TV.apk (assinado, alinhado, sem v4/idsig)
FRIDA=1 bash build/build.sh         # teste: inclui frida-gadget (porta 27042, on_load resume)
A="adb -s 192.168.14.11:5555"
$A shell am force-stop com.activision.boz
$A install --no-incremental -r -g BOZ-TV.apk
$A logcat -c; $A logcat -G 4M       # buffer maior: com o padrão o log se perde
$A shell monkey -p com.activision.boz -c android.intent.category.LAUNCHER 1
$A logcat -d -s BozPad BozSave BozHud
$A shell screencap -p /sdcard/x.png && $A pull /sdcard/x.png <destino>   # exec-out corrompe o PNG
```

- `build.sh` decodifica `build/base.apk`, compila o Java de `build/java/com/carlos/boz/` (javac → d8 →
  baksmali), roda `patch_smali.py`, `patch_s3e.py`, `patch_pickups.py`, `patch_pickups_console.py`,
  `patch_lib.py`, `patch_text.py` e `patch_brand.py` (`patch_binds.py` é dos atalhos do OBB, à parte), gera `assets/boz_menus.json` com `menus/menus.py`, copia os assets dos
  menus e assina com `build/boz-tv.jks` (senha `boztv123`, alias `boz`). Ferramentas em `build/tools/`.
- **A build não envia o OBB**. O OBB da TV só muda com `adb push`/`dd` explícito.
  Gravar OBB sempre com `dd ... conv=notrunc` (sem isso já cortou o arquivo).
- Opções: `FPS30=0` / `MATCH_TIMER=0` desligam esses patches; `TABLET=1` (HUD tablet), `LOWEND=1`,
  `BO1_MUSIC=1` existem mas não são usadas.
- Se o espaço da TV ficar abaixo de 500 MB, a instalação falha: `settings put global
  sys_storage_threshold_max_bytes 209715200`, instalar, depois `settings delete global ...`.
- Rollbacks de APK em `build/character-menu/rollback/` (`BOZ-TV-before-*.apk`).
- Python para análise: venv com `capstone keystone-engine frida==17.4.*` (o gadget é 17.x). O venv do
  scratchpad some entre sessões; recriar com `python3 -m venv <scratch>/venv && <scratch>/venv/bin/pip install ...`.

### Frida (build `FRIDA=1`)

- `build/frida/run.py script.js [segundos]` (com `adb forward tcp:27042 tcp:27042`) já define `base` (início da
  imagem). Função Thumb = `base.add(offset + 1)`; stubs de importação são ARM (sem +1).
- Ler memória funciona. **Chamar funções do jogo a partir da thread do Frida falha** (sem contexto do jogo).
- Para observar a inicialização: trocar `"resume"` por `"wait"` em `build/frida/libgadget.config.so`
  (restaurar depois!), anexar com `dev.attach(pid)` + `dev.resume(pid)` e achar a imagem por varredura
  (`Process.enumerateRanges('rwx')` + assinatura do `run.py`). Funcionou uma vez; noutra o processo foi morto.
- `build/frida/crashcatch.js` mostra pc/lr relativos à imagem num crash.

## 5. Binário do jogo e patches nativos

- Endereços = **offsets na imagem** (binário descomprimido a partir de `0x43F3`). Em execução:
  `0x4a000000 + offset - 0x35090` (`build/re/sym.py` converte). **A imagem não fica no endereço 0**: código novo
  deve acessar globais/strings por PC-relativo (`ldr rX,[pc,#lit]; add rX,pc`), nunca absoluto (já causou SIGSEGV).
- `build/patch_s3e.py` é o ponto único: `CONFIG` (trocas de mesmo tamanho no ICF embutido) e `CODE`
  `(offset, bytes_antigos, bytes_novos)`, com verificação dos bytes antigos. Bytes montados com keystone e
  conferidos com capstone; a build não precisa dessas libs.
- **Área livre para código novo:** `0x40f12d..0x41108f` (sempre zerada, rwx). Uso atual:

| Faixa | Conteúdo |
|---|---|
| `0x410000..0x4104c7` | timer de partida (MATCH_TIMER, `build/analysis/fps-timer/timer.s`); estado em `0x410700` |
| `0x410600..0x41068f` | limite de 30 FPS (FPS30): yield com prazo, hook em `0x0fbc36` |
| `0x410800..0x4108ff` | CAVE: banco do 2º tático (claymore + Monkey Bomb) |
| `0x410900..0x410b2f` | CAVE2: ondas BO1, drops por ciclo, save do banco do tático |
| `0x410c00..0x410cbf` | Death Machine (`build/patch_pickups.py`): latch do Quick Revive, flag da partida, gate do ApplyEffect |
| `0x410d00..0x410d8b` | nome do jogador: `readname` / `profname` |
| `0x410d90..0x410dd3` | Death Machine/raio: `dmok`, `blkset` (dcycle) e flag "teve QR na partida" (`0x410dd0`) |
| `0x410de0..0x410ed3` | zumbi preso (`build/analysis/stuck/assemble_stuck.py`); tabela de 32 zumbis em `0x40f200` |
| `0x410ee0..0x410eed` | proteção do tratador de "som terminou" |
| a partir de `0x410ef0` (e `0x40f130..0x40f1ff`, `0x40f500..0x40ffff`) | livre |

- Patches existentes (detalhe em comentários do `patch_s3e.py`): morte → Game Over direto (`0x1C7F2C`),
  recarregar no X (`0x1DBFC6`, `0x1DC0E8`, `0x1DC116`, `0x1DC0FA`: sem a flag `[+0x51]`, que só ligava segurando), mira de segurar → alternar, HUD de toque escondido,
  claymore + Monkey Bomb, Max Ammo do banco, ondas BO1 sem teto (máximo vivo 15; 24 derrubava o jogo),
  drops BO1 (ciclo sem repetição, limiar ×1,14, Carpenter só com 5+ janelas), Dead Ops liberado (`0x2296C4`),
  contador de FPS nativo (`DisplayFramerate=1`, posição `FPS_X/FPS_Y` = rodapé central), 30 FPS, tempo de
  partida e tempo total no perfil (`STAT_BOZ_PLAY_SECONDS` no `CStatsManager`).
- **Death Machine** (`build/patch_pickups.py`): entra no ciclo de drops e funciona só com energia ligada
  (`[0x4958d0]+0x23d`) **ou** depois de alguém ter tido Quick Revive na partida (flag `0x410dcc`). `dmok`
  (`0x410d90`) decide; `blkset` (`0x410daa`, chamado pelo dcycle em `0x410a04`) põe a DM (bit 6) na máscara de
  bloqueados, como o Carpenter; o gate do ApplyEffect (`0x207d28`→`0x410ca0`, tipo 6) usa o mesmo `dmok`. A flag é
  zerada no construtor do jogador (`0x20b9de`→`0x410c00`) e ligada pelo latch (`0x21111c`→`0x410c20`) e pela
  concessão do perk 5 (`0x201110`→`0x410c50`); `+0x2bd` do `CPlayerController` continua como latch por jogador.
  Testado na TV em 26/10. Diagnóstico: `build/frida/diag_drops.js`. **Não pôr hook do Frida na entrada do
  dcycle (`0x4109f8`)**: deslocou a pilha e derrubou o jogo (SIGSEGV em `0x410a2c`).
- **Timer de partida** (`timer.s`, gerado por `assemble_timer.py` para o bloco GENERATED TIMER do
  `patch_s3e.py`): estado em `0x410700` (+8 ativo, +12/16 tempo jogado ms, +40/44 último instante, +48 flag de
  pause escrita pelo `Hud.java`, +52/56 tempo restaurado, +60 restaurar). O tick soma só intervalos ≤ 250 ms sem a
  flag de pause. Save & Quit → Continue: o `Hud` guarda o último tempo ativo e o mapa em SharedPreferences
  (`matchSavedMs`, `matchSavedMap`; o `.i3d` não pode ser alterado) e devolve pelo +52..+60; o `finish` desconta a
  base restaurada para o TEMPO TOTAL não contar duas vezes. Só o Continue da tela Single (Dead Ops não).
  Na tela: dígitos do HUD (`hud/d0-9.png`, ":" desenhado), `MM:SS` e `HH:MM:SS` a partir de 1 h, alinhado à
  direita em x=1232, y=150; botão TIMER: ON/OFF no pause (pref `timerShown`, só esconde).
- **Pause no Dead Ops:** `CDOIngameStatePlaying::vf3` (`0x17c748`) só lê a tecla de pause (`0x7e`) com o
  touchpad analógico inativo, e o analógico de tiro o mantém ativo. Um patch que lia a tecla mesmo assim **não
  funcionou** (desfeito). Solução: o `Cursor`, no Start, toca o botão de pause do HUD (1228,48) quando
  `Hud.s_DeadOps` (partida sem `CWaveManager`, `[0x4958c4]` = 0). O pause do Dead Ops é reconhecido como `pause`
  (mesma página Java); mostra DEAD OPS ARCADE, sem round (fica no `CDORoundManager`, não lido).
- **Raio (DG2):** o ciclo de drops bloqueia sempre o tipo 8 (`blkset`, `orr r1,#0x100`); a flag do ciclo
  (`CPickupManager+0x115`, de `LightningBoltLevels`) também liga enquanto um raio está no chão, por isso tirar o mapa
  da lista não bastava. O George, ao morrer, cria a garrafa de perk e o raio direto (`sub_205e3c` no
  `CPickupManager::vf2`, `0x208048`), fora do ciclo. Testado na TV em 26/10: DG-2 só depois de matar o George.
  O OBB da TV não precisa mais ser regravado.
- **Zumbi preso (26/10, não testado em situação de travamento):** a entrada `vf27` da vtable do `CAIControllerZombie`
  (`0x423744`, update por quadro) vai para `0x410de0`. Com o zumbi "andando" (`+0xfd` = 2 ou 16) e sem sair de um raio de
  64 unidades por 15 s, ele recebe dano = vida + 1000 por `CHealth::sub_1cea14` (o `CHealth` vem de
  `CHealth::sub_1ce7a4(entidade)`); morte normal, conta no round. Estados vistos: 2 andando, 4/5 janela, 10/12 entrando,
  7/8 atacando, 15 morrendo. Em jogo normal (156 zumbis) ninguém passou de 5,3 s parado. Frida:
  `build/frida/zombie_watch.js`, `stuck_watch.js`. `+0xfd` é só cópia da máquina de estados da IA (`vf29`).
- **Som (26/10):** crash na thread principal no tratador de "som terminou" (`0x10d8b0`) com os dados do som já liberados;
  `0x10d8c6` → `0x410ee0` encerra o canal pelo caminho normal (`0x10d956`) quando os dados são nulos.
- **Pause → jogo:** o `Pad` guarda os últimos analógicos e os reaplica ao voltar ao modo jogo (`Pad.onGameMode`), senão um
  analógico já inclinado ficava "morto" até mexer.
- **Táticos (27/10):** um só por vez, como no original. O banco do CAVE ficou desligado (`buy` em `0x410808` só
  guarda o jogador; `select` em `0x4108a8` não troca); direcional direito = só claymore, LB = só o tático que não é
  claymore. Nomes (djb2): `monkeyBomb` `0x1561c758`, `claymore` `0x3dc9e501`, `bh_bomb` (Gersh, Ascension)
  `0x75121c4e`, `matryoshka_doll` (Ascension e CoTD) `0x832c1d12`; `ammoIcon` 12/13/14. Ícones do HUD Java recortados do
  atlas `hud_icons` (`ingame.group.bin`, RGBA4444): Matryoshka em (405,460), Gersh em (42,295) (aparelho comprido com
  duas alças, conferido com o HUD original). Testados na TV em 27/10 (LB e HUD).
- **Nome do jogador (P1):** o Java grava `files/data-gles1/boz_name.txt` (maiúsculo, até 15 letras).
  `readname` (`0x410d00`) lê esse arquivo uma vez via `s3eFileOpen/Read/Close` (stubs `0x354e0`, `0x3580c`,
  `0x35498`; caminho com prefixo `data-gles1/`). `profname` (`0x410d36`, no lugar do `bl 0x229114` do
  construtor do `CSaveManager` em `0x22A1DA`) copia o nome para o perfil (`CSaveManager+0x4c`, singleton em
  `0x4958d8`). O co-op (`CGameNetwork`, `0x1c44d8`) montava `"Player-%d"` com `rand()>>4`; agora usa
  `snprintf(buf, 16, "%s", readname())`. Trocar o nome vale na próxima abertura do jogo.
- **Saves** (`files/data-gles1/*.i3d`): formato = versão (`0x493e0`) + Adler-32 do arquivo com esse campo
  zerado + dados, **e** um segundo checksum guardado num registro à parte (`0x15c1f0` grava, `0x15c1bc`
  confere). **Nunca escrever um `.i3d` de fora:** o jogo o considera adulterado, apaga e recria
  (aconteceu com o `profile_0.i3d`). Arquivos: `profile_0`, `save_stats_0`, `1_/2_save_game`,
  `save_settings`, `save_achievement`, `mm`, `bostik`, `rcpt` + `files/*.rcpt` e `__ng__.dat` (loja/COD Points).
- Mapa do código: `build/re/` (`run_all.sh` refaz a análise; `sym.py` consulta; nomes à mão em `known.json`).
  Pseudocódigo Ghidra de todas as funções em `build/re/out/decomp_all/<módulo>.c`, strings e funções que as
  usam em `build/re/out/strings.csv`. `python3 build/re/gendocs.py` regenera a documentação por módulo em
  `docs/re/` (apagada, mas regenerável).

## 6. Camada Java (`build/java/com/carlos/boz/`)

| Arquivo | Papel |
|---|---|
| `Pad.java` | controle → teclas/touchpad do Xperia Play; `attach()` faz o reset único do perfil e grava o nome |
| `Cursor.java` | modo menu/jogo, foco por cantoneiras, toques no SWF, seleção de mapa/personagem, **transições**; `ArrowView` desenha tudo |
| `Menus.java` | reconhece a tela por PixelCopy da SurfaceView (grade 160×90 em cinza, sonda + `probe2`, regra `after`), a cada 120 ms no modo menu |
| `MainMenu.java`, `CharacterMenu.java`, `BrandScreen.java` | menu principal, seleção de personagem/mapa e título Reimagined (overlay) |
| `StyledScreen.java` | telas repaginadas a partir de `menus-style/assets/screens.json`; cortina de transição |
| `NameEditor.java`, `PlayerNames.java` | editor de nome em grade (só controle, sem IME) e nomes P1/P2 em SharedPreferences `boz_tv` |
| `ProfileSave.java` | reset único (`profileReset1`) e `boz_name.txt` |
| `Hud.java` | contadores de granada e timer de partida (lê memória nativa; ponto sensível a crash) |
| `NetPad.java`, `s3eTouchpad.java` | entrada por rede / touchpad |

### Menus: como uma tela funciona

- O overlay Android desenha por cima; os toques continuam indo ao SWF por coordenada; a sonda só lê a
  SurfaceView, então o overlay não atrapalha o reconhecimento.
- `build/menus/menus.py` define cada tela: `shot` (print em `build/menus/shots/`), `probe`, `probe2`, `after` e
  itens `["b", x, y, w, h, tags...]` / `["s", x1, y1, x2, y2]` (slider). Tags: `game`, `selection`, `name`,
  `tap:X,Y`, `to:ID` (tela que o toque abre; `to:*` = muda sem destino conhecido), `stay` (não muda de tela),
  `char:N`, `map:N`, `start`. A saída imprime a tela mais parecida com cada uma: manter a distância > 16.
- Geometria compartilhada: `build/main-menu/assets/layout.json` (menu principal) e
  `build/menus-style/assets/screens.json` (`kind` `page` = página inteira em Java; `frame` = tarja no título +
  rodapé sobre o SWF; `windows`, `message`, `back`, `buttons` com `to`/`primary`/`art`).
- 21 telas mapeadas: title, title_prompt, main, single, newgame, charselect, coop, coopwifi, coopmap,
  cooplobby, extras, controls, settings, help, quitconfirm, pause, pausesettings, sensitivity, savequit,
  player_profile, gameover. Substituições (page): single, newgame, quitconfirm, savequit, pause, gameover.
- **Transição** (`Cursor`): ao tocar um botão com `to`, ou ao apertar B numa tela com `back`, o overlay não
  espera o SWF. Se o destino é página Java, ela aparece na hora; senão fica a **cortina** (moldura parada).
  Crossfade de 160 ms; a cortina some em 200 ms; um traço vermelho fino corre sob a faixa do título após
  250 ms. Termina quando a sonda vê outra tela, após 900 ms se a origem não mudou (antes repete o toque uma
  vez), ou em no máximo 2,5 s. Durante a transição o *apertar* de A/B/D-pad é bloqueado; o *soltar* precisa
  passar (senão o toque fica preso e o jogo ignora os seguintes). Tela nova: A/B esperam 350 ms. Sonda que
  falha no meio de uma animação dispara uma cortina implícita (fora da partida). Do título para o menu
  principal: transição já no aperto do A, sem fade.
- Diálogos com o mesmo fundo: `quitconfirm` só vindo de main/title, `newgame` só de single, `savequit` só de
  pause (`after`).
- Arquivos editáveis do visual: `python3 -I build/menus-style/export_svg.py` gera `build/menus-style/design/*.svg`
  (um por tela, camadas nomeadas, ids = ids do JSON; fonte `comic.ttf` = CCAdamKubert). SVGs fora do git.
  O usuário pode editar e devolver; o agente passa as mudanças para JSON/Java.
- Design system: canvas 1280×720; PAPER `#F2EEE4`, INK `#151211`, RED `#791D24`, DIM `#191614` (alfa DE),
  legenda `#D5C9AC`, borda `#827763`; faixa do título 64,38–1216,134 ("BLACK OPS ZOMBIES / REIMAGINED"),
  corpo 48,134–1232,674, rodapé em y=698; painéis com cantos inclinados; foco em cantoneiras brancas/pretas
  que deslizam.
- READMEs técnicos ficaram ao lado do código: `build/menus-style/README.md`, `build/main-menu/README.md`,
  `build/character-menu/README.md`, `build/branding/README.md`, `build/menus-style/design/LEIA-ME.md`.

## 7. Decisões do usuário (não reabrir)

- Controle só no padrão Xbox; sem detecção de PlayStation.
- HUD de celular (o maior); mira de segurar virou alternar; Game Over estilo BO1 abandonado; morte vai
  direto ao Game Over (sem tela de reviver).
- TV em 60 Hz (50 Hz não pega no painel). Desempenho medido 57–62 FPS no CoTD antes do limite de 30 FPS.
- Dead Ops liberado de saída. COD Points removidos de menus/pausa/compras. More Apps e Buy COD Points escondidos.
- Player Profile: estatísticas originais mantidas, item novo para o nome (PLAYER: <nome> no menu principal).
- Telas de configurações e Game Over com tratamento só de moldura: aceito.
- Pause (aprovado 26/10): nome do jogador + `MAPA • ROUND` (Java, lidos de `[0x4958d0]+0x55` e `[0x4958c4]+0x78`),
  janela 196,270,560×190 mostrando o painel do SWF (personagem, kills, headshots, revives; a cortina também
  recorta as janelas) e o cenário do mapa (`character-menu/*background.png`) como painel único de 48,38 a
  1232,674. Uma janela grande mostrando o mapa ao vivo foi **rejeitada**.
- Player Profile: TIME PLAYED dentro do quadro STATISTICS, abaixo de HIGHEST WAVE, no estilo do SWF (aprovado).
- Co-op: **Play Online não funciona** (servidores desligados) — ignorar. **Map Vote não é usado** (fora do foco).
- Moon (clone do Kino com a No Man's Land): **pausado** pelo usuário.

## 8. Pendências e próximos passos

0. **A confirmar na TV:** ícone novo do Gersh, analógicos ao sair do pause, zumbi preso morrendo após 15 s (e se dá
   pontos) e se a proteção do som acabou com o fechamento sozinho.
1. **Mais telas sem mapa:** About/créditos, More Info, as subtelas de Extras/Help, o diálogo do Play Online e
   o lobby depois que outro jogador conecta. Para mapear: o usuário abre a tela, o agente tira print, copia para
   `build/menus/shots/NN_nome.png`, cria a entrada em `menus.py` (sonda no título) e, se quiser o estilo,
   uma entrada `frame` em `screens.json`.
1a. **Pause:** se a linha `MAPA • ROUND` sumir de novo depois de um Continue (aconteceu uma vez em 26/10, sem
   causa achada; no reteste o jogo informou mapa e round normalmente), ler o log `BozHud` "partida: ...".
2. **Co-op Local Wi-Fi com dois aparelhos:** não testado (host/cliente, nome do P2, `PlayerNames` slot 2).
3. **Testes ainda pendentes na TV:** timer de partida e TEMPO TOTAL (Save & Quit, Game Over, Restart, Dead Ops),
   30 FPS, Save & Quit com claymore + Monkey Bomb, drops/ondas BO1, prompts "[A] to …", tutorial.
4. **Hud.java** lê ponteiros nativos (`libcore.io.Memory`); um SIGSEGV não é capturado. Melhoria sugerida:
   o código nativo publicar um snapshot com geração/validade (como o timer já faz).
5. HUD: só um indicador de granada nativo (0x1cbe58 alterna); dois indicadores a mais exigiriam editar
   `ingame.group.bin`.
6. Visual: o usuário pode devolver SVGs editados de `build/menus-style/design/` para implementar.
7. Moon/NML (pausado): 1 pedaço e 4 texturas funcionam; 14 pedaços dão SIGSEGV perto de `0x26a3fa`.

## 9. Armadilhas já conhecidas

- Instalação incremental apaga o app; sempre `--no-incremental`.
- `adb exec-out screencap` gera PNG inválido nesta TV; usar `screencap` para arquivo + `pull`.
- O pacote não é debuggable: `adb` não lê `/data/data/com.activision.boz`; código Java dentro do app lê
  (foi assim que os saves foram inspecionados, com uma sonda temporária).
- Textos LOC0 (`localisation/english_*.dat`): só trocas de mesmo tamanho têm efeito (`patch_text.py`).
- O SWF dos menus não tem foco próprio; botões são `ButtonBase`. O carrossel de mapas do SWF: Tutorial=0,
  Kino=1, Dead Ops=2, Ascension=3, CoTD=4 (o Cursor ancora no índice 0).
- O A do título vai direto ao jogo (não passa pelo foco).
- `menus.py` precisa de `convert` (ImageMagick) para ler os prints.
- Texto Java é encolhido para caber (`size * maxWidth / largura`); o `export_svg.py` imita isso com PIL.

## 10. Outras ferramentas

- `maps/tools/portkit.py` (`doctor`, `scan`, `extract`, `diff`, `map-init`, `map-set`, `map-build`, `hud-plan`,
  `hud-apply`) e testes em `maps/tests` (`python3 -m unittest discover -s maps/tests -v`). Saídas em
  `maps/out/portkit/` (locais, ignoradas pelo git). Formato dos grupos em `maps/docs/formato-group-bin.md`.
- `maps/tools/`: `dz.py`, `iwgroup.py`, `iwmodel.py`, `render.py`, `clone_level.py`, `hud_patch.py`,
  `console_bin.py`, `console_set.py`. Os 1098 parâmetros do `console.bin` em `maps/docs/console-params.txt`.
- `menus-edit/`: kit de extração/repack das 318 texturas do `frontend.group.bin` (round-trip byte-idêntico).
- Externos: OpenAssetTools em `~/repos/ext/OpenAssetTools`, JPEXS FFDec em `~/repos/ext/ffdec`.
- Jogo da Steam (BO1): trilha MP3 oficial utilizável em `assets/blackops-music/`; menus de PC são outro sistema.
- Pasta `.claude/` traz agentes/skills de um template de estúdio (Godot/Unity/Unreal). **Não se aplicam** a este
  projeto; a engine em `project.yaml` não está configurada de propósito, e o aviso do hook pedindo `/start` pode
  ser ignorado.

## Documentação antiga

Apagada em 26/10/2026, logo após o commit `9159633`, que ainda a contém. Para consultar:

```bash
git show 9159633:RESUMO.md                       # histórico das sessões 22–25/10
git show 9159633:RELATORIO-menus-desempenho.md   # desempenho, FPS, Steam, interações
git show 9159633:docs/reimagined/03-fps-timer.md # detalhes do limite de 30 FPS e do timer
git show 9159633:docs/re/criador-de-mapas.md     # entidades, colisão, portais e navegação
git show 9159633:docs/re/hud-e-moon.md           # HUD/granadas e crash do Moon
git show 9159633:docs/PORTKIT.md                 # guia do portkit
git ls-tree -r --name-only 9159633 docs/         # lista completa
```
