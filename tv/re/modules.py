"""Divisão do código do jogo em módulos (subsistemas): regras por nome de classe e faixas de endereço.

Cada módulo tem um id (nome do arquivo em docs/re/), um título e uma camada:
    jogo      código próprio do BOZ (classes C*, compiladas em ordem alfabética em 0x138000..0x258000)
    is        framework "Is" da Ideaworks Studios (CIs*: entidades, componentes, reflexão, setores, nav)
    marmalade SDK Marmalade (CIw*/Iw*: IwGx, IwGL, IwUI, IwGraphics, IwResManager, IwSound, IwNetwork...)
    terceiros bibliotecas de terceiros (Bullet, Recast, gameswf, DemonWare, STLport, libc, zlib, png, jpeg)
"""
import re

MODULES = [
    # id, camada, título
    ('jogo-estados', 'jogo', 'Estados do jogo, menus e telas (CGame, CGameState*, CIngameState*, CFrontendState*)'),
    ('jogo-hud-ui', 'jogo', 'HUD e interface própria (CHUDManager, CUI*, CGui*, CTouch*, toque)'),
    ('jogo-input', 'jogo', 'Entrada: comandos, atalhos, acelerômetro (CInputManager, CShortcut)'),
    ('jogo-jogador', 'jogo', 'Jogador: controlador, vida, perks, save do jogador (CPlayer*, CHealth, CPerk*)'),
    ('jogo-armas', 'jogo', 'Armas, projéteis e explosões (CWeapon*, *Behaviour, CProjectile*, CRocket*)'),
    ('jogo-zumbis-ia', 'jogo', 'Zumbis e IA (CAIController*, CBody*, CGib*, CDeath*, CZombieConfiguration)'),
    ('jogo-ondas-spawn', 'jogo', 'Ondas/rounds e spawn (CWave*, CSpawn*)'),
    ('jogo-pickups', 'jogo', 'Power-ups/drops (CPickup*)'),
    ('jogo-mundo', 'jogo', 'Objetos do mapa: barricadas, portas, caixa, Pack-a-Punch, armadilhas, teleportes, Moon/Ascension'),
    ('jogo-fase-save', 'jogo', 'Fase, carga e save (CLevel*, CSave*, CWorld, CSuspendManager)'),
    ('jogo-pontos-stats', 'jogo', 'Pontos, estatísticas, conquistas, métricas (CScore*, CStats*, CAchievement*)'),
    ('jogo-audio', 'jogo', 'Som e música do jogo (CSound*, CMusicManager, CAmbience*, CFootsteps)'),
    ('jogo-camera-fx', 'jogo', 'Câmera e efeitos visuais (CCamera*, CFullscreenFX*, CRadial*, CVFX*, cores, neblina)'),
    ('jogo-rede-coop', 'jogo', 'Rede/co-op do jogo (CGameNetwork, CRemote*, NetworkProperty, *StateInfo)'),
    ('jogo-loja-plataforma', 'jogo', 'Loja, COD Points, GameCenter/Play Games, eventos do aparelho'),
    ('jogo-tutorial', 'jogo', 'Tutorial (CTutorial*, CAITutor)'),
    ('jogo-deadops', 'jogo', 'Dead Ops Arcade (CDO*)'),
    ('jogo-outros', 'jogo', 'Código do jogo sem classe identificada'),
    ('is-entidades', 'is', 'Is: entidades, componentes, reflexão, objetos e máquinas de estado'),
    ('is-render-cena', 'is', 'Is: renderização, setores/portais, LOD, luz ambiente, câmera'),
    ('is-colisao-fisica', 'is', 'Is: colisão e corpos rígidos (ponte para o Bullet)'),
    ('is-navegacao', 'is', 'Is: nav mesh, multidão, caminhos (sobre o Recast/Detour)'),
    ('is-animacao', 'is', 'Is: animação (ASM), anexos, ossos'),
    ('is-flash', 'is', 'Is: menus em Flash (CIsFlashMovie, renderizador gameswf)'),
    ('is-debug', 'is', 'Is: menus e desenho de debug'),
    ('is-recursos-rede', 'is', 'Is: recursos, arquivos, HTTP'),
    ('iw-base', 'marmalade', 'Marmalade base: CIwManaged, memória (buckets), propriedades, strings, serialização'),
    ('iw-gx', 'marmalade', 'IwGx/IwGL: renderização, shaders, texturas, materiais, EGL/GLES'),
    ('iw-graphics', 'marmalade', 'IwGraphics: modelos (CIwModel*), blocos de modelo'),
    ('iw-ui', 'marmalade', 'IwUI: elementos, layouts, eventos, estilos'),
    ('iw-resmanager', 'marmalade', 'IwResManager: grupos de recursos (.group.bin), handlers'),
    ('iw-mundo-cena', 'marmalade', 'IwWorld/IwGame: entidades CIwEnt, câmeras, frames, LOD, scene graph, volumes, caminhos'),
    ('iw-gfx-particulas', 'marmalade', 'IwGFX: partículas, feixes, emissores'),
    ('iw-som', 'marmalade', 'IwSound'),
    ('iw-rede', 'marmalade', 'IwNetwork: conexões, lobby, sockets, descoberta'),
    ('iw-console-menu', 'marmalade', 'IwConsole/IwMenu (debug)'),
    ('iw-fonte-texto', 'marmalade', 'IwGxFont e localização (IwLocalise)'),
    ('lib-bullet', 'terceiros', 'Bullet Physics'),
    ('lib-recast', 'terceiros', 'Recast/Detour'),
    ('lib-gameswf', 'terceiros', 'gameswf (player de Flash)'),
    ('lib-demonware', 'terceiros', 'DemonWare (bd*, IwDW*: login, lobby, stats online)'),
    ('lib-stl-cxx', 'terceiros', 'STLport e runtime C++ (libsupc++, demangler)'),
    ('lib-c', 'terceiros', 'libc/libm/libgcc, crt0 e cola do s3e'),
    ('lib-imagem-zlib', 'terceiros', 'zlib, libpng, libjpeg'),
    ('desconhecido', '?', 'Sem módulo identificado'),
]
TITLE = {m[0]: m[2] for m in MODULES}
LAYER = {m[0]: m[1] for m in MODULES}

