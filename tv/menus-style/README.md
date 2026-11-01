# Menus repaginados + nome do jogador (25/10/2026)

Overlay Android (`StyledScreen.java`) sobre as telas SWF, no estilo do
`MainMenu`/`CharacterMenu`. O SWF e o OBB **não** mudam: `build.sh` não
reempacota o SWF, e a única tentativa de editá-lo (`title_logo`) quebrou a
textura na TV. Os toques continuam indo ao SWF por coordenada, e as sondas do
`Menus.java` leem só a SurfaceView, por isso o overlay não afeta o reconhecimento.

## Fonte única de geometria

`assets/screens.json` é lido pelo `menus/menus.py` (itens de foco/toque em
`boz_menus.json`) e pelo `StyledScreen.java` (desenho). Item = `bounds`
(desenho + foco), `tap` (ponto no SWF original, medido em `menus/shots/`),
`action` opcional (`selection`, `game`, `name`).

| Tela | Tipo | Observação |
|---|---|---|
| single, newgame, quitconfirm, savequit | **page (substituição)** | Página inteira em Java; os textos do SWF foram reescritos |
| pause | **page (substituição)** com janela | A janela mostra retrato e estatísticas do SWF; o saldo de COD Points fica coberto |
| gameover | **page (substituição)** com janela | A janela mostra score, wave, kills e XP do SWF |
| settings, pausesettings, sensitivity, controls, help, coop, extras | frame | Tarja sobre o título original + rodapé; sliders e caixas continuam sendo do SWF |
| player_profile | frame | Tarja no título; o nome vem do próprio jogo (patch nativo readname/profname) |
| About/créditos, More Info | sem overlay | Não têm sonda; o cursor de seta continua |

More Apps e Buy COD Points já não aparecem no menu principal (`MainMenu`),
e na pausa o saldo fica coberto.
**Regressões possíveis nas substituições:** perde-se a animação do SWF, e o
CONTINUE aparece mesmo sem save (o toque segue para o SWF, que decide).

## Nome do jogador

- O jogo tem um nome de perfil (CSaveManager+0x4c, salvo em `profile_0.i3d`), mas apaga um save escrito de
  fora (2º checksum guardado à parte). `ProfileSave.java` grava o nome do P1 em `data-gles1/boz_name.txt`, e o
  patch nativo (`patch_s3e.py`, readname/profname) o coloca no perfil e no nome do co-op ao abrir o jogo.
  Trocar o nome vale a partir da próxima abertura. Máximo de 15 letras. Não há IME (`s3eOSReadString` não é importado). O nome fica em
  `SharedPreferences("boz_tv")`, chaves `player1Name`/`player2Name`
  (`PlayerNames.java`). Sem chave = padrão `PLAYER 1`/`PLAYER 2`. Os saves do jogo não são tocados.
- Regras: ASCII imprimível, sem espaços duplicados ou nas pontas, até 16
  caracteres, e um nome vazio volta ao padrão. Gravado com `commit()` (síncrono).
- Editor (`NameEditor.java`, opção B, teclado em grade): item **PLAYER: <nome>**
  na faixa inferior do menu principal. D-pad/analógico movem; A digita; B apaga;
  X insere espaço; Y cancela; START salva; LB/RB trocam P1/P2. As teclas de ação
  SPACE/DELETE/CANCEL/SAVE ficam na grade. O nome do P1 também aparece na seleção de personagem.
- Co-op: ligar o `GameCenterAndroid::GetLocalPlayerName` (0x1c2a30) a este valor
  fica para depois.

## Transição entre telas (Cursor.java)

O botão diz para onde leva com `to` (em `screens.json` ou `layout.json`; `"*"` quando o destino é desconhecido), e o
B usa o `back` da tela. Ao tocar, o overlay para de esperar o SWF. Se o destino é uma página Java (main,
charselect, page), ela aparece na hora. Senão fica a **cortina**: a mesma moldura parada, sem título. O conteúdo
se dissolve entre as páginas em 160 ms, e a cortina leva 200 ms para sumir e revelar uma tela frame. Se a espera
passar de 250 ms, um traço vermelho fino corre sob a faixa do título. A transição acaba quando a sonda vê outra
tela, ou depois de 900 ms se a tela de origem continuar (o toque não fez nada), ou no máximo em 2,5 s (tela sem
mapa ou carregamento). Durante a transição, A, B e o direcional ficam bloqueados. Botões com `stay` (caixinhas)
não abrem transição. A sonda roda a cada 120 ms no modo menu.

## Arquivos editáveis

`export_svg.py` gera `design/*.svg` (um por tela, em camadas). Veja `design/LEIA-ME.md`.

## Prévia e verificação

`python3 -I build/menus-style/preview.py` gera `previews/<tela>.png`, uma aproximação
sobre os prints do SWF com o foco em verde. Não substitui uma captura na TV.
Os prints de `menus/shots/` são antigos (ainda mostram COD Points); a posição
das telas atuais precisa ser conferida no aparelho.

Rollback: `build/character-menu/rollback/BOZ-TV-before-menus-style.apk`.
