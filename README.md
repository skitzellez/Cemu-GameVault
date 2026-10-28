# Game Vault · Wii U (Cemu) — for Xbox Series X|S (Dev Mode)

Cemu, the Wii U emulator, built for [Game Vault](https://github.com/skitzellez/xbox-game-vault): pick a Wii U game in
Game Vault and it starts here, then comes back to Game Vault when you quit. On the console it is listed as
**Game Vault · Wii U (Cemu)** (Xbox Series X|S only).

## How to use it

1. Install it with the Game Vault setup on your PC (it does everything), or in Device Portal: **My games & apps → Add**,
   choose `CemuForGameVault_<version>_x64.appx`, tick the box for dependencies and add
   [`Microsoft.VCLibs.x64.14.00.appx`](https://github.com/skitzellez/xbox-game-vault/releases/download/runtime-vclibs/Microsoft.VCLibs.x64.14.00.appx), then **Start**.
2. Put your Wii U games on the games drive (or a network share Game Vault knows): `.wua`, `.wud`, `.wux`, or an
   unpacked game folder (`code\*.rpx`).
3. Pick a Wii U game in Game Vault and press **A**.

## ⚠️ Keys: you need your own

Encrypted `.wud` / `.wux` disc images need the **disc key from your own Wii U dump**: put it next to the game with the
same name (`Game.wux` → `Game.key`, 16 bytes), or put your own `keys.txt` in the game's folder or a folder above it
(for example your Wii U games folder on the USB drive or the PC share). This app can't see the `keys.txt` of another
Cemu on the console, so copy it over once.
**Without the key the game cannot start and Cemu opens on its own menu instead.** That is not a crash: add the key and
start the game again from Game Vault.

**No games, keys or Nintendo files come with this app, and we never share, sell or link to keys. Please don't ask.**

## Credits

The Xbox (UWP) port and its Direct3D 12 renderer are the work of **DanP142**
([danprice142/Cemu-UWP](https://github.com/danprice142/Cemu-UWP)). Cemu itself is by the
[Cemu team](https://github.com/cemu-project/Cemu). This build adds only what Game Vault needs: its own app name
(so another Cemu on the console stays as it is), the `gamevault-cemu:` link, and messages back to Game Vault when a
game cannot start (for example a missing key).

Licence: Mozilla Public License 2.0, like Cemu (files in `dependencies` keep their own licences). The source of
every release is this branch (`gamevault`).

---

# **Cemu - Wii U emulator**

[![Build Process](https://github.com/cemu-project/Cemu/actions/workflows/build.yml/badge.svg)](https://github.com/cemu-project/Cemu/actions/workflows/build.yml)
[![Discord](https://img.shields.io/discord/286429969104764928?label=Cemu&logo=discord&logoColor=FFFFFF)](https://discord.gg/5psYsup)
[![Matrix Server](https://img.shields.io/matrix/cemu:cemu.info?server_fqdn=matrix.cemu.info&label=cemu:cemu.info&logo=matrix&logoColor=FFFFFF)](https://matrix.to/#/#cemu:cemu.info)

This is the code repository of Cemu, a Wii U emulator that is able to run most Wii U games and homebrew in a playable state.
It's written in C/C++ and is being actively developed with new features and fixes.

Cemu is currently only available for 64-bit Windows, Linux & macOS devices.

### Links:
 - [Open Source Announcement](https://www.reddit.com/r/cemu/comments/wwa22c/cemu_20_announcement_linux_builds_opensource_and/)
 - [Official Website](https://cemu.info)
 - [Compatibility List/Wiki](https://wiki.cemu.info/wiki/Main_Page)
 - [Official Subreddit](https://reddit.com/r/Cemu)
 - [Official Discord](https://discord.gg/5psYsup)
 - [Official Matrix Server](https://matrix.to/#/#cemu:cemu.info)
 - [Setup Guide](https://cemu.cfw.guide)

#### Other relevant repositories:
 - [Cemu-Language](https://github.com/cemu-project/Cemu-Language)
 - [Cemu's Community Graphic Packs](https://github.com/cemu-project/cemu_graphic_packs)

## Download

You can download the latest Cemu releases for Windows, Linux and Mac from the [GitHub Releases](https://github.com/cemu-project/Cemu/releases/). For Linux you can also find Cemu on [Flathub](https://flathub.org/apps/info.cemu.Cemu).

On Windows, Cemu is available both as an installer and in a portable format, where no installation is required besides extracting it in a safe place.

The native macOS build is currently purely experimental and should not be considered stable or ready for issue-free gameplay. There are also known issues with degraded performance due to the use of MoltenVK and Rosetta for ARM Macs. We appreciate your patience while we improve Cemu for macOS.

Pre-2.0 releases can be found on Cemu's [changelog page](https://cemu.info/changelog.html).

## Build Instructions

To compile Cemu yourself on Windows, Linux or macOS, view [BUILD.md](/BUILD.md).

## Issues

Issues with the emulator should be filed using [GitHub Issues](https://github.com/cemu-project/Cemu/issues).  
The old bug tracker can be found at [bugs.cemu.info](https://bugs.cemu.info) and still contains relevant issues and feature suggestions.

## Contributing

If you want to contribute you can take a look at our [contribution guidelines](/CONTRIBUTING.md).

## License
Cemu is licensed under [Mozilla Public License 2.0](/LICENSE.txt). Exempt from this are all files in the dependencies directory for which the licenses of the original code apply as well as some individual files in the src folder, as specified in those file headers respectively.