TEMPLATE = re.compile(r'^(?:CIsReflectClass|CIsComponentTable|CIsSingleton|CIsObjectPoolDecl|CIsObjectPool|'
                      r'CIsSystemStatic|CIsComponentRef|CIsObjectStateMachine|CIwPropertyData|CIwProperty|'
                      r'CIwPropertyList|CIwComponentBase|CIwComponentBaseWithOwner|CIsReflectPropertySpec|'
                      r'CIwUIElementSignal)<(.+)>$')

GAME_RULES = [
    ('jogo-deadops', r'^(CDO|DO[A-Z]|CEventDO)'),
    ('jogo-tutorial', r'^(CTutorial|CAITutor|CEventFinishTutorial)'),
    ('jogo-estados', r'^(CGame$|CGameState|CIngameState|CFrontendState|CFlashMenuState|CLoadingScreen|IState|'
                     r'IStateHandler|CEventInGameState|CEventLoadGame|CEventStartGame|CEventPlayerReady|'
                     r'CIngame$|CUIController|CMenuItemAchievement|CLocalise|CResolutionSpecificString|CObjectState)'),
    ('jogo-input', r'^(CInputManager|CAccelerometerInput|CAnalogTouchPad|CShortcut|CEventUrgentDeviceKey)'),
    ('jogo-hud-ui', r'^(CHUD|CUI|CGui|CTouch|CIngameAchievementDisplay|CIwUISlot|CIwUISignal|IDelegate|IUnlockDelegate)'),
    ('jogo-pickups', r'^(CPickup|PickupInfo|PickupManager)'),
    ('jogo-ondas-spawn', r'^(CWave|WaveManager|StartWaveInfo|CSpawn|SpawnInfo|ZombieSpawnInfo|SpawnEffectInfo|CZombieConfiguration)'),
    ('jogo-zumbis-ia', r'^(CAI|CBody|CGib|GibState|CDeath|CDamageActionSpawn|ZombieInACan|GeorgeInACan|GeorgeSpawnInfo|'
                       r'RomeroDespawnInfo|ZombieControllerMigrationInfo|splineAnims|CASMHandler|CRigCollision|CCollisionFilter|CHumanGunBehaviour)'),
    ('jogo-armas', r'^(CWeapon|CPlayerWeapon|CRemotePlayerWeapon|C\w*Behaviour$|CProjectile|SpawnProjectileInfo|CRocket|'
                   r'CExplosion|CImpactEffect|CChainLightning|CSmokeTrail|CRenderableTrail|CMonkey)'),
    ('jogo-jogador', r'^(CPlayer|CHealth|CPerk|CRemotePerkManager|PlayerStats|CHostController|CJumpConnection|CIceSlide|CLifetime)'),
    ('jogo-fase-save', r'^(CLevel|CSave|CWorld|CSuspendManager|SaveBlackOp|CEventSuspend|CEventLowbattery|CInitialRigidBodyState|'
                       r'CEntityFlags|CEnt$|CDynamicEntityComponent|DynamicEntityInfo|CNotable|CArea|CLocator|CDirection|CGroup|'
                       r'CParsableEnumManager|CParsable|CPOI|CPathWalker|CConsoleShaderParamManager)'),
    ('jogo-pontos-stats', r'^(CScore|ScorePair|Scores|CStats|CAchievement|CMetrics|CRankingConfig|LeaderBoard|Map_LeaderBoard)'),
    ('jogo-audio', r'^(CSound|CMusic|CAmbience|CFootsteps)'),
    ('jogo-camera-fx', r'^(CCamera|CFullscreenFX|FullscreenFXState|CRadial|CVFX|CFog|CColouriser|CAmbient|CVertexColouriser|'
                       r'CRenderFlasher|CSnowTrigger|CTVScreen|CProjector|CSpinner|CCycleController|CModelBlockSwitcher|CAnimatedScenery)'),
    ('jogo-rede-coop', r'^(CGameNetwork|CRemote|NetworkProperty|SmartNetworkProperty|CNetwork|CBoolArray|\w*StateInfo$|'
                       r'\w*SyncInfo$|TeleportPlayerInfo|FlingerSync|MatchMaking|CEventNewChannelRoom|CEventVoiceChat)'),
    ('jogo-loja-plataforma', r'^(CGameStore|CCODPoints|CTransactionManager|ITransactionObserver|IGameStore|IIGameStore|'
                             r'CContentUnlocker|GameCenter|CDeviceResolution|CMetrics|IGame)'),
    ('jogo-mundo', r'^(CBarricade|CDoor|CMysteryBox|CPackAPunch|CPowerSwitch|CTrap|CTeleport|CZip|CLunar|CTurret|CEasterEgg|'
                   r'CLighthouse|CCentrifuge|CFlinger|FlingingEntInfo|CCurvePath|CLock)'),
]

