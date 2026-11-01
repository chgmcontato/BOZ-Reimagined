# BOZ Reimagined

**English** · [Português](README.pt-BR.md)

**Call of Duty: Black Ops Zombies** (Android 1.0.11) adapted for **Android TV**: fully playable with a gamepad,
no touch HUD, the Black Ops 1 console button layout, and menus redesigned in the game's comic-book style.

<p align="center"><img src="screenshots/01-titulo.png" width="80%" alt="Reimagined title screen"></p>

> Non-profit fan project, not affiliated with Activision.
> *Call of Duty* and *Call of Duty: Black Ops Zombies* are trademarks and the property of **Activision Publishing, Inc.**
>
> The game was **removed from Google Play** and is no longer sold. **This repository does not contain the game**
> (no original APK, OBB, binaries or music) and never will. The patch in Releases only works when applied to your own
> copy of the APK: without the original game this project cannot be used. Please don't ask for the files in issues.

## Screenshots (real TV, 1280×720)

| Pause with match data (Kino der Toten) | Pause with the map scenery (Call of the Dead) |
|---|---|
| ![Kino pause](screenshots/02-pause-kino.png) | ![CoTD pause](screenshots/03-pause-cotd.png) |
| **Dead Ops Arcade with match timer** | **Player Profile with name and TIME PLAYED** |
| ![Dead Ops](screenshots/04-deadops-hud.png) | ![Player Profile](screenshots/05-player-profile.png) |

## What changes

**Controls (Xbox layout)**
- Sticks to move and aim, triggers to shoot and aim down sights (hold-to-aim became toggle), X to reload.
- Grenades and tacticals as in BO1: D-pad right for the claymore, LB for the other tactical (Monkey Bomb, Gersh, Matryoshka).
- Every menu can be navigated with the D-pad, with corner-bracket focus and transitions that hide the original SWF.

**Menus and interface**
- Redesigned "Reimagined" title, main menu, character and map select, Single Player, New Game, Quit, Save & Quit,
  Pause and Game Over (Java layer drawn over the game).
- Name editor driven by the gamepad; the name appears in the Player Profile, the pause menu and the co-op lobby.
- Pause shows player, map, round and stats; Start also pauses Dead Ops.
- Match timer in the HUD font (toggle in the pause menu) and TIME PLAYED in the Player Profile.
- COD Points, More Apps and purchases removed.

**Gameplay (binary patches)**
- BO1-paced waves and drops: non-repeating drop cycle, Carpenter only with broken windows,
  Death Machine only with power on or after someone has had Quick Revive, DG-2 only after killing George.
- Dead Ops Arcade unlocked from the start; death goes straight to Game Over.
- A stable 30 FPS cap and an FPS counter.
- Fixes: a stuck zombie dies after 15 s without moving, and the game no longer crashes when a sound that was already freed finishes playing.

## Status

