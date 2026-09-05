# 🐐 Goat Client

A black-minimalist, fully-offline **Minecraft ghost client**, rebuilt from
recovered Vape V4.22 modules into a single self-contained injector.

No login. No service. No account. No network calls. Drop in, inject, play.

---

## What it is

Goat Client is an **injection-based** ghost client. A native DLL
(`GoatClientNative.dll`) injects an embedded Java payload into a running
Minecraft JVM, which boots the full client module set inside the game.

- **Full Vape V4.22 module set** — CrystalAura, SilentAura, KillAura, AimAssist,
  Reach, Velocity, AutoClicker, Scaffold, Fly, Speed, ESP, NameTags, XRay,
  AutoTotem, AutoArmor, ChestSteal, MLG, PearlCatch, and hundreds more.
- **100% offline** — account/friends/profiles/network layers stripped. The
  client boots and runs with zero remote connectivity.
- **Single EXE** — `GoatClient.exe` auto-detects a running Minecraft and
  injects. No process picker, no separate DLL file, no manual steps.
- **Local config** — settings auto-save to `~/.goat-client/config.json`, and
  can be exported/imported as portable JSON.
- **Black-minimalist theme** — near-black `#080808` background, slate
  `#6E7B8B` accent, clean Inter-style typography.

---

## Supported versions

| Minecraft | Vanilla | Forge | Fabric |
| :--- | :---: | :---: | :---: |
| 1.7.10  | ✅ | ✅ | — |
| 1.8.9   | ✅ | ✅ | — |
| 1.12.2  | ✅ | ✅ | — |
| 1.16.5  | ⚠️ limited | ⚠️ | — |
| 1.21.11 | ✅ | ✅ | ✅ |
| 26.2    | ✅ | ✅ | ✅ |

Also supports **Lunar Client 1.8.9** and **Badlion Client 1.8.9** injection.

> **64-bit JVM required.** The injector refuses 32-bit targets.

---

## Usage (prebuilt)

1. Run `Defender-Exclusion.ps1` **as administrator** (one time) so Windows
   Defender doesn't block the DLL injection.
2. Launch Minecraft (64-bit) and wait for the main menu, or join a world.
3. Double-click `GoatClient.exe`. It finds Minecraft and injects automatically.
4. In game, press **RIGHT SHIFT** to open the GUI.

On success, `goatclient-native.log` (next to the EXE) ends with:

```
NativeBridge.start completed; injection is active
```

---

## Configuration

Config persists automatically to `~/.goat-client/config.json` whenever you
change settings. Manage it in-game via chat commands:

| Command | Effect |
| :--- | :--- |
| `.goat save` | Save config to disk |
| `.goat load` | Reload config from disk |
| `.goat export <path>` | Export config to a JSON file |
| `.goat import <path>` | Import config from a JSON file |
| `.goat path` | Show the config location |
| `.goat profile list` | List profiles (`*` = active) |
| `.goat profile create <name>` | Create and switch to a new profile |
| `.goat profile switch <name>` | Switch to a profile |
| `.goat profile rename <name>` | Rename the active profile |
| `.goat profile delete <name>` | Delete a profile |
| `.goat profile duplicate <name>` | Duplicate a profile |

---

## Build from source

Requirements: **JDK 17** + **llvm-mingw** (clang + windres), Windows x64.

```bash
export JAVA_HOME=/path/to/jdk-17
export MINGW_BIN=/path/to/llvm-mingw/bin
./build.sh
```

Output: `GoatClient/build/GoatClient.exe` + `GoatClientNative.dll`.

The build pipeline:
1. Gradle compiles the recovered Java payload into a self-contained jar
   (`verifyInjectionPayload` enforces Java 8 bytecode + runtime coverage).
2. The jar is embedded as a resource into `GoatClientNative.dll` (C, JNI/JVMTI).
3. The auto-injector EXE (C++, GDI+) is compiled with the DLL embedded inside.

---

## Architecture

```
GoatClient.exe  (C++, GDI+ launcher)
  └─ GoatClientNative.dll  (C, JNI/JVMTI bridge)
       └─ injected Java payload  (gg.vape.*, ~2900 classes)
            ├─ module/*       (456 module files: combat, blatant, render, ...)
            ├─ value/*        (settings system)
            ├─ event/*        (event bus + 130+ events)
            ├─ config/*       (profiles + offline persistence)
            └─ ui/*           (ClickGUI frames, HUD, notifications)
```

The DLL attaches to the target JVM via JVMTI, appends the payload jar to the
game's classloader (URL / Fabric Knot / Forge ModLauncher aware), and calls
`NativeBridge.start()`.

---

## Warnings

- This is a **cheat client**. Using it on multiplayer servers can get you banned.
- It is a reverse-engineered recovery, **not** the official Vape product.
- Windows Defender flags DLL injection by behavior (`CreateRemoteThread`); the
  one-time exclusion in `Defender-Exclusion.ps1` is required.

---

## License

Recovered source is CC0 1.0 (see `LICENSE`). Third-party fonts, textures, and
libraries retain their own rights.