IS_RULES = [
    ('is-flash', r'^(CIsFlash|IsFlash)'),
    ('is-debug', r'^(CIsDebug|CIsProfileMenu)'),
    ('is-navegacao', r'^(CIsNavMesh|CIsCrowd|CIsPath|IIsNavMesh|CIsEntityPath)'),
    ('is-colisao-fisica', r'^(CIsCollision|CIsRigidBody|CIsBulletWorld|CIsChildShape|IIsChildShape)'),
    ('is-animacao', r'^(CIsAnim|CIsASM|IIsASM|CIsAttachment|CIsBoneAttachment|CIsFrameBone|CIsModelBlockSwitcher)'),
    ('is-render-cena', r'^(CIsRender|CIsSector|CIsPortal|CIsOccluder|CIsFrustrum|CIsLOD|CIsAmbient|CIsCamera|CIsModelBlock|'
                       r'CIsMaterialSet|CIsTextureReference|CIsSceneGroup|CIsSceneManager|CIsDirection|CIsRenderDirection)'),
    ('is-recursos-rede', r'^(CIsHTTP|CIsArchive|CIsResConfigLoader|CIsAssetBuild|CIsResource|CIsITXClassParser|CIsNetwork)'),
    ('is-entidades', r'^(CIs|IIs)'),
]

