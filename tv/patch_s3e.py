#!/usr/bin/env python3
"""Ajusta a config (.icf) embutida no código do jogo (assets/boz.s3e, LZMA "alone").
Cada troca mantém o mesmo tamanho em bytes, para não deslocar nada no binário.
Uso: patch_s3e.py <boz.s3e>"""
import lzma
import os
import struct
import sys

IMAGE = 0x43F3   # início do código/dados do jogo no binário descomprimido

# código novo (Thumb) num trecho da área de dados que fica sempre zerado (0x40f12d..0x41108f, conferido com o
# jogo rodando; a imagem é carregada rwx). Montado com o keystone; endereços absolutos:
#   0x410800 buy:    (compra de tático, r4 = jogador, r5 = item novo) guarda o jogador em bank+8; o tático atual
#                    vai para o banco; se o novo já está no banco, os dois trocam e o novo é reabastecido (0x2555c4)
#   0x410830 reset1, 0x410840 reset2: inventário zerado -> banco zerado, jogador em bank+8
#   0x410854 cmd:    entrada do tratador de comandos (r1 = id). 17 (toggleFreeMode, sem uso; direcional
#                    direito) marca bank+12 = "claymore" e seleciona a claymore, sem o tratador original.
#                    11 (+tacticalGrenade): quer a claymore se bank+12 marcado (e desmarca), senão a outra
#                    (Monkey Bomb); se o jogador não tem o item pedido, o comando é ignorado (cada botão só
#                    usa o seu item); senão segue no tratador original.
#   0x41088a select: r0 = quer claymore? troca tático (item e quantidade) com o banco se preciso; r0 = 1 se o
#                    tático agora é o item pedido
#   0x4108ce maxammo: (Max Ammo, 0x2540a8, só recarregava letal e tático) item do banco volta ao máximo
#                    (claymore 2, outros 3, como 0x252ecc); depois ldr.w r3,[r4,#0x1f0]; b.w 0x2540bc
#   0x4108ec hash do "claymore" (djb2), 0x4108f0 banco: item, quantidade, jogador, marca (Hud.java lê aqui)
CAVE = 0x410800
CAVE_CODE = bytes.fromhex(
    "d4f8f0613aa39c6086b1ae420ed01a68aa4207d11e60d4f8d0215a60c4f8f05144f6d0be1e60d4f8d0215a6044f6b3be"
    "c4f8f0312ea2136053609460d360f8bdc4f8f031c4f8d03129a2136053609460d360f8bd112906d10fb525a30120d860"
    "00f013f80fbd0b290ad10fb520a3d8680022da6000f009f80028bde80f4003d04b1ef7b5cbf5d4ba704719a39968e1b1"
    "d1f8f021cab1d26ddff850c062450cbf0122002282420ed01a6872b1d1f8f0c1c1f8f021c3f800c0d1f8d0c15a68c1f8"
    "d021c3f804c0012070470020704708a31a683ab1d26ddff814c062450cbf022203225a60d4f8f03143f6e8bb01e5c93d"
    "00000000000000000000000000000000")

