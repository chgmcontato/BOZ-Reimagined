# Formato dos .group.bin do COD:BOZ (Marmalade IwResManager + motor "Is")

Hashes de classe/nome: djb2 sobre o nome em minúsculas (tools/hashes.json mapeia hash -> nome).
Endereços abaixo = offset na imagem do boz.s3e (descomprimido, imagem em 0x43f3).

## Grupo
- `3d 03 07 01` versão do serializador, depois bloco do nome do grupo (u32 = len+9, nome\0, ...).
- Listas de recursos: u32 tamanho, u32 nº de listas, cada lista começa com o hash da classe.
- Grupos de mapa: <mapa>.group.bin -> <mapa>_dynamics + <mapa>_statics; <mapa>_sectors -> salas <sala>_shared.

## IwSerialise (0x28ce34 u16, 0x28cf40 s16, 0x28d044 u8, 0x28cc28 u32)
Valores crus little-endian (bits <= 16 -> 2 bytes). Arrays com "descritor" fixo no código
(0x28d804): u16 por campo, bits 0-3 = método, bits 6+ = nº de componentes, bit 4 = pular.
- método 4: bytes; cada componente em lista separada.
- método 5: int16; u16 centro único (min+max)/2, depois cada componente em lista separada (valor - centro).
- método 8/9/10: 32/32/64 bits.

## Blocos do CIwModel (cada objeto: hash da classe, hash do nome, depois Serialise)
Base CIwModelBlock (0x2741cc): CIwManaged::Serialise, u16 [+0xc] (tam. em memória), u16 [+0xe] nº elementos, u16 [+0x10].
- CIwModelBlockVerts (0x277fd8): u16 nº posições únicas N; descritor 0xc5 (método 5, 3 comps) sobre N int16x3;
  se N != nº elementos: (nº elementos - N) u16 de remapeamento (vértice GL i>=N usa posição remap[i-N]).
- CIwModelBlockGLUVs / GLUVs2 (0x275684): 2*nº int16 intercalados (u,v), 15 bits.
- CIwModelBlockCols (0x27729c): u8 modo; modo&1 = 1 byte cinza por vértice (r=g=b, a=255);
  modo&4 (versão nova) = RGB565 u16 (+ alfa u8 se modo&2); senão descritor 0x104 (método 4, 4 comps: R[],G[],B[],A[]).
- CIwModelBlockGLTriList (0x275908): u32 hash do material (versões novas), depois nº u16 índices.

## Grupo (confirmado: 78/78 grupos de mapa regravam idênticos com tools/iwgroup.py)
u32 versão (0x0107033d), u16 0, blocos [tag u32][tam u32 contando o próprio campo][conteúdo] até u32 0.
1º bloco ResGroupMembers = nome\0 + u32 flags. ResGroupChildPaths = subgrupos. ResGroupResources:
u32 nº listas; lista = u32 hash da classe, u32 nº objetos, u8 mesma_classe, u8 com_tamanho;
objeto = u32 tam (desde o próprio campo) [u32 classe se !mesma_classe] u32 hash do nome + Serialise.

## Textura (CIwTexture 0x2b3e68 / CIwImage 0x283b20)
u32 flags, u8, u8, s16[2], imagem: u8 formato, u16 flags, u16 larg, u16 alt, u16 pitch, u32 tam (versão > 0x30400), dados.
Formatos: 0x03 RGB_565 (tam = alt*pitch), 0x23 COMPRESSED (dados começam com u16 subformato 0x27 = ETC).
Material: molde do Kino (textura referenciada pelo hash do nome dela).

## Teste na TV (22/10/2026)
Cores do saguão do Kino trocadas no OBB (in-place) -> o jogo carregou o saguão vermelho. Original restaurado.
Offset do lobby no OBB: 441286054 (tools/dz.py dá o offset de cada arquivo).