IW_RULES = [
    ('iw-ui', r'^(CIwUI|IIwUI|IwUI)'),
    ('iw-gx', r'^(CIwGx|CIwMaterial|CIwTexture|CIwClut|CIwResTemplateMTL|CIwResTemplateImage|CIwResHandlerImage)'),
    ('iw-graphics', r'^(CIwModel|CIwResTemplateEntSpec)'),
    ('iw-resmanager', r'^(CIwRes|CIwResource|IIwStateLoader)'),
    ('iw-gfx-particulas', r'^(CIwGFX)'),
    ('iw-som', r'^(CIwSound)'),
    ('iw-rede', r'^(CIwNetwork|IIwNetwork|IwNetwork|CIwGCMatchmaking|IStunSession|s3eStun)'),
    ('iw-console-menu', r'^(CIwConsole|IIwConsole|CIwMenu|CIwEventConsoleCommand)'),
    ('iw-fonte-texto', r'^(CIwGxFont)'),
    ('iw-mundo-cena', r'^(CIwEnt|CIwCamera|CIw\w*CameraController|CIwFrame|CIwHierarchy|CIwLOD|CIwSG|CIwTree|IIwTreeObject|'
                      r'CIwVolume|CIwWorld|CIwPath|CIwRender|CIwAnimator|CIwAmbient|CIwFog|CIwHorizon|CIwLocator|CIwCollider|'
                      r'CIwCollisionBounds|CIwAutoBounds|CIwRayTunnel|CIwGame|CIwGameState|CIwComponent|CIwSpec|CIwEntityLookup|'
                      r'CIwTouch|IIwGameClock|CIwKeyboard|IIwKeyboard|IIwBackground|CIwEvent$)'),
    ('lib-demonware', r'^(IwDW|IwDemonware|IwCommand)'),
    ('iw-base', r'^(CIw|IIw|Iw)'),
]

LIB_RULES = [
    ('lib-bullet', r'^(bt|Broadphase|SphereTriangleDetector|SupportVertexCallback|DebugDrawcallback|GIM_)'),
    ('lib-recast', r'^(rc|dt)[A-Z]'),
    ('lib-gameswf', r'^(gameswf|render_handler|bitmap_info|video_handler|weak_pointee|tu_|membuf)'),
    ('lib-demonware', r'^bd[A-Z]'),
    ('lib-stl-cxx', r'^(std::|_STL::|__gnu_cxx|__cxxabiv1|_SgI::)'),
    ('lib-bullet', r'^(bParse::|BulletUtils::)'),
    ('lib-gameswf', r'^(image::|jpeg::)'),
    ('is-animacao', r'^ASMHelper::'),
    ('lib-c', r'^s3eFileReader'),
]

