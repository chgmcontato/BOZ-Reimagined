# Reimagined — identidade visual v1

Arte `assets/title-reimagined-v1.png` criada com a ferramenta integrada imagegen,
usando `build/menus/shots/01_title.png` como referência. Original gerado foi copiado
para este projeto; a build não depende da pasta pessoal de imagens geradas.

## Direção visual

Quadrinhos de horror, preto e vermelho, tinta marfim desgastada, retícula fina.
A hierarquia preserva CALL OF DUTY / BLACK OPS / ZOMBIES e acrescenta REIMAGINED.
O aviso PRESS A TO START é texto Android com a fonte de quadrinhos já usada pelo
menu. O menu principal recebe uma tarja REIMAGINED no espaço central sob o logo.

## Integração

`BrandScreen.java` desenha somente em `title`, `title_prompt` e `main`, pelo
overlay já existente em Cursor. PixelCopy continua lendo a SurfaceView original,
sem incluir a arte sobreposta; coordenadas e eventos de toque não foram alterados.
O título usa uma única imagem carregada uma vez, sem animação ou timer novo.
Se a imagem não carregar, o título original permanece visível.

O OBB não recebe alterações. Não reativar o patch SWF `title_logo`: o teste na TV
mostrou título sem textura. Não substituir a textura ETC1 por PNG diretamente.
O contador FPS nativo fica coberto pela arte no título; continua intacto nas
demais telas e na partida.

## Validação e rollback

- `apk-check.json`: comparação do APK com a versão funcional anterior.
- `menus-edit/tests.py`: testes do kit visual.
- Round-trip válido: `menus-edit/night-2026-10-25-v2`, não o manifesto legado
  de `tv-current` (incompatível com o inventário atual de tags).
- Rollback completo do APK: `../character-menu/rollback/BOZ-TV-stable-before-branding.apk`.
- Instalar sempre com `adb install --no-incremental -r -g`, sem desinstalar.
- Navegação, seleção de mapa e Save & Quit com controle exigem teste humano;
  não são comprovados por screenshot de abertura.

## Prompt usado (ferramenta integrada)

Use case: logo-brand / precise-object-edit. Create a finished 16:9 landscape title-screen artwork for this Call of Duty Black Ops Zombies Android TV fan port, named Reimagined. Reference image is the original game title. Retain the recognizable original red-and-black ink zombie soldier composition and distressed off-white stacked CALL OF DUTY / BLACK OPS / ZOMBIES lettering, but refine into crisp vintage horror comic-book print with fine halftone, dark vignette, aged bone white ink and deep oxblood red. Recompose the logo slightly higher and smaller so there is a clear additional line REIMAGINED centered beneath ZOMBIES around 70% of the canvas height. REIMAGINED must be large, beautifully spaced, angular condensed block lettering, bone white on a dark red distressed horizontal print strip. Exact text only: CALL OF DUTY, BLACK OPS, ZOMBIES, REIMAGINED. Remove the existing PRESS B TO START text entirely, and do not add any button prompts or other text; the application will draw its own prompt. Keep bottom 18% dark and visually quiet for that live UI. Wide 16:9 composition, all lettering within central 75% width and no text cut off. No dashboard UI, no cards, no modern gradients, no glossy 3D logo, no added emblems or extra characters. Production-quality game title art, not a mockup.
