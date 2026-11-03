# Menus do Reimagined em PSD

Os arquivos gerados contêm arte do jogo instalado e **não vão para o Git**. Para gerá-los de novo:

    python3 tools/psd/export_menus.py --assets build/game/reimagined/assets --refs assets/reimagined/psd/native
    python3 tools/psd/validate.py        # requer psd-tools

## Pastas
- `psd/`: um arquivo por tela, em 1920×1080, com camadas e grupos.
- `svg/`: a mesma tela com texto vivo e formas vetoriais (imagens em `layers/`).
- `preview/`: PNG de cada tela.
- `layers/`: cada camada em PNG.
- `layers.json`: mapa de camada para função de código, retângulo no PSD e retângulo lógico.

## Coordenadas
PSD = lógico × 1,5. O jogo sempre desenha em 1280×720 lógicos e escala para a resolução real.
Exemplo: um botão em x=96 no código fica em x=144 no PSD.

## Grupos
BACKGROUND, NATIVE_REFERENCE, PANELS, DECORATIONS, BUTTONS, ICONS, TEXT, OVERLAYS.
Ocultos: VARIANTS (estados focado e desativado) e HIT_AREAS (áreas clicáveis, em magenta).
As guias marcam o viewport, as áreas clicáveis e as aberturas.

## O que é arquivo e o que é código
| Elemento | Como alterar |
|---|---|
| Papel, tarja, setas, retratos, arte de mapa e visto (`character-menu/*.png`, `main-menu/*.png`) | Trocar a textura |
| Painéis, explosão, raios, dentes da folha, placas | Desenhados em `src/menus.c` (`kit_*`): mudar em código |
| Textos e fonte | Desenhados em código com `comic.ttf`; o conteúdo é fixo ou dinâmico |
| Áreas clicáveis | `build_targets`, `define_coop_pages` e `layout.json`/`screens.json` |
| Cartões, menus e pausa nativos | Do jogo, não entram na arte. As aberturas ficam transparentes |

## Referências nativas
Com `--refs <pasta>`, cada `<tela>.png` entra em NATIVE_REFERENCE. Ela fica oculta nas telas do Reimagined e
não entra nas prévias nem na arte. Arquivos `native_<tela>.png` viram PSDs só de referência (Perfil, Extras,
Controles, Configurações, Ajuda, Sensibilidade, seleção de mapa do Coop e lobby), em que o port não desenha
quase nada; a edição deles não volta para o jogo. Para capturar, use `CODBOZ_HIDE_OVERLAY=1`.

## Devolver alterações
1. Edite sem renomear as camadas e devolva os PSDs em uma pasta.
2. Rode `python3 tools/psd/import_menus.py <pasta>`.
3. O relatório lista as camadas alteradas e salva o PNG em `changed/`.
4. Camada movida em BUTTONS, PANELS ou HIT_AREAS exige atualizar a posição e a área clicável em código.

## Limitações
- **O texto no PSD está rasterizado**, em camadas próprias. Texto editável fica no SVG.
- As formas são raster no PSD e vetoriais no SVG, exceto em telas com abertura, onde são imagens.
- Texto dinâmico mostra valores de exemplo: nome, mapa, round, status e timer.
- O "PLAYERS" e a nota do Match Paused usam `comic.ttf` aqui; no jogo usam a fonte do corpo.
- Não inclui o placar do Coop nem o HUD, desenhados com dados da partida.
- As referências nativas são capturas achatadas, não camadas editáveis.
- A referência de `pause_coop_box` e `leavecoop` vem de Coop forçado em partida Solo; `match_paused` mostra só uma partida Solo por trás.
