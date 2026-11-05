# Slay the Spire PS3

Climb the Spire on a PlayStation 3. This is an unofficial port of Slay the
Spire, built from your own copy of the game.

All four characters play, with music and sound. It runs in the RPCS3 emulator.
It is not yet tested on a real PS3.

This repository has no game code or game art. The builder converts the game
from the copy you own on Steam.

## Neow's blessing: what you need

- A Windows PC with about 15 GB of free disk space.
- [Docker Desktop](https://www.docker.com/products/docker-desktop/), installed
  and running.
- Slay the Spire from Steam.
- An internet connection the first time you build.

## Act 1: build the PKG

1. Start Docker Desktop. Wait until it says it is running.
2. Find `desktop-1.0.jar`. In Steam, right-click Slay the Spire, then choose
   Manage > Browse local files.
3. Drag `desktop-1.0.jar` onto `build_pkg.bat`.
4. Wait. The first build takes 20 to 30 minutes, because it downloads the PS3
   build tools. Later builds take about 15 minutes.
5. `SlayTheSpire-PS3.pkg` appears next to `build_pkg.bat`.

If the build stops, check that Docker Desktop is running, then run
`build_pkg.bat` again.

## Act 2: install it

- RPCS3: choose File > Install Packages/Raps/Edats and pick the PKG.
- PS3 with HEN or custom firmware: copy the PKG to a USB stick. On the PS3,
  open Package Manager > Install Package Files and pick the PKG.

## Act 3: known issues

- Effects that grey out cards and relics show in full colour.
- It is tested only in RPCS3. Real hardware can behave differently.
- If the game crashes, it writes a log to `/dev_hdd0/tmp/stsps3.log`. In RPCS3
  that is `dev_hdd0\tmp\stsps3.log` inside the RPCS3 folder.

## Relics: how it works

The game is written in Java. [TeaVM](https://teavm.org/) compiles it ahead of
time to C, and the PS3 toolchain ([PSL1GHT](https://github.com/ps3dev/PSL1GHT))
compiles that C for the PS3's processor. A small native layer replaces the
desktop libraries: graphics on the RSX, controller input, audio, files and the
on-screen keyboard. Everything runs in Docker, so you install no compilers.

Developer notes are in [docs/ps3-menu.md](docs/ps3-menu.md).

## Credits

- Based on [STSPSVITA](https://github.com/mmrmagno/stspsvita) by mmrmagno, the
  Vita version of this pipeline. Its original instructions are in
  [docs/vita-README.md](docs/vita-README.md).
- PS3 toolchain by the [ps3dev](https://github.com/ps3dev) project.
- Slay the Spire is made by [Mega Crit](https://www.megacrit.com/). This
  project is not affiliated with or endorsed by Mega Crit. Buy the game.

The build tooling is MIT licensed. See [LICENSE](LICENSE).
