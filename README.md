<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->
# NXAevora

NXAevora is a project to run Pokemon Legends Arceus natively on a Windows PC. The code of the game is translated
ahead of time from ARM64 to x86-64 with the static recompiler [ScarletNX](https://github.com/scarletnx/ScarletNX).
NXAevora adds the settings, patches and tools that are specific to this game, and a launcher is planned.

## Status

NXAevora is at an early stage. It is not ready to play.

- The base game and update 1.1.1 recompile in a few minutes.
- The recompiled game starts, shows the language selection and plays the opening scene at sea at about 30 frames
  each second. This was tested on one machine, only as far as that scene.
- The recompiled code is not complete in general. When the game reaches code that the recompiler missed, the
  ScarletNX runtime compiles it at that moment. We study these misses and remove them. In the opening scene, the
  misses are gone. Other scenes are not tested. See [ROADMAP.md](ROADMAP.md) and
  [docs/jit-analysis.md](docs/jit-analysis.md).
- The launcher does not exist yet.

## What you need

- Windows 11 x64.
- A GPU and a driver with Vulkan 1.3.
- Your own copy of the game, dumped from your own console, with the update. NXAevora accepts NSP and XCI files.
- The keys of your console (`prod.keys`, and `title.keys` for NSP files), dumped from your own console.

NXAevora contains no game files and no keys. Do not ask for them and do not share them.

## Build

Clone the repository with its submodule:

```sh
git clone --recursive <this repository>
```

The build steps for Windows are in [docs/SCARLETNX_BUILD-NOTES.md](docs/SCARLETNX_BUILD-NOTES.md). They use MSYS2
and the CLANG64 environment.

## Repository layout

| Folder | Content |
| --- | --- |
| `third_party/ScarletNX/` | The recompiler and the Switch runtime (submodule) |
| `recomp/` | Settings and patches for the recompile of this game |
| `runtime/` | Parts of the runtime that ScarletNX does not have |
| `launcher/` | The launcher (planned) |
| `tools/` | Helper scripts for the analysis of the recompiled game |
| `docs/` | Notes and analysis |

## Legal

NXAevora is an independent project. It is not affiliated with or endorsed by Nintendo, The Pokemon Company or Game
Freak. Pokemon and Pokemon Legends Arceus are trademarks of their owners.

You must own the game to use NXAevora. The files that you make with it, such as the recompiled code, come from the
game and are for your own use. Do not distribute them.

## License

NXAevora is free software under the GNU General Public License, version 3 or later. See [LICENSE](LICENSE).
ScarletNX is also GPL-3.0. Its license and the list of the projects that it builds on are in
`third_party/ScarletNX/`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
