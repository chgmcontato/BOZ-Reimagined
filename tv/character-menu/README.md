# Seleção de personagens para TV — cartoon e cantoneiras (24/10/2026)

Tela Android própria, desenhada por `CharacterMenu.java` e acionada pelo reconhecimento
`charselect`. Revisão visual a pedido do usuário: quadrinhos como o BOZ original,
fonte instalada CCAdamKubert, papel envelhecido, retratos grandes, tarjas inclinadas,
setas de voltar/iniciar e marcas de seleção extraídas do jogo. O menu anterior continua na
SurfaceView, oculto pelo Canvas. PixelCopy lê essa SurfaceView sem incluir a sobreposição.

O novo layout fica no APK; não precisa alterar o SWF nem enviar o OBB de 1 GB.
Os botões continuam acionando a seleção original por toque interno. O controle físico
navega pelo mapa espacial em `build/menus/menus.py`. Há 11 ações alcançáveis pelo direcional.

## Assets e build

```sh
python3 build/character-menu/prepare_assets.py
bash build/build.sh
python3 build/character-menu/validate.py
```

`prepare_assets.py` copia as imagens extraídas de `menus-edit/work` e as fontes locais.
O renderer atual usa `~/.local/share/fonts/21_CCAdamKubert.ttf`; Carbon é mantida entre
os assets para preservar a variante anterior. A origem e o SHA256 de cada
arquivo ficam em `sources.json`. Os PNGs originais não são editados. O renderer carrega
essas imagens uma vez; não há animação contínua nem decodificação a cada frame.

`validate.py` abre o APK final, verifica os assets incorporados e a integridade ZIP,
recusa Frida e verifica margem segura e alcance de todos os botões pelo direcional.
A assinatura também foi verificada com `apksigner`: corresponde à build anterior.

## Integração com o jogo

- A seleciona o personagem ou mapa em foco; B e o botão Voltar retornam.
- O mapa selecionado é destacado, enquanto o foco indica onde A vai agir.
- Iniciar aguarda a sincronização do carrossel. O personagem é reaplicado antes
  do início para acompanhar as atualizações do SWF durante a troca de mapa.
- Se o carrossel não responder em 20 s, o início fica bloqueado e a tela pede
  selecionar o mapa novamente. Sair da tela cancela os toques pendentes.
- Dead Ops usa seu esquadrão próprio; a seleção dos quatro personagens fica inativa.
- Moon continua sendo o protótipo baseado em Kino, identificado como tal.
- Reconhecimento tolera até 750 ms de animação antes de retirar a nova tela.
- Cantoneiras conforme `selecao/B_cantoneiras.png`: quatro cantos brancos com contorno
  preto, sem escurecimento nem borda fechada. Aplicadas também aos demais menus
  reconhecidos. A animação curta de deslocamento do foco foi preservada.

## Instalação e teste real

Instalado em `192.168.14.9:5555` com `adb install --no-incremental -r -g BOZ-TV.apk`, seguido de
`am start -n com.activision.boz/.Main`. ADB retornou `Success`; o processo abriu e
a tela de abertura foi capturada. Saves e OBB foram preservados; sem Frida.

A seleção foi aberta pelo usuário com o 8BitDo e capturada na TV. Fonte,
layout e foco foram verificados visualmente; o log registrou os quatro personagens
e a sincronização do carrossel nos cinco mapas (incluindo retorno de Moon para
Call of the Dead), sem timeout. A captura mostra Dempsey selecionado. Não houve
erro AndroidRuntime nos logs coletados. Início de partida ainda precisa de teste. Não foram enviados
comandos `adb input`. Evidência: `production/qa/evidence/2026-10-24-character-menu-tv.png`.

Roteiro: Um jogador → Novo jogo → seleção; conferir foco nas quatro opções de
personagem e cinco mapas; escolher personagem, aguardar “Pronto para sobreviver”,
iniciar Kino; voltar ao menu e testar outro mapa. Conferir também B e Dead Ops.

Backup anterior: `rollback/BOZ-TV-before.apk`. Para reverter somente esta atualização:

```sh
adb -s 192.168.14.9:5555 install --no-incremental -r -g build/character-menu/rollback/BOZ-TV-before.apk
```

Instalar o APK fecha o jogo. O rollback não remove saves nem altera o OBB.

A primeira versão, com Carbon e destaque laranja, também foi preservada em
`rollback/BOZ-TV-carbon.apk`. O relato de testes acima refere-se a essa primeira versão;
a revisão cartoon será registrada separadamente após a instalação.


## Revisão cartoon e incidente de instalação

A atualização incremental de 24/10, às 18:10, devolveu `Success`, mas o Android
removeu o pacote logo depois (`deletePackageX` e `ACTION_PACKAGE_FULLY_REMOVED`).
As pastas externas de dados e OBB desapareceram junto. O usuário confirmou que
não desinstalou nem limpou dados. A causa interna exata não foi isolada.

Recuperação: reinstalação com `--no-incremental`, reposição de
`obb_moon/blackops_etc.dz` e `obb/blackops_etc.dz.dat`. APK e OBB remotos conferidos
por SHA256; `am start -W` retornou `Status: ok`; processo e tela de abertura
confirmados após a instalação. Não foi encontrado backup dos saves no projeto,
Downloads ou backups locais nem arquivo de save `.i3d` no home. O progresso
anterior não foi recuperado.

**Nunca usar instalação incremental nesta TV.** O build agora desativa a assinatura
v4 e remove o `.idsig` antigo para evitar que adb selecione o modo automaticamente.
Use sempre `adb install --no-incremental -r -g BOZ-TV.apk` e verifique a abertura.
A versão cartoon usa fonte CCAdamKubert e cantoneiras B em todos os menus;
`validation-cartoon.json` contém o hash do APK conferido.


Verificação visual da revisão cartoon: usuário abriu a seleção com 8BitDo; captura
em `production/qa/evidence/2026-10-24-character-menu-cartoon-tv.png` confirma
CCAdamKubert, artes, tarjas e cantoneiras. Logs registraram os quatro personagens
e entrada em seleção de mapas. APK instalado: SHA256
`9b2aee38ed0c696b2b4a901fb54362dc6366de76ea93343259ad2ba17cd94683`.
OBB restaurado: SHA256
`9b053b2d51fd54e7b648365d144f8b4fc6d4ec2a363c2bb654123fce16bbc36c`.


## Idioma e dificuldade

A interface fica em inglês por preferência do usuário (tradução futura). Inclui
títulos, botões, status, dicas de navegação e aviso de conexão do controle. O rótulo
de dificuldade foi removido: não há seletor nessa tela. A configuração interna
fixa do SWF e a fórmula de zumbis por round não foram alteradas.

Atualização em inglês instalada com `--no-incremental -r -g`; `am start -W`
retornou `Status: ok`, processo e tela de abertura confirmados. APK remoto
conferido por SHA256 (`validation-english.json`); OBB permanece presente.
`assets/boz.s3e` e `libs3e_android.so` são byte-idênticos à versão cartoon
anterior, confirmando que esta revisão não alterou a lógica das ondas.
