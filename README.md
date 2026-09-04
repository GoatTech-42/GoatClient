# Goat Client

A black-minimalist Minecraft **ghost client**, built from recovered Vape V4.22
modules, fully **offline**, with a single-click auto-injector.

- **Goat Client** branding + goat logo + clean black theme (Inter-font look).
- **Full Vape V4.22 module set** — CrystalAura, SilentAura, KillAura, Fly,
  Speed, Scaffold, Phase, XRay, ESP, NameTags, AutoTotem, AutoArmor,
  ChestSteal, MLG, PearlCatch, and hundreds more.
- **100% offline** — no login, no service, no account, no network calls.
- **Local config** — settings auto-save to `~/.goat-client/config.json`, and
  can be exported/imported as portable JSON.
- **Single EXE** — finds a running Minecraft and injects automatically.

## Requirements

- Windows 10/11 x64
- Minecraft with a **64-bit JVM**: 1.7.10 / 1.8.9 / 1.12.2 / 1.21.11 / 26.2
  (Vanilla, Forge, or Fabric), plus Lunar/Badlion 1.8.9.
- To *rebuild*: JDK 17 + llvm-mingw (clang + windres).

## Usage (prebuilt)

1. Run `Defender-Exclusion.ps1` **as administrator** (one time) so Windows
   Defender doesn't block the injection.
2. Launch Minecraft (64-bit) and wait for the main menu or join a world/server.
3. Double-click `GoatClient.exe`. It finds Minecraft and injects automatically.
4. In game, press **RIGHT SHIFT** to open the GUI.

On success, `goatclient-native.log` (next to the EXE) ends with
`NativeBridge.start completed; injection is active`.

## Config (save / export / import)

Config persists automatically to `~/.goat-client/config.json` whenever you
change settings. To move or share a config:

```java
gg.vape.runtime.NativeBridge.exportConfig("C:/path/to/goat-config.json");
gg.vape.runtime.NativeBridge.importConfig("C:/path/to/goat-config.json");
```

## Build from source

From a Git Bash prompt at the repo root:

```bash
export JAVA_HOME=/path/to/jdk-17
export MINGW_BIN=/path/to/llvm-mingw/bin   # clang + windres
./build.sh
```

Output: `VapeV4.21/build/GoatClient.exe` + `GoatClientNative.dll`.

## Notes / warnings

- This is a **cheat client**. Using it on multiplayer servers can get you banned.
- It is a reverse-engineered recovery, not the official Vape product.
- Windows Defender flags DLL injection by behavior (`CreateRemoteThread`); the
  one-time exclusion in `Defender-Exclusion.ps1` is required to run it.

## License

Recovered source is CC0 1.0 (see `LICENSE`). Third-party fonts, textures, and
libraries retain their own rights.