| Feature | Status |
|---|---|
| Controls, menus, transitions, player name | ✅ tested on TV |
| Pause (data, map, Dead Ops), timer, TIME PLAYED, Save & Quit → Continue | ✅ tested on TV |
| Death Machine, DG-2, tacticals on LB and HUD | ✅ tested on TV |
| **Local Wi-Fi** co-op (lobby, player name, map select) | ✅ PC ↔ TV match played (TV ↔ TV not tested) |
| Stuck zombie, sound fix, sticks after leaving pause | 🟡 implemented, needs confirmation on TV |
| BO1 waves/drops, 30 FPS, tutorial, "[A] to …" prompts | 🟡 implemented, full test pending |
| Play Online | ❌ official servers are offline |
| Moon map | ⏸️ paused (crash while loading No Man's Land) |

Tested on a TCL TV with Android 11 (armeabi-v7a, 1280×720, 60 Hz) and an 8BitDo Ultimate in Xbox mode.

## Required original files

Check your files first: the patch will not apply to any other version. The OBB is not modified: it is the same file the game downloaded.

| File | Size | SHA-256 |
|---|---|---|
| Android APK 1.0.11 (`com.activision.boz`) → `tv/base.apk` | 48,440,698 bytes | `348c3ca53291b28380eca44f25198cc64e2e8ce9d56ed4ac170310aff3a86747` |
| `assets/boz.s3e` (inside the APK) | — | `f458c15a7111779ad320af377d0bb751294119788ba06d430bee0cc977539fee` |
| OBB `blackops_etc.dz` (original, goes to the TV **unchanged**) | 526,783,202 bytes | `4537aabe0be836048bafd7493ac5f18b33c0bc19d6dd0b873533bb953bbd3a91` |

```bash
sha256sum base.apk blackops_etc.dz
unzip -p base.apk assets/boz.s3e | sha256sum
```

The iOS version (`.ipa`) won't work: the patches target the Android binary.

## Installing on a TV

You need the original APK and OBB (table above), a PC with `adb` and `xdelta3`, and the TV on the same network.

1. On the TV, enable **Developer options → USB/ADB debugging** and note its IP (Settings → Network).
2. Download `boz-reimagined-*.xdelta` from [Releases](../../releases). It only works when applied to the original
   APK, turning it into the Reimagined APK.
3. Run:
   ```bash
   bash instalar.sh <TV-IP> base.apk blackops_etc.dz boz-reimagined-tv-v0.4.xdelta
   ```
   The script checks the hashes, applies the patch, installs the APK and copies the OBB to
   `/storage/emulated/0/Android/obb/com.activision.boz/`. The first time you connect, the TV may ask you to authorize the PC.
4. Pair the gamepad (Xbox layout) over Bluetooth and open the game.

- **Windows:** apply the patch with [Delta Patcher](https://github.com/marco-calautti/DeltaPatcher) or
  `xdelta3 -d -s base.apk patch.xdelta BOZ-TV.apk`, then install with
  `adb install --no-incremental -r -g BOZ-TV.apk` and `adb push`.
- **Original game already installed on the TV?** The signature differs: uninstall it first. This deletes your saves.
- Always use `--no-incremental`; an incremental install once wiped the app and its data on a TV.

## Co-op over the internet with ZeroTier (experimental)

**Local Wi-Fi** co-op only looks for partners on the same network. [ZeroTier](https://www.zerotier.com) creates a
virtual local network between different homes, so two TVs in different cities "see" each other as if they were
on the same Wi-Fi.

> ⚠️ **Not tested yet.** Co-op hasn't been tested even on the same network, and the game may look for the host
> by broadcast, which doesn't always go through the VPN on Android. Whether it works or not, please report it in an issue.

1. **Create the network (one person only):** go to [my.zerotier.com](https://my.zerotier.com), create a free
   account, click **Create A Network** and copy the **Network ID** (16 characters). Keep the network **Private**
   and, under *Advanced*, leave **Allow Ethernet Broadcast** enabled.
2. **Install ZeroTier on the TV (every player):** the *ZeroTier One* app usually doesn't show up in the TV's Play
   Store. Download the Android APK from the [official download page](https://www.zerotier.com/download/) and
   install it from the PC:
   ```bash
   adb connect <TV-IP>:5555
   adb -s <TV-IP>:5555 install --no-incremental zerotier.apk
   ```
3. **Join the network:** open ZeroTier One on the TV (some screens need a pointer; a USB mouse or a remote-control
   app helps), tap **+**, enter the Network ID and enable the network. Android asks for permission to create a
   **VPN**: accept it. Some TVs never show this dialog; ZeroTier won't work on them.
4. **Authorize:** whoever created the network opens its page on my.zerotier.com and ticks **Auth** for each TV in
   the member list. Each TV gets a ZeroTier IP (for example `10.147.x.x`).
5. **Check the connection:** from a PC that has also joined the network, run `ping <TV-ZeroTier-IP>`, or, on the
   TV itself, `adb shell ping -c 3 <other-TV-ZeroTier-IP>`.
6. **Play:** with ZeroTier active on both TVs, one player opens **Co-op → Local Wi-Fi** and hosts the match; the
   other joins from the same menu.

Tip: with ZeroTier active, you can install updates remotely on someone else's TV by using its ZeroTier IP instead
of its local IP (`bash instalar.sh <ZeroTier-IP> ...`), as long as that TV has authorized your PC for ADB.

## Building (developers)

Requirements: Linux, JDK 17, Android SDK (build-tools 36, platform 36), `tools/apktool.jar`, `tools/baksmali.jar`,
Python 3 and ImageMagick. The build also uses images and fonts extracted from the game (portraits, backgrounds, HUD
digits and menu reference shots) that are not in the repository: see `tv/character-menu/sources.json` and the
`menus-edit/` scripts. With everything in place, `bash tv/build.sh` produces `BOZ-TV.apk`.

## How it works

- The game runs on the Marmalade engine as a closed binary (`boz.s3e`, ARM/Thumb). Gameplay changes are
  **binary patches** verified byte by byte (`tv/patch_s3e.py`, `patch_pickups.py` and others), with new code
  placed in a free area of the image.
- Controls, menus, pause and timer are a **Java layer** (`tv/java/com/carlos/boz/`) that recognizes each screen
  by its pixels and draws on top, without touching the menu SWF.
- Reverse-engineering tools (Ghidra, Frida, `.group.bin` format) live in `tv/re/`, `tv/frida/` and `tv/maps/`.
- [`CLAUDE.md`](CLAUDE.md) is the full technical log (in Portuguese): offsets, pitfalls and decisions.

## Roadmap

The focus is improving the experience of playing on a TV:

1. Test Local Wi-Fi co-op with two devices (host, client and P2 name), on the same network and over ZeroTier.
2. Confirm the recent fixes on TV (stuck zombie, sound, sticks after pause).
3. Redesign the remaining screens: About, More Info, Extras and Help subscreens.
4. Make the Java HUD's memory reads more robust.
5. Translate the interface (currently English).

Bugs and suggestions: open an *issue* with the map, the round and what happened (a screenshot helps a lot).
Issues in English or Portuguese are welcome.
