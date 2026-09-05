# Contributing to Goat Client

Thanks for your interest. Goat Client is a reverse-engineered, fully-offline
Minecraft ghost client, rebuilt from recovered Vape V4.22 modules.

## Ground rules

- **Do not change module behavior.** The combat/render/utility modules are
  recovered code; altering their logic is out of scope and risky.
- **Stay offline.** No network calls, no accounts, no telemetry. The client
  must run with zero remote connectivity.
- **Java 8 bytecode target.** The injected payload must be Java 8 compatible
  (`targetRelease=8`). No `var`, no Java 9+ APIs.
- **Package names are frozen.** The `gg.vape` package cannot be renamed —
  reflection, mappings, and the native bridge depend on it.

## Build

```bash
export JAVA_HOME=/path/to/jdk-17
cd GoatClient
./gradlew build verifyInjectionPayload
```

The native DLL + EXE are built from the repo root:

```bash
export MINGW_BIN=/path/to/llvm-mingw/bin
./build.sh
```

## Verification

Every change must pass:

```bash
JAVA_HOME="C:\Users\lukep\.jdks\jdk-17.0.20.1+1" ./gradlew build verifyInjectionPayload --no-daemon
```

`verifyInjectionPayload` enforces: Java 8 bytecode (zero Java 9+ classes) and
runtime package coverage.

## Style

- Match surrounding decompiled-code conventions (even if unusual).
- Prefer surgical edits over rewrites.
- Add null guards defensively; the client must never crash in-game.
- Branding strings are "Goat Client"; do not reintroduce "Vape".

## License

Recovered source is CC0 1.0 (see `LICENSE`). Third-party fonts, textures, and
libraries retain their own rights.
