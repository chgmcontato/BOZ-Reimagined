# Menus editáveis (SVG)

Um arquivo por tela, 1280x720 (a mesma grade do jogo). Abra no **Inkscape** (melhor), Figma ou Illustrator.
Instale `comic.ttf` (CCAdamKubert) antes de abrir, senão o texto aparece em outra fonte.

Camadas:
- **Referência SWF** (travada): o que o jogo original desenha por baixo. Não vai para o jogo.
- **Moldura**: papel, faixa do título, corpo de tinta, tarjas.
- **Botões**: cada grupo tem o `id` do botão no JSON (`single`, `back`, `map-1`...). Pode mover,
  redimensionar, trocar cor, arte e texto.
- **Textos / Rodapé**: títulos, mensagens e dicas de controle.
- **Foco (exemplo)**: só mostra como ficam as cantoneiras de seleção.

Pode mudar à vontade: posição e tamanho, cores, textos, fontes, artes (troque a imagem), bordas e
inclinação dos painéis, e também acrescentar elementos novos. Nas telas "frame" (settings, controls, help, coop,
extras, player profile), os sliders e as caixas continuam sendo do jogo original. Lá, só a tarja,
a placa de nome e o rodapé são nossos; o resto da área precisa continuar deixando o SWF à mostra.

Devolva os SVG editados (mantendo os ids dos grupos) que eu passo para o Java/JSON.
Para gerar de novo a partir do código: `python3 -I build/menus-style/export_svg.py`
(isso sobrescreve esta pasta; guarde suas versões com outro nome).
