# codboz-pc

Runs **Call of Duty: Black Ops Zombies** on a PC from **your own APK and OBB**, or from **your own iOS IPA**.
No game data ships here.

The game's ARM executable (`boz.s3e`) runs on the Dynarmic ARM JIT (or on Unicorn with
`make EMU=unicorn`, see below); its system calls
(s3e, EGL, OpenGL ES, sound, sockets, ZeroConf) are reimplemented on SDL2 and desktop OpenGL.
The game always renders at 1280x720, the TV's resolution, scaled to the monitor
(borderless fullscreen by default).

## Start

```sh
git submodule update --init --recursive
make CC=gcc CXX=g++ PKG_CONFIG_PATH=/home/linuxbrew/.linuxbrew/lib/pkgconfig
./run.sh
```

The launcher opens: pick a mode, **Install** from an APK or an IPA (the mode is detected from the
file), **Select the OBB** (`blackops_etc.dz`, it is never copied; the iOS mode needs none), type
the player name, map the controls, then **PLAY**. `--play` skips the launcher and repeats the last
choice; `--install FILE` installs from the command line.

Data lives in `build/game/`: `settings.txt`, `bindings.txt`, `launcher.cfg`, and one folder per
mode (`original/`, `reimagined/`, `ios/`) with its assets and saves.

### CPU backends (Dynarmic, Unicorn)

```sh
make CC=gcc CXX=g++ BOOST_INCLUDE_DIR=/path/to/boost/include      # Dynarmic (default)
make EMU=unicorn CC=gcc                                            # Unicorn, into build-unicorn/
./build-unicorn/codboz-pc --data build/game
```

`run.sh` starts `build/codboz-pc`; `CODBOZ_EMU=unicorn ./run.sh` starts the Unicorn build.
Dynarmic: same game, same data, about 6x less CPU time per frame (Call of the Dead without vsync: 38 -> 211
fps on a Radeon 610M laptop). `CODBOZ_DYN_FASTMEM=1` is faster still but lets the game reach any
low host memory; `CODBOZ_DYN_CODE_WATCH=1` checks for self-modifying code (none found).
Details: `tools/dynarmic_probe/README.md`.

### iOS IPA (version 1.3.5)

Install `Call of Duty Black Ops Zombies` 1.3.5's IPA as a third mode, **iOS**. On iOS, Marmalade
links the game into the app executable: the installer takes the game image from the armv7 slice,
decodes its scrambled data part (the key is recovered from the image's own relocations) and writes
a normal `boz.s3e`, plus `blackops_gles1.dz` and the music. The iOS art is PVRTC, decoded to RGBA
when the game uploads it. Co-op only with the same version (it is not the Android 1.0.11 game).

## The two modes

| | Original | Reimagined |
|---|---|---|
| APK | Android 1.0.11 (`com.activision.boz`) | boz-reimagined `BOZ-TV.apk` |
| `boz.s3e` | untouched | the TV's own, byte for byte |
| Co-op with | phones running the original | TVs running Reimagined |
| Waves / drops | mobile rules | BO1 horde pace, BO1 drop cycle, Death Machine/DG-2 rules |
| HUD | mobile HUD, no touch buttons (Xperia profile) | TV HUD: grenade counters, match timer, native FPS counter |
| Aim | hold | hold (sent as the TV's toggle + leave-aim) |
| Name | replaces "Player-N" in co-op | the TV's `boz_name.txt`: profile and co-op lobby |
| Profile | original | TIME PLAYED in Player Profile, player/map/round on pause |

Co-op needs the **same mode on both sides**: Reimagined changes game rules that the host and
client both simulate, so a Reimagined PC must not join an original phone, and vice versa.
The PC adds nothing to the network protocol in either mode.

What the PC does in both modes, outside the game binary: the in-memory key bindings fix
(reload on key 9, same as the TV's OBB; your OBB already has it), hiding the virtual stick art,
screen recognition (the TV's `boz_menus.json`) to decide when the mouse is a cursor.

## Controls

Remap everything in the launcher (**Controles**) or in `bindings.txt`. Defaults:

| Action | Keyboard/mouse | Controller (BO1 layout) |
|---|---|---|
| Move / look | WASD / mouse | sticks |
| Shoot / aim (hold) | left / right button | RT / LT |
| Use, buy, sprint | E, Shift, Space | A |
| Reload | R | X |
| Switch weapon | 1, wheel | Y |
| Crouch | C, Ctrl | B |
| Melee | V, middle button | RS |
| Grenade / tactical / claymore | G / Q / 4 | RB / LB / D-pad right |
| Pause | Esc | Start |

The mouse is a cursor on every recognised menu (including pause and Game Over) and a camera in
gameplay; the switch is automatic. On the pause screen, T toggles the match timer. F11: fullscreen;
F3: host FPS counter.

## Not done yet

- The TV's redesigned menus (Reimagined title, main menu, character select, styled pages,
  name editor, controller focus brackets) are not redrawn yet: menus show the original Flash screens.
- Save & Quit -> Continue does not restore the match timer.
- Co-op discovery (ZeroConf) is implemented but untested against a phone or TV.
- Windows build.
- iOS mode: `s3eInetPton` (iOS-only import) is still a stub; it was never called in testing.
- The first click after the iOS title screen is swallowed by the game (click again).

## Layout

- `src/`, `Makefile`: the PC port (ARM emulator + SDL/OpenGL). `make` builds `build/codboz-pc`;
  `make clean` only removes compiled files (`build/game` holds the installed game data and saves).
- `tv/`: the Android TV layer (Java in `tv/java`, TV menu JSON in `tv/assets`).
  `tv/build_tv_apk.sh BASE.apk OUT.apk` rebuilds and signs the TV APK (key and password stay in `~/.config/boz`).
- `assets/icon`, `tools/make_icon.py`: game icon and TV banner.