# mais código novo, logo depois do CAVE (mesma área zerada), montado com o keystone:
#   0x410900 zcount(this, R): zumbis do round R = BO1 solo * 15/24 (mesmo nº de hordas do BO1 com a horda de 15
#                    do jogo), arredondado (.5 para cima): R1-9 = 4, 5, 8, 11, 15, 17, 18, 18, 18 (BO1 6, 8, 13, 18, 24,
#                    27, 28, 28, 29); R10+ = (2400 + 9R^2 + 80) / 160 (R10 = 21, R20 = 38, R30 = 66, R100 = 578);
#                    sem teto (R limitado a 20000 só para a conta não estourar 32 bits)
#   0x410944 ztotal(this, R, especiais): zcount + (especiais ? vt+0x58, contagem de especiais do original : 0)
#   0x410962 zmax: máximo vivo = 24 (sem uso: com 24 vivos o jogo fechava na TV, SIGSEGV; fica o original, 15)
#   0x410966 getthr: limiar de pontos atual (0 = ainda não lido -> PickupPointsThreshold do console.bin)
#   0x410976 thrhook: r2 = getthr, para o teste de drop por pontos
#   0x410980 dreset: zera ciclo e limiar e segue no reset original (0x206cf4)
#   0x41098c hbit(hash): 1 << índice do tipo (0-8; tabela de hashes dos tipos em 0x4a171c, preenchida em tempo de
#                    execução). Tipos: 0 Insta-Kill, 1 Double Points, 2 Max Ammo, 3 Nuke, 4 Carpenter, 5 Fire Sale,
#                    6 Death Machine, 8 Lightning Bolt (switch do efeito em 0x207d48)
#   0x4109b0 rcount(nó) / 0x4109e0 wcount: janelas abertas = CBarricade com todas as tábuas arrancadas
#                    ([+0x80] = 0, [+0x98] total > 0, sem m_StartBroken [+0x94]); percorre a árvore da
#                    CIsComponentTable<CBarricade> (0x496858: cabeça em +4, raiz = cabeça+4; nó: +8/+0xc filhos,
#                    +0x14 componente)
#   0x4109f8 dcycle: ciclo do BO1. r5 = hash sorteado; candidatos válidos (hashes) em [sp+0x10] (n em [sp+0x14]).
#                    Carpenter bloqueado com menos de 5 janelas abertas (como no BO1). Se r5 já saiu neste ciclo
#                    (ou está bloqueado), sorteia entre os candidatos livres; sem nenhum, novo ciclo (máscara
#                    zerada). Marca r5 na máscara. Se o drop foi por pontos ([r4+0x80] > limiar), limiar *= 1.14.
#                    Depois refaz o ldr.w r8 trocado e volta a 0x205fc6.
#   0x410ac0 dados: máscara do ciclo, limiar atual, máscara de bloqueados
#   0x410ad0 savehook / 0x410b02 loadhook: banco do tático no save (ver os patches em 0x257d78 e 0x257e64)
CAVE2 = 0x410900
CAVE2_CODE = bytes.fromhex(
    "092902d80ba2505c704744f62062914288bf1146084648430922504340f6b012104440094cf6cd42ccf6cc42a0fb0220"
    "80087047040405080b0f1112121200000000000070b504460d461646fff7d8ff36b1064623689b6d2046294698473044"
    "70bd1820704756a1486818b9d4f8d80000694860704700b5fff7f5ff024600bd4fa2002313605360f6f5b4b906b540f6"
    "8251c0f209017944002251f822e0864502d00132092af8d1012000fa02f006bd30b5002504468cb1a068fff7f9ff0544"
    "616949b1d1f8982032b191f894201ab9d1f8802002b90135e468ece7284630bd00b545f66a60c0f208007844406810b1"
    "4068fff7ddff00bdfff7f2ff0021052838bf10212ea291602da2136891680b432846fff7bbff184233d0ddf810a0ddf8"
    "14b000214ff00008594509d25af82100fff7acff184208bf08f101080131f3e7b8f1000f06d11ea21068002819d00020"
    "1060d9e708b46ff6f4ea41469df7d2eb08bc4ff0000c5af82c50284602b4fff78dff02bc184201d119b101390cf1010c"
    "f1e72846fff782ff0da2136803431360fff769ffd4f8801081420ad97221484348f21f51c5f2eb11a0fb0101490904a2"
    "51604bf2cc68c0f22908f5f584ba00bf0000000000000000000000000000000007b5102d10d1d9f8e00168b1c06b58b9"
    "4ff60461cff6ff7179440868002200b1c26d10924a681192bde8074005a841467047042d0ed10c9961b1204642f6f9f9"
    "40b14ff6d251cff6ff71794408600d9a4a608c60013547f676b9")

# BEGIN GENERATED FPS (assemble_fps.py + fps.s)
FPS_CODE = [
    # FPS deadline/yield trampoline
    (0x410600, bytes(78), bytes.fromhex(
        "1fb50ff27c04002025f4cce925f4a0ebe2688ab122686368821a71eb03030bd353b9a368022b0cbf222321239a4203d2"
        "981a25f4b8e9e9e720606160a268521c032a28bf0022a2600122e2601fbd"
    )),
    # FPS timestamps/phase (reserved)
    (0x410680, bytes(16), bytes.fromhex(
        "00000000000000000000000000000000"
    )),
    # FPS free-step yield hook
    (0xfbc36, bytes.fromhex("002039f7b4ee"), bytes.fromhex(
        "14f3e3fc00bf"
    )),
]
# END GENERATED FPS

