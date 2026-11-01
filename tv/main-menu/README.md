# Menu principal em quadrinhos

Versão local de 25/10. Não instalada nem testada na TV por pedido do usuário.

- `assets/layout.json`: fonte compartilhada de geometria, títulos, legendas,
  artes e destinos de toque. `MainMenu.java` desenha; `menus.py` gera o foco.
- Single Player, Player Profile e Co-op têm painéis principais independentes.
- Extras / Settings abre Options, que contém Settings; Quit mantém confirmação
  original. COD Points e More Apps não aparecem no novo menu.
- Ilustrações reaproveitadas dos assets do jogo, com recorte central sem distorção.
- As sondas leem a SurfaceView original e não são cobertas pelo overlay Android
  na captura PixelCopy. Nenhum script SWF ou patch nativo foi alterado.
- Ainda não implementa XP, ranks, lobbies ou mudanças de rede. Os IDs `profile`
  e `coop` separam os painéis para evolução futura. As legendas podem receber
  dados reais quando os respectivos sistemas estiverem prontos.
- `preview.svg`/`preview.png`: aproximação local do desenho, não captura Android.
  Gerar SVG com `python3 build/main-menu/preview.py`; renderizar com librsvg.
- `layout-check.json`: verifica destinos antigos, limites, não sobreposição e
  alcance de todas as ações com a regra direcional usada em Menus.java.
- Rollback: `../character-menu/rollback/BOZ-TV-before-main-menu.apk`.

Depois da instalação pelo usuário: conferir foco, cada destino, B para voltar,
retorno de Save & Quit e a troca de overlays ao entrar/sair do menu principal.
