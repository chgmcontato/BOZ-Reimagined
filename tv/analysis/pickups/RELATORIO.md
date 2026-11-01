# Patches de pickups — BOZ Reimagined

APK final: `/home/carloshgm/Downloads/COD.BOZ/Port/BOZ-TV.apk`. Build concluído, assinatura verificada. Não instalado; sem teste em TV.

## 1. Verificações 1A–1D

- +0x2bd, +0x2be e +0x2bf (701–703): nenhum acesso direto localizado no pseudocódigo jogo-*.c. Escolhido +0x2bd, padding entre o byte original +0x2bc e os próximos campos. Essa evidência estática não substitui teste em TV.
- CLevelManager: slot de singleton 0x4958d0. O stub deriva seu endereço por PC, lê o ponteiro e depois o byte +0x23d. O campo é ativado pelo evento 0x4a2238 e zerado na construção/limpeza do nível (0x1df7e8).
- Destinatário: ApplyEffect(r0=manager,r1=entidade,r2=tipo,r3=extra) → CPlayerController::sub_20895c(entidade) → controller+0x2bd. A consulta é relativa ao destinatário, não à origem do drop.
- console.bin: arquivo binário de entradas tipadas; STR=0x583577e8. Chave LightningBoltLevels em 0x5762 e valor em 0x5776 no arquivo maps/mobile/console.bin original. Valor original callofthedead|proto; novo proto. Remove 13 bytes, com reempacotamento do DTRZ pelo dz.py existente.
- O console extraído em maps/mobile difere previamente do console do OBB em outras configurações. Ambos foram corrigidos individualmente, sem copiar um por cima do outro.

## 2. Diffs e escopo

[Diff completo de código/configuração do build](source-diff.patch). [Bytes e desmontagem dos stubs](binary-diff.txt). [Lista estruturada dos patches](patches.json).

| Hook | Antes | Depois | Stub |
|---|---|---|---|
| 0x20b9de | 84f8bc52 | 05f20fb9 | 0x410c00, 12 bytes |
| 0x21111c | 84f8bc02 | fff180bd | 0x410c20, 22 bytes |
| 0x201110 | f8b50125 | 0ff29ebd | 0x410c50, 32 bytes |
| 0x207d28 | 2de9f041 | 08f2babf | 0x410ca0, 76 bytes |

Todos os hooks ocupam quatro bytes. Os stubs usam bytes previamente zerados, depois do CAVE2 existente, sem aumentar ou realocar a imagem. Cada escrita exige os bytes originais esperados ou os novos bytes para idempotência; divergências abortam.

O construtor zera o latch junto à escrita original de +0x2bc. O hook de vf24 mantém essa escrita e só seta o latch se o valor for não zero. Como esse trecho de vf24 captura a posse ao entrar no estado de queda, também foi necessário marcar o latch na concessão do perk 5 em 0x201110: isso cobre compra e recompensa sem exigir uma queda prévia. A perda posterior do perk não limpa o latch.

O gate na entrada de ApplyEffect só filtra tipo 6. Preserva argumentos e retorna antes de pontuação, temporizador, HUD e troca de arma quando energia ou latch não autorizam. Os tipos diferentes de 6 retomam o prólogo original. A função não devolve um resultado público; o bloqueio retorna com a pilha original intacta.

O ciclo normal da Death Machine NÃO foi filtrado nesta entrega: conforme o passo 2B mais recente, o bloqueio foi implementado na concessão. Assim, um pickup Death Machine sorteado antes dos requisitos pode aparecer e ser recusado na coleta.

Configuração:

```diff
-LightningBoltLevels = callofthedead|proto
+LightningBoltLevels = proto
```

DeathMachineLevels permanece callofthedead|ascension|proto. O caminho do George, incluindo 0x142ce0, sub_205e3c, +0x116 e +0x1b0, não recebeu patches. Índice 7 corresponde a CPickupPerkBottle (vf25 em 0x20222e) e índice 8 a CPickupLightningBolt (vf25 em 0x202234). Nenhuma flag desse caminho foi reinterpretada.

O build chama patch_pickups.py depois de patch_s3e.py e patch_pickups_console.py em seguida. Não houve edição manual do boz.s3e intermediário. O instalador passou a obter OBB_SRC de Port/obb; o destino e a pasta DATA continuam os originais. O script de instalação não foi executado.

