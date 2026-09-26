# Goat Client 4.22 — Release Notes

## Highlights

- **Blue accent** — GUI, HUD, and text-GUI accent color changed from Vape's
  green to a clean blue (`#2F7AE5`).
- **No login page** — the online "Vape Online" login button and sign-in panel
  are gone; the client is fully offline with no account screen.
- **Fixed false "injection failed"** — the injector now reads `LoadLibraryW`'s
  return value directly instead of polling the module snapshot, so success is
  reported correctly.
- **Goat watermark** — the in-game Text GUI watermark now renders the Goat
  Client logo + wordmark instead of the Vape logo.
- **Profile management** — `.goat profile list/create/switch/rename/delete/duplicate`
  chat commands.
- **Stable saves** — settings persist locally without network errors.

## Config commands

| Command | Effect |
| :--- | :--- |
| `.goat save` / `.goat load` | Save / reload config |
| `.goat export <path>` / `.goat import <path>` | Export / import JSON config |
| `.goat profile ...` | Profile management (list/create/switch/rename/delete/duplicate) |
| `.goat path` | Show config location (`~/.goat-client/config.json`) |

## Supported versions

1.7.10, 1.8.9, 1.12.2, 1.21.11, 26.2 (Vanilla/Forge/Fabric) + Lunar/Badlion
1.8.9. 64-bit JVM only.

## Known limitations

- 1.16.5 support is incomplete (mapping/rendering issues).
- This is a cheat client — multiplayer use risks a ban.