# BEGIN GENERATED TIMER (assemble_timer.py + timer.s)
TIMER_CODE = [
    # timer code
    (0x410000, bytes(1224), bytes.fromhex(
        "014878440068704706610800034878440068002818bfd0f8f4007047c658080010b582b00446fff7ebff014623460022"
        "684621f6b6fd009802b010bd70b50ff2c064fff7ddff98b146f2ee30caf6a830fff7e6ff456942f23f00c8f6e140fff7"
        "dfff46696561a661fff7d0ffe06170bd00206061a06170bd10b50ff28464a06801280cd0fff7daff25f462eea062e162"
        "0020e0602061606220620120a06010bd10b50ff25c64a068012820d1e06b012805d1606ba16be06021610020e06325f4"
        "48eea26ae36aa062e162801a99410ed300290cd1b0f1fa0f09d8226b002a06d1e2682369121843f10003e260236110bd"
        "00224023400049415241b2f57a7f03d3a2f2e83240f001005b1ef3d170472de9f0410ff2ec54a068012847d1fff7c0ff"
        "0020a060a062e062fff76aff00b18046fff76cffe16988423dd146f2ee30caf6a830fff76dff054642f23f00c8f6e140"
        "fff766ff06467269b2f57a7f28bf0022e0682169636bc01aa36b994101d20020002100236363a363801841f10001fff7"
        "b7ff4ff6ff77c7f6ff77002904d16b69c01801d2b84200d93846686172616061a2614046002119f617ff6062bde8f081"
        "05206062fae706206062f7e710b5fff7a6ff0ff23c540020a060e06020612062a062e0626063a063e06310bd70b50446"
        "0ff21c550ff21e01acf58efe064620460ff22101acf588fe304318bf01202862fff71cff70bd706c617965725f70726f"
        "66696c650070726f66696c655f737461747300002de90f500ff2d44cdcf804305b1cccf80430bff35b8ffff7bfffbff3"
        "5b8f0ff2bc4cdcf804305b1cccf80430bde80f50b6f54ebd2de90f500ff2a04cdcf804305b1cccf80430bff35b8ffff7"
        "03ffbff35b8f0ff2884cdcf804305b1cccf80430bde80f50c5f52ab82de90f500ff26c4cdcf804305b1cccf80430bff3"
        "5b8ffff7fdfebff35b8f0ff2544cdcf804305b1cccf80430bde80f50b7f51abd2de90f500ff2384cdcf804305b1cccf8"
        "0430bff35b8ffff71affbff35b8f0ff2204cdcf804305b1cccf80430bde80f50c8f5d0bb2de90f500ff2044cdcf80430"
        "5b1cccf80430bff35b8ffff700ffbff35b8f0ff2ec3cdcf804305b1cccf80430bde80f50b6f55cba2de90f500ff2d03c"
        "dcf804305b1cccf80430bff35b8ffff73dffbff35b8f0ff2b83cdcf804305b1cccf80430bde80f5062f5d0bb2de90f50"
        "0ff29c3cdcf804305b1cccf80430bff35b8ffff781febff35b8f0ff2843cdcf804305b1cccf80430bde80f506cf56eb9"
        "2de90f500ff2683cdcf804305b1cccf80430bff35b8ffff77bfebff35b8f0ff2503cdcf804305b1cccf80430bde80f50"
        "62f5f0b82de90f500ff2343cdcf804305b1cccf80430bff35b8ffff798febff35b8f0ff21c3cdcf804305b1cccf80430"
        "bde80f506cf52aba2de90f500ff2003cdcf804305b1cccf80430bff35b8ffff77efebff35b8f0ff2e82cdcf804305b1c"
        "ccf80430bde80f5061f5d8bf2de90f500ff2cc2cdcf804305b1cccf80430bff35b8ffff7cbfebff35b8f0ff2b42cdcf8"
        "04305b1cccf80430bde80f50adf5aeb82de90f500ff2982cdcf804305b1cccf80430bff35b8ffff7a1febff35b8f0ff2"
        "802cdcf804305b1cccf80430bde80f50cff5aab92de90f500ff2642cdcf804305b1cccf80430bff35b8ffff787febff3"
        "5b8f0ff24c2cdcf804305b1cccf80430bde80f5073f588bf"
    )),
    # timer state v1
    (0x410700, bytes(64), bytes.fromhex(
        "314d49540000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "00000000000000000000000000000000"
    )),
    # classic_enter
    (0x42f714, bytes.fromhex("651c194a"), bytes.fromhex(
        "95b13d4a"
    )),
    # classic_play
    (0x431180, bytes.fromhex("51021a4a"), bytes.fromhex(
        "c9b13d4a"
    )),
    # classic_tick
    (0x42f71c, bytes.fromhex("652c194a"), bytes.fromhex(
        "fdb13d4a"
    )),
    # classic_results
    (0x431548, bytes.fromhex("053a1a4a"), bytes.fromhex(
        "31b23d4a"
    )),
    # classic_exit
    (0x42f718, bytes.fromhex("5117194a"), bytes.fromhex(
        "65b23d4a"
    )),
    # deadops_enter
    (0x42754c, bytes.fromhex("6dda134a"), bytes.fromhex(
        "99b23d4a"
    )),
    # deadops_play
    (0x4285c8, bytes.fromhex("dd75144a"), bytes.fromhex(
        "cdb23d4a"
    )),
    # deadops_tick
    (0x427554, bytes.fromhex("15d5134a"), bytes.fromhex(
        "01b33d4a"
    )),
    # deadops_results
    (0x428708, bytes.fromhex("bd77144a"), bytes.fromhex(
        "35b33d4a"
    )),
    # deadops_exit
    (0x427550, bytes.fromhex("4dd3134a"), bytes.fromhex(
        "69b33d4a"
    )),
    # profile_screen
    (0x42ec90, bytes.fromhex("2d85184a"), bytes.fromhex(
        "9db33d4a"
    )),
    # classic_restart
    (0x1e0430, bytes.fromhex("fff7daf9"), bytes.fromhex(
        "30f216f8"
    )),
    # deadops_restart
    (0x17b4ce, bytes.fromhex("08f083ff"), bytes.fromhex(
        "94f2e1ff"
    )),
]
# END GENERATED TIMER