## 3. Build e validação

[Saída integral do build](build.log), [hashes anteriores](before.json), [verificação final](verification.json).

```text
I: Built apk into: /home/carloshgm/Downloads/COD.BOZ/Port/build/out/unsigned.apk
OK -> /home/carloshgm/Downloads/COD.BOZ/Port/BOZ-TV.apk
apksigner verify: Verifies; esquemas v1, v2 e v3 válidos
```

| Artefato | SHA-256 antes | SHA-256 depois |
|---|---|---|
| APK | `48bb62f6a39256822b48a286d45da928c55ee44d42933fd98e2ad37f8a657ae7` | `15da0f7f96ae4700748d082e917eec0a3244b29ba83fa4994d5a45934c6ebab3` |
| boz.s3e comprimido no APK | `e93f23f56ad287a11af66ee90355d50d9c30597d7475726078d798cece217f91` | `6441e9410c8e50fc2d6661deba3d8868e864d975305a4b1806b63faed7530b46` |
| boz.s3e descomprimido | `97e48a735329ca4f30dbb382e5821d6632b77bdaf0aa20a44e7f0aece208930b` | `9e4095ace3bd5666f4d5416c52c7609e6c2b68e7d8b4d0c2d4f9e94770c2c1e6` |
| OBB | `4537aabe0be836048bafd7493ac5f18b33c0bc19d6dd0b873533bb953bbd3a91` | `d4f731072a93fb8c96d7c296c830171191ac8a740a382675d9947d7b019829fb` |

- 84 cenários executados em Unicorn ARM/Thumb: gate para nove tipos, combinações energia/latch/espelho ativo, aquisição de sete perks, latch acumulativo e inicialização. Registradores e pilha conferidos. Script: test_patches.py (requer unicorn instalado).
- 144 bytes diferentes entre as imagens descomprimidas anterior e nova; todos pertencem aos intervalos declarados dos novos patches.
- Bytes dos trechos especiais do George e de sub_205e3c idênticos aos anteriores.
- OBB: 221 entradas; somente o payload console.bin mudou. Em cada console, a alteração binária exata foi a troca da string alvo por proto, preservando prefixo e sufixo.
- Aplicação dos patches sobre a imagem já corrigida é idempotente.
- Sintaxe Python, JavaScript e shell validada.
- O primeiro build abortou na validação porque uma entrada opaca do parser resulta em NaN. A comparação foi ajustada sem alterar os dados de câmera; o build subsequente concluiu.

## 4. Frida

Script completo: ../../frida/frida_diag_pickups.js. Instruções de CLI, Gadget e seis cenários estão no cabeçalho. O APK final é de produção, sem Gadget; a execução requer frida-server compatível ou build FRIDA=1. O diagnóstico registra solicitações especiais 7/8, campos do George, elegibilidade da Death Machine por destinatário e solicitações efetivas de spawn provenientes do ciclo normal.

Não existem logs de TV desta entrega. 'Permitido' no diagnóstico significa que o gate autoriza, não prova de que a arma foi equipada.

## 5. Rollback

Script: ../../revert_pickups.sh. Restaura os backups .bak-manual de build.sh, patch_s3e.py, instalador, console e OBB; restaura também o intermediário boz.s3e se houver backup e executa o build original. Os scripts novos permanecem disponíveis para auditoria, mas deixam de ser chamados pelo build restaurado. O script imprime o hash real do APK regenerado. Não foi executado, pois desfaria esta entrega.

Há também BOZ-TV.apk.bak-manual com o hash anterior acima. A alternativa git reset --hard checkpoint-antes-drops está documentada como destrutiva, nunca é executada automaticamente e não restaura os binários ignorados pelo Git.

## 6. Limitações para teste

O latch novo não é serializado no Save & Quit. Se o controller for recriado após salvar com Quick Revive já perdido, o histórico não será recuperado. A posse ativa restaurada pelo caminho de concessão pode marcar novamente o latch, mas isso não restaura posse histórica já perdida. Corrigir persistência exige uma extensão própria do save e não foi incluído silenciosamente.

O gate foi testado por emulação, não por gameplay. É necessário validar aquisição, perda do perk, novo jogo e coleta real na TV. A energia e o latch não alteram os outros casos de ApplyEffect; o mapa proto mantém sua elegibilidade normal de raio.
