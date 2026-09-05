# Goat Client — Client Payload

This directory contains the **Java payload** injected into Minecraft, plus the
**native bridge** (`native/`) and the **auto-injector** (`native/autoinject/`).

> This is a reverse-engineered recovery of the Vape V4 client, rebranded and
> made fully offline as **Goat Client**. It is not the official Vape source,
> release, or a signed artifact.

## Layout

```
GoatClient/
├── build.gradle            # Gradle build (Java 8 target, shadow jar)
├── settings.gradle         # rootProject.name = 'goat-client'
├── gradle.properties       # targetRelease=8
├── src/main/java/gg/vape/  # recovered client source
│   ├── module/             # 456 module files (combat, blatant, render, ...)
│   ├── value/              # settings value system
│   ├── event/              # event bus + events
│   ├── config/             # profiles + offline config commands
│   ├── ui/                 # ClickGUI frames, HUD, notifications
│   └── runtime/            # NativeBridge + offline persistence
└── native/
    ├── *.c, *.h            # JNI/JVMTI injection bridge
    ├── CMakeLists.txt
    └── autoinject/         # GoatClient.exe launcher (C++, GDI+)
```

## Build

```bash
export JAVA_HOME=/path/to/jdk-17
./gradlew clean build verifyInjectionPayload
```

The injection jar is produced at
`build/libs/goat-client-4.22-injection.jar`, then embedded into the native DLL
by `build.sh` (repo root).

## Minecraft compatibility

| Minecraft | Vanilla | Forge | Fabric |
| --- | :---: | :---: | :---: |
| 1.7.10 | ✅ | ✅ | — |
| 1.8.9 | ✅ | ✅ | — |
| 1.12.2 | ✅ | ✅ | — |
| 1.16.5 | ⚠️ | ⚠️ | — |
| 1.21.11 | ✅ | ✅ | ✅ |
| 26.2 | ✅ | ✅ | ✅ |

64-bit JVM only.

## Offline design

- `NativeBridge.gp/sp` persist config to `~/.goat-client/config.json`.
- Account/friends/profiles/Zeus network paths are stripped or stubbed.
- `GoatConfigCommand` provides in-game chat commands (`.goat ...`).
