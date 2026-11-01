# codboz-pc

Runs **Call of Duty: Black Ops Zombies** on a PC from **your own APK and OBB**. No game data ships here.

The game's ARM executable (`boz.s3e`) runs on an ARM emulator (Unicorn); its system calls
(s3e, EGL, OpenGL ES, sound, sockets, ZeroConf) are reimplemented on SDL2 and desktop OpenGL.
The game always renders at 1280x720, the TV's resolution, scaled to the monitor
(borderless fullscreen by default).

## Start

```sh
make CC=gcc PKG_CONFIG_PATH=/home/linuxbrew/.linuxbrew/lib/pkgconfig
./build/codboz-pc
```

The launcher opens: pick a mode, **Install from an APK...** (the mode is detected from the APK),
**Select the OBB** (`blackops_etc.dz`, it is never copied), type the player name, map the
controls, then **PLAY**. `--play` skips the launcher and repeats the last choice.

Data lives in `build/game/`: `settings.txt`, `bindings.txt`, `launcher.cfg`, and one folder per
mode (`original/`, `reimagined/`) with its assets and saves.

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
- iOS IPA; Windows build.

## Layout

- `src/`, `Makefile`: the PC port (ARM emulator + SDL/OpenGL). `make` builds `build/codboz-pc`;
  `make clean` only removes compiled files (`build/game` holds the installed game data and saves).
- `tv/`: the Android TV layer (Java in `tv/java`, TV menu JSON in `tv/assets`).
  `tv/build_tv_apk.sh BASE.apk OUT.apk` rebuilds and signs the TV APK (key and password stay in `~/.config/boz`).
- `assets/icon`, `tools/make_icon.py`: game icon and TV banner.