# faixas de endereço (fallback para funções sem classe); da varredura de strings/classes por bloco
RANGES = [
    (0x035090, 0x036380, 'lib-c'),
    (0x036380, 0x03C000, 'iw-base'),
    (0x03C000, 0x040000, 'iw-rede'),
    (0x040000, 0x050000, 'lib-demonware'),
    (0x050000, 0x080000, 'lib-bullet'),
    (0x080000, 0x08C000, 'is-colisao-fisica'),
    (0x08C000, 0x0D4000, 'lib-gameswf'),
    (0x0D4000, 0x0DC000, 'lib-recast'),
    (0x0DC000, 0x0EC000, 'is-entidades'),
    (0x0EC000, 0x0F8000, 'is-entidades'),
    (0x0F8000, 0x0FC000, 'jogo-estados'),
    (0x0FC000, 0x100000, 'iw-gfx-particulas'),
    (0x100000, 0x10C000, 'iw-rede'),
    (0x10C000, 0x110000, 'iw-som'),
    (0x110000, 0x138000, 'iw-mundo-cena'),
    (0x138000, 0x258000, 'jogo-outros'),
    (0x258000, 0x268000, 'is-render-cena'),
    (0x268000, 0x270000, 'iw-resmanager'),
    (0x270000, 0x288000, 'iw-graphics'),
    (0x288000, 0x290000, 'iw-base'),
    (0x290000, 0x29C000, 'lib-imagem-zlib'),
    (0x29C000, 0x2A4000, 'lib-imagem-zlib'),
    (0x2A4000, 0x314000, 'iw-gx'),
    (0x314000, 0x320000, 'iw-base'),
    (0x320000, 0x35C000, 'iw-ui'),
    (0x35C000, 0x364000, 'iw-fonte-texto'),
    (0x364000, 0x380000, 'lib-stl-cxx'),
    (0x380000, 0x384000, 'lib-c'),
    (0x384000, 0x394000, 'lib-stl-cxx'),
    (0x394000, 0x3B3800, 'lib-c'),
]


SLOT = re.compile(r'^(?:CIwUISlot\w*|CIwUISignal\w*|CIwUIElementSignal|CIsObjectStateMachine|CIsComponentTable|'
                  r'CIsReflectClass|CIsSingleton|CIsSystemStatic)<(\w+)[,>]')


def unwrap(name):
    """CIsReflectClass<CWave> -> CWave (repetido para modelos aninhados); CIwUISlot1<CX, ...> -> CX"""
    m = SLOT.match(name)
    if m and not PRIMITIVE.match(m.group(1)):
        return m.group(1)
    for _ in range(3):
        m = TEMPLATE.match(name)
        if not m:
            break
        name = m.group(1).split(',')[0].strip()
    return name


PRIMITIVE = re.compile(r'^(bool|char|float|int|short|signed char|unsigned \w+|_STL::|std::|CIwF?S?Vec\d|CIwFMat|CIwFQuat|CIwColour)')
EXTRA = {'ActivationParams': 'jogo-mundo', 'LCListNode': 'iw-base', 'MyMemBucketDebugObject': 'iw-base',
         'TeleportStateTransitionInfo': 'jogo-rede-coop', 'IAnimChangedListener': 'is-animacao',
         'IRenderable': 'is-render-cena'}


def module_of_class(name):
    n = unwrap(name)
    if PRIMITIVE.match(n):
        n = name
        if n.startswith(('CIsReflect', 'CIsComponent', 'CIsSingleton', 'CIsObject', 'CIsSystem')):
            return 'is-entidades'
        if n.startswith('CIwProperty'):
            return 'iw-base'
    if n in EXTRA:
        return EXTRA[n]
    if n.startswith('CIwUISlot') or n.startswith('CIwUISignal'):
        m = re.search(r'I\d+(C\w+?)(?:MS0_|P12)', n) or re.search(r'<(C\w+)', n)
        inner = m.group(1) if m else ''
        if inner:
            return module_of_class(re.sub(r'^\d+', '', inner))
    for rules in (LIB_RULES, GAME_RULES, IS_RULES, IW_RULES):
        for mod, rx in rules:
            if re.match(rx, n):
                return mod
    if re.match(r'^(C[A-Z]|I[A-Z])', n):
        return 'jogo-outros'
    return None


def module_of_addr(a):
    for lo, hi, m in RANGES:
        if lo <= a < hi:
            return m
    return 'desconhecido'