# patches de código (offset na imagem, bytes originais, bytes novos)
CODE = [
    # tratador de eventos de jogo: morte -> SetState(11 = tela "Quick Revive / slide to spend")
    # vira SetState(4 = Game Over/resultados), igual a apertar QUIT na tela de reviver
    (0x1C7F2C, bytes.fromhex("0b21"), bytes.fromhex("0421")),
    # comando -reloadChangeWeapon (id 5) ao soltar o X: o toque rápido (<150 ms) esperava um 2º toque
    # (toque duplo = trocar arma) e não recarregava -> vai direto ao caminho do recarregar
    (0x1DBFC6, bytes.fromhex("40f38d80"), bytes.fromhex("00f08db8")),   # ble.w -> b.w 0x1dc0e4
    # esse caminho só recarregava se [+0x54] > 0 ou [+0x58] == 0 (estado do botão de toque) -> sempre
    (0x1DC0E8, bytes.fromhex("05dc"), bytes.fromhex("05e0")),           # bgt -> b 0x1dc0f6
    # depois de recarregar o jogo zerava [+0x51] e só rearmava ao segurar o botão -> não desarma
    (0x1DC116, bytes.fromhex("84f85150"), bytes.fromhex("aff30080")),   # strb.w r5,[r4,#0x51] -> nop.w
    # [+0x51] começa 0 e só vira 1 no vf2 (0x1dc48c) depois de segurar o botão (UICallReleaseTime): o primeiro
    # recarregar da partida não acontecia até uma segurada longa -> recarrega sem depender de [+0x51]
    (0x1DC0FA, bytes.fromhex("73b1"), bytes.fromhex("00bf")),           # cbz r3,0x1dc11a -> nop
    # mira de segurar: o comando "aim" (id 8) só liga/alterna. O alternateFire (id 15, sem uso) vira
    # "sair da mira": chama o setter da mira (0x1dc28c) com 0; o controle o dispara ao soltar o LT
    (0x1DBEFE, bytes.fromhex("8e490122002384f84b20"),
     bytes.fromhex("2046002100f0c3f908e1")),   # mov r0,r4; movs r1,#0; bl 0x1dc28c; b 0x1dc11a
    # HUD sem botões de toque (CHUD, 0x1cbe58): cada botão era mostrado conforme a opção de canhoto
    # [+0x68] (normal = !canhoto, _lefty = canhoto). Mira, agachar, recarregar/trocar, faca, Button/Image
    # e os dois analógicos virtuais passam a "esconder": ldrb.w rX,[r4,#0x68] -> movs rX,#0 e o eor #1
    # seguinte -> nop.w. A granada fica (mostra quantas restam).
    (0x1CC078, bytes.fromhex("90f86820"), bytes.fromhex("002200bf")),
    (0x1CC080, bytes.fromhex("82f00102"), bytes.fromhex("aff30080")),
    (0x1CC08A, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC092, bytes.fromhex("82f00102"), bytes.fromhex("aff30080")),
    (0x1CC09E, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC0A6, bytes.fromhex("82f00102"), bytes.fromhex("aff30080")),
    (0x1CC0B6, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC0C6, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC0D6, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC136, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC13A, bytes.fromhex("81f00101"), bytes.fromhex("aff30080")),
    (0x1CC152, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC156, bytes.fromhex("81f00101"), bytes.fromhex("aff30080")),
    (0x1CC186, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC19E, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC1BE, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC1C2, bytes.fromhex("81f00101"), bytes.fromhex("aff30080")),
    (0x1CC1DA, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC1DE, bytes.fromhex("81f00101"), bytes.fromhex("aff30080")),
    (0x1CC1F6, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC20E, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC226, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC230, bytes.fromhex("82f00102"), bytes.fromhex("aff30080")),
    (0x1CC23A, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC242, bytes.fromhex("82f00102"), bytes.fromhex("aff30080")),
    (0x1CC252, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC262, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC26E, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC276, bytes.fromhex("82f00102"), bytes.fromhex("aff30080")),
    (0x1CC282, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC28A, bytes.fromhex("82f00102"), bytes.fromhex("aff30080")),
    (0x1CC29A, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC2AA, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC2CE, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC2D2, bytes.fromhex("81f00101"), bytes.fromhex("aff30080")),
    (0x1CC2DA, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC2E4, bytes.fromhex("82f00102"), bytes.fromhex("aff30080")),
    (0x1CC300, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC304, bytes.fromhex("81f00101"), bytes.fromhex("aff30080")),
    (0x1CC30C, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC316, bytes.fromhex("82f00102"), bytes.fromhex("aff30080")),
    (0x1CC332, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC33E, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    (0x1CC35C, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC368, bytes.fromhex("94f86820"), bytes.fromhex("002200bf")),
    # a granada também sai: o indicador do jogo alterna entre letal e tático; o app (Hud.java) desenha um para
    # cada item (frag, Monkey Bomb, claymore). grenade_button: ldrb.w r1,[r4,#0x68]; eor r1,#1 -> movs r1,#0; nop
    (0x1CC11A, bytes.fromhex("94f86810"), bytes.fromhex("002100bf")),
    (0x1CC11E, bytes.fromhex("81f00101"), bytes.fromhex("aff30080")),
    # sem COD Points: com BuyWithCODPointsActive=0 o "sem pontos" mostrava o Buy_COD_points_element
    # ("TAP TO BUY COD POINTS / ... IN THE BANK", com o texto "NOT ENOUGH POINTS. X [preço]" dentro dele)
    # -> sempre o caminho da mensagem normal (0x1d4a60 com o texto de "sem pontos"), sem o botão
    (0x1D4BFC, bytes.fromhex("37d1"), bytes.fromhex("37e0")),           # bne 0x1d4c6e -> b 0x1d4c6e
    # saldo COD_points_balance ("... IN YOUR ACCOUNT") na pausa e na partida: SetVisible(1) -> (0)
    (0x1D1EDE, bytes.fromhex("0121"), bytes.fromhex("0021")),
    (0x1D4044, bytes.fromhex("0121"), bytes.fromhex("0021")),
    # mapas novos (p.ex. "moon"): o START só iniciava deadops/kino/tutorial/ascension/callofthedead
    (0x1BFC10, bytes.fromhex("10d1"), bytes.fromhex("00bf")),           # bne 0x1bfc34 -> nop
    # a tabela id -> nome dos mapas (0x1bfe10..) só tem tutorial/kino/proto/ascension/deadops/callofthedead/
    # cotdeadops; um mapa fora dela vira nome "" e a fase (CLevel) não é criada. O "proto" (mapa de teste,
    # fora do menu) vira "moon": entra na tabela e, em 0x1ddc4c, ganha o tratamento do kino (o clone é o kino)
    (0x3E395F, b"proto\0", b"moon\0\0"),
    # Dead Ops liberado de saída: 0x2296c4 (save+0xf8) devolve 1 só com as 4 moedas dos menus coletadas
    # (bytes save+0x21..0x24); é ele que o menu (deadopsUnlocked) consulta -> movs r0,#1; bx lr
    (0x2296C4, bytes.fromhex("002310b5"), bytes.fromhex("01207047")),
    # classe do aparelho (0x245c78: 2 = smartphone, 3 = tablet). A TV caía em tablet pelo TabletThreshold e
    # usava o HUD menor; fixa smartphone (HUD maior): ldr.w r0,[r3,#0x40c] -> movs r0,#2; nop
    (0x245CE0, bytes.fromhex("d3f80c04"), bytes.fromhex("022000bf")),
    # interação (porta, barricada, compra): no Xperia o jogo abria a página ig_poiXperia, um botão X redondo
    # enorme no meio da tela; o texto "[A] ..." já diz o botão -> não abre a página: cbz -> b 0x1d4886
    (0x1D4860, bytes.fromhex("88b1"), bytes.fromhex("11e0")),
    # contador de FPS do IwGx (DisplayFramerate, sempre ligado; ver CONFIG): no fim do quadro o motor chama
    # IwGxPrintFrameRate(0, 0) (0x2ad888) -> canto superior esquerdo. Passa a desenhar em (FPS_X, FPS_Y):
    # mov r0,#0; mov r1,r0 -> movw r0,#FPS_X; movw r1,#FPS_Y
    (0x2AACF8, bytes.fromhex("0000a0e30010a0e1"), None),
    # texto "`aFPS: %u.%02u" (`a = código de formatação do IwGxPrint) -> "FPS %u" (a fração é ignorada)
    (0x3FEF3E, b"`aFPS: %u.%02u\0", b"FPS %u".ljust(15, b"\0")),
    # escala do texto de debug do IwGx (dado inicial em 0x450970, letra de 8 px x escala): 1 -> 2
    (0x450970, bytes.fromhex("01000000"), bytes.fromhex("02000000")),
    # 3º slot (claymore e Monkey Bomb juntas; desligado em 27/10, ver abaixo): o jogador tem um só item tático ([+0x1f0] item,
    # [+0x1d0] quantidade) e a compra (0x255592) descartava o anterior. Código novo em CAVE (ver acima): o
    # outro tático fica num "banco"; o direcional direito (tecla 3 -> toggleFreeMode, patch_binds.py) usa a
    # claymore e o LB (+tacticalGrenade) usa a Monkey Bomb.
    (CAVE, bytes(len(CAVE_CODE)), CAVE_CODE),
    # 27/10: de volta a UM tático, como no jogo original (pedido do usuário): a compra só guarda o jogador e segue o
    # caminho original, que descarta o tático anterior (buy: cbz r6 -> b), e o select não troca mais com o banco
    # (devolve "não é o item deste botão"). Continua: direcional direito só usa a claymore; LB só o tático que não
    # é claymore (Monkey Bomb, Gersh = bh_bomb, Matryoshka = matryoshka_doll).
    (0x410808, bytes.fromhex("86b1"), bytes.fromhex("10e0")),           # cbz r6,0x41082c -> b 0x41082c
    (0x4108A8, bytes.fromhex("1a6872b1"), bytes.fromhex("00207047")),   # banco -> movs r0,#0; bx lr
    (CAVE2, bytes(len(CAVE2_CODE)), CAVE2_CODE),
    # zumbis por round como no BO1 solo (ver CAVE2): a contagem (vt+0x54) e contagem+especiais (vt+0x50) do CWave (vtable 0x43ab80, rounds 1-10 dos dados do mapa) e do CWaveRepeatable
    # (0x43ac48, round 11+; era base * mult^(R-início) = 30 * 1.0^n, ou seja 30 para sempre). Os rounds especiais
    # (cachorros, CWaveSpeciality 0x43ad10) ficam com o original. Entrada = 0x4a000000 + endereço - 0x35090 + 1.
    (0x43ABD0, bytes.fromhex("3942214a"), bytes.fromhex("b5b83d4a")),   # CWave total -> ztotal
    (0x43ABD4, bytes.fromhex("4742214a"), bytes.fromhex("71b83d4a")),   # CWave contagem -> zcount
    (0x43AC98, bytes.fromhex("8b48214a"), bytes.fromhex("b5b83d4a")),   # CWaveRepeatable total
    (0x43AC9C, bytes.fromhex("d546214a"), bytes.fromhex("71b83d4a")),   # CWaveRepeatable contagem
    # drops como no BO1. Escolha do tipo (0x205eb0): depois do sorteio por peso, ldr.w r8,[pc,#0xd4] -> b.w dcycle
    (0x205FC2, bytes.fromhex("dff8d480"), bytes.fromhex("0af219bd")),
    # drop por pontos (0x2060a8): o limiar era o PickupPointsThreshold fixo; ldr.w r2,[r4,#0xd8] -> bl thrhook
    # (r2 = limiar atual do CAVE2) e o ldr r2,[r2,#0x10] seguinte -> nop
    (0x206144, bytes.fromhex("d4f8d820"), bytes.fromhex("0af217fc")),
    (0x20614C, bytes.fromhex("1269"), bytes.fromhex("00bf")),
    # carga da fase (0x2072e0): bl 0x206cf4 (reset do gerenciador de pickups) -> bl dreset (zera ciclo e limiar)
    (0x2072E6, bytes.fromhex("fff705fd"), bytes.fromhex("09f24bfb")),
    # (removido em 27/10 com o banco) Save & Quit com claymore e Monkey Bomb: o save (0x257d3c) grava um registro por slot (hash [+0x5c], munição
    # [+0x2c]/[+0x30], anexo [+0x3c] -> sp+0x30/0x34) e o Continue (0x257da4) recria cada item pelo hash; o item do
    # banco (CAVE) se perdia. No registro do tático (slot 4, sem anexo) vão o hash e a quantidade do banco:
    # add r0,sp,#0x14; mov r1,r8 -> bl savehook; e no Continue, adds r5,#1; b 0x257e16 -> b.w loadhook (recria o
    # item pelo hash, 0x252f02, e devolve ao banco)
    # Zumbi preso (26/10, pedido do usuário; o BO1 da Steam faz com 30 s): no estado "andando" (+0xfd = 2 ou 16), se
    # o zumbi não sai de um raio de 64 unidades por 15 s, recebe dano = vida + 1000 pelo CHealth (morte normal,
    # conta no round). Código em 0x410de0 (build/analysis/stuck/assemble_stuck.py), tabela de 32 zumbis em
    # 0x40f200; entra pela vf27 da vtable do CAIControllerZombie (update por quadro) e segue no vf27 original.
    (0x410de0, bytes(244), bytes.fromhex("f0b5044624f4b4ef0646394d7d44002320222968a14210d033b921b16968711ab1f5fa6f00d92b461835013af1d1002b58d01d462c6000216960e068002851d0e6f428fb07004dd069686e60711ab1f57a7f40d894f8fd10022901d010293ad197ed170ad7ed180a97ed191ad5ed031a95ed042ad5ed052a70ee611a30eec22a71ee622a61eea11a42ee021a42eea21a40f20001c4f2805103ee101af4eec31af1ee10fa17dca968711a43f69822914218d9e068bdf586fc68b190ed120a40f20001c4f27a4100ee901a30ee200a10ee101abdf5affdae60f86de860386e2861786e68612046bde8f04036f50fbf00bf10e4ffff")),
    (0x423744, bytes.fromhex("5d2c114a"), bytes.fromhex("51bd3d4a")),   # vtable vf27 -> stuck
    # Crash no som (26/10, jogo fechou sozinho, thread principal): o tratador de "som terminou" (0x10d8b0) lia
    # [[canal+0xc]+0x2c] com os dados do som já liberados (nulo). ldr r2,[r1,#0xc]; ldr r1,[r2,#0x2c] -> b.w 0x410ee0:
    # dados nulos -> 0x10d956 (marca o canal parado e devolve "não repetir"); senão as mesmas leituras e volta a 0x10d8ca
    (0x410EE0, bytes(14), bytes.fromhex("ca680ab9fcf437bdd16afcf4eebc")),
    (0x10D8C6, bytes.fromhex("ca68d16a"), bytes.fromhex("03f30bbb")),
    (0x255592, bytes.fromhex("d4f8f061"), bytes.fromhex("bbf135b9")),   # ldr.w r6,[r4,#0x1f0] -> b.w buy
    (0x25715E, bytes.fromhex("c4f8f031"), bytes.fromhex("b9f167bb")),   # str.w r3,[r4,#0x1f0] -> b.w reset1
    (0x255BA4, bytes.fromhex("c4f8f031"), bytes.fromhex("baf14cbe")),   # str.w r3,[r4,#0x1f0] -> b.w reset2
    (0x1DBE2C, bytes.fromhex("4b1ef7b5"), bytes.fromhex("34f212bd")),   # subs r3,r1,#1; push -> b.w cmd
    # Nome do jogador (P1). O jogo não carrega um profile_0.i3d escrito de fora: cada save tem um 2º checksum
    # num registro à parte (0x15c1f0/0x15c1bc) e, se não bate, o arquivo é apagado e volta "Player". Então o
    # Java grava só data-gles1/boz_name.txt (o s3eFileOpen precisa do prefixo data-gles1/) (ProfileSave.java) e o código nativo lê esse arquivo:
    #   0x410d00 readname: r0 = nome (lido uma vez com s3eFileOpen/Read/Close, até 15 letras) ou "PLAYER"
    #   0x410d36 profname: chama o setProfile original (0x229114) e copia o nome no perfil (CSaveManager+0x4c),
    #            que a tela Player Profile mostra e o jogo salva com o checksum certo
    # Co-op (CGameNetwork, 0x1c44d8, criado antes do CSaveManager): era snprintf(buf, 14, "Player-%d",
    # rand() >> 4), um número sorteado ("PLAYER-1391"); vira snprintf(buf, 16, "%s", readname())
    (0x410D00, bytes(140), bytes.fromhex("30b515a4207880b917a01da124f4e8eb58b10546204601210f222b4624f476ed00212154284624f4b8eb207808b1204630bd14a030bd10b5044618f6ebf9fff7dfff10a1884205d010f8012b04f8012b002af9d110bd00bf00000000000000000000000000000000646174612d676c6573312f626f7a5f6e616d652e7478740072620000504c415945520000")),
    (0x22A1DA, bytes.fromhex("fef79bff"), bytes.fromhex("e6f1acfd")),   # bl setProfile -> bl profname
    (0x1C468E, bytes.fromhex("bbf0d8ec"), bytes.fromhex("4cf237fb")),   # blx rand -> bl readname
    (0x1C4694, bytes.fromhex("0e21"), bytes.fromhex("1021")),           # movs r1,#14 -> #16 (buffer de 16)
    (0x1C4698, bytes.fromhex("0311"), bytes.fromhex("0346")),           # asrs r3,r0,#4 -> mov r3,r0
    (0x3E41C1, b"Player-%d\x00", b"%s" + bytes(8)),                    # formato (só usado aqui)
]

# Recursos independentes; desative um com FPS30=0 ou MATCH_TIMER=0 no build.
if os.environ.get("FPS30", "1") != "0":
    CODE.extend(FPS_CODE)
if os.environ.get("MATCH_TIMER", "1") != "0":
    CODE.extend(TIMER_CODE)

# posição do contador de FPS, em pixels da tela (1280x720); "FPS 50" na escala 2 ocupa 96x16
FPS_X, FPS_Y = 592, 698   # rodapé central: no topo encostava no "2x" dos power-ups
def movw(rd, imm):
    return (0xE3000000 | (imm >> 12) << 16 | rd << 12 | (imm & 0xFFF)).to_bytes(4, "little")
CODE = [(o, a, movw(0, FPS_X) + movw(1, FPS_Y) if b is None else b) for o, a, b in CODE]
# TABLET=1 (teste): sem o patch de classe smartphone (0x245CE0) -> HUD de tablet, o original na TV
if os.environ.get("TABLET") == "1":
    CODE = [c for c in CODE if c[0] != 0x245CE0]

CONFIG = [
    # sem compras com COD Points dentro da partida: some a tela "Quick Revive / slide to spend"
    (b"BuyWithCODPointsActive=1", b"BuyWithCODPointsActive=0"),
    # sem o botão da loja de COD Points nos menus
    (b"EnableFrontEndMTXStore=1", b"EnableFrontEndMTXStore=0"),
    # contador de FPS nativo do IwGx, no lugar de um comentário do [GX] (posição/texto/escala: ver CODE)
    (b"Rasterisation=HW\t\t\t# Use HW rasterisation", b"Rasterisation=HW\r\nDisplayFramerate=1     "),
]

path = sys.argv[1]  # opcional: --raw grava sem compressão
packed = open(path, "rb").read()
props, dict_size, size = packed[0], struct.unpack_from("<I", packed, 1)[0], struct.unpack_from("<Q", packed, 5)[0]
raw = lzma.LZMADecompressor(lzma.FORMAT_ALONE).decompress(packed)
assert len(raw) == size, (len(raw), size)

# LOWEND=1 (teste de desempenho): liga o modo LowEndDevice do jogo (era só do iPhone 3GS/iPad 1), no lugar de um
# comentário do [GAME]; esconde objetos marcados m_VisibleOnLowEndDevice=0 e usa as variantes _lowend
if os.environ.get("LOWEND") == "1":
    old = b"GuiBucketSize=2500000\t\t\t# this is getting use for IwUI"
    new = b"GuiBucketSize=2500000\r\nLowEndDevice=1"
    CONFIG.append((old, new + b" " * (len(old) - len(new))))


for old, new in CONFIG:
    assert len(old) == len(new)
    n = raw.count(old)
    if n == 0 and raw.count(new):
        print("já aplicado:", new.decode())
        continue
    assert n == 1, (old, n)
    raw = raw.replace(old, new)
    print("config:", new.decode())

raw = bytearray(raw)
for off, old, new in CODE:
    cur = bytes(raw[IMAGE + off:IMAGE + off + len(old)])
    if cur == new:
        print("já aplicado: código %#x" % off)
        continue
    assert cur == old, "bytes inesperados em %#x: %s" % (off, cur.hex())
    raw[IMAGE + off:IMAGE + off + len(new)] = new
    print("código %#x: %s -> %s" % (off, old.hex(), new.hex()))
raw = bytes(raw)

if "--raw" in sys.argv:
    open(path, "wb").write(raw)
    print("%s: gravado descomprimido (%d bytes)" % (path, len(raw)))
    sys.exit(0)
out = lzma.compress(raw, format=lzma.FORMAT_ALONE,
                    filters=[{"id": lzma.FILTER_LZMA1, "dict_size": dict_size, "lc": 3, "lp": 0, "pb": 2}])
# o loader do Marmalade espera o tamanho real no cabeçalho (o Python grava "desconhecido")
out = out[:5] + struct.pack("<Q", size) + out[13:]
assert out[0] == props, (hex(out[0]), hex(props))
assert lzma.LZMADecompressor(lzma.FORMAT_ALONE).decompress(out) == raw
open(path, "wb").write(out)
print("%s: %d -> %d bytes" % (path, len(packed), len(out)))
