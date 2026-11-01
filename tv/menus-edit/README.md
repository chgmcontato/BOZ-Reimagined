# Kit de edição visual — COD BOZ

Fluxo local: **extract → editar PNG no Photocraft → repack → push**. Os scripts
validam o manifesto, os pixels, a transparência e o formato antes de publicar
um OBB. `push.py` significa publicar em `build/out/`: não chama ADB, não envia
arquivos e não altera a TV.

## Começar

```bash
python3 -B menus-edit/extract.py --obb obb/blackops_etc.dz --work menus-edit/minha-edicao
```

O kit guarda uma cópia de referência em `.base/`, gera `manifest.json`, PNGs e
`catalogo.html`. Mantenha `.base/` e o manifesto intactos. A cópia usada nesta
sessão (`obb/blackops_etc.dz`) tem MD5 `e1b853fb9c38fe6d0e286d485e7fd0c9` e é
byte-idêntica à base salva anteriormente em `tv-current/.base/`; o caminho
`obb_moon/blackops_etc.dz` indicado no contexto não existe nesta cópia de trabalho.

Edite somente os PNGs em `assets/`. `previews/` contém texturas ETC1 decodificadas
para referência visual e não pode ser reinjetado. Para preservar alfa, dimensões
e cores, exporte PNG verdadeiro, 8 bits/canal, sem perfil ou metadados de cor.
Uma textura RGBA exige alfa idêntico pixel a pixel; RGB não deve ser convertido
para RGBA.

Antes de editar, prove o round-trip:

```bash
python3 -B menus-edit/repack.py --work menus-edit/minha-edicao --roundtrip
```

Sem alterações, o MD5 da saída deve ser idêntico ao OBB original. No teste desta
base, 251 texturas PNG foram reconstruídas e o OBB permaneceu byte-idêntico
(MD5 `e1b853fb9c38fe6d0e286d485e7fd0c9`).

Depois da edição:

```bash
python3 -B menus-edit/repack.py --work menus-edit/minha-edicao
python3 -B menus-edit/push.py --work menus-edit/minha-edicao
```

`push.py` grava uma cópia validada e nomeada pelo hash em `build/out/` e imprime
o comando de transferência para execução manual futura. **O comando não é
executado**; não use durante esta sessão.

## O que é editável

- `frontend/frontend.group.bin`: texturas RGBA8888/RGB888 são editáveis. A
  dimensão, alfa e formato original estão no manifesto por textura. As texturas
  ETC1 aparecem apenas como previews.
- `frontend/fe_v4_cs4_swf.bin`: o inventário registra o ID, tipo, offset e
  tamanho de cada tag `DefineBitsLossless*`, além do vínculo com o índice da
  textura. Neste SWF, as imagens `2×1` dessas tags são placeholders de atlas,
  não bitmaps com pixels de arte editáveis; a arte real fica no atlas acima.
  Repack não altera o SWF.
- `ingame.group.bin`: fora do escopo do extrator atual. O atlas contém dados
  RGBA4444 e requer um editor/repacker específico para evitar conversão com
  perda; não o edite com este kit.

### Logo e título

- `assets/291_main_menu_logo.png` é textura **RGBA8888 512×512**, usada pelo
  bitmap SWF ID 1228. É o logo principal e pode ser editado/reinjetado mantendo
  a máscara alfa.
- `previews/290_blackopstitle_ipad.png` é a textura do título, **ETC1 RGB
  2048×1024**. Está disponível como referência PNG, mas não pode ser reinjetada
  por este kit. Não confundir o índice de atlas 291 com um número de tag SWF.

## Testes

```bash
python3 -B menus-edit/tests.py
python3 -B menus-edit/tests.py --integration --work menus-edit/minha-edicao
```

Os testes são locais; não instalam APKs, não enviam OBBs e não executam comandos
ADB.
