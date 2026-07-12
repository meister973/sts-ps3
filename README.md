# STSPSVITA

Build your own PlayStation Vita homebrew package for Slay the Spire from your
legally purchased PC copy.

This repository contains no Slay the Spire code, assets, or compiled game
output. The build runs locally, reads your `desktop-1.0.jar`, produces a Vita
asset folder, and builds a VPK for your own hacked PS Vita.

Status: pre-alpha. Expect rough edges, long build times, and hardware-specific
issues.

## What You Need

- A PS Vita or PSTV with HENkaku/h-encore and `libshacccg.suprx` installed.
- A legal PC copy of Slay the Spire with `desktop-1.0.jar`.
- A Linux, macOS, or WSL build machine with:
  - VitaSDK, with `VITASDK` set in your environment.
  - JDK 17 or newer.
  - Gradle.
  - CMake.
  - Python 3.
  - GCC.
  - ImageMagick, providing the `magick` command.
  - ffmpeg, optional, only needed when re-encoding audio.

## Quick Start

Copy your Slay the Spire jar into the repo:

```sh
mkdir -p game
cp /path/to/SlayTheSpire/desktop-1.0.jar game/
```

Then run the build:

```sh
./build.sh
```

Default output:

- `build/STSVITA-slim.vpk`
- `work/assets_vita/`

Install `build/STSVITA-slim.vpk` with VitaShell.

Copy the contents of `work/assets_vita/` to this folder on your Vita:

```text
ux0:data/stsvita/assets/
```

The final layout should look like:

```text
ux0:data/stsvita/assets/build.properties
ux0:data/stsvita/assets/audio/...
ux0:data/stsvita/assets/images/...
```

You can also upload the assets over VitaShell FTP:

```sh
make deploy-assets VITA_IP=<your-vita-ip>
```

## Build Options

Use a game install folder instead of copying the jar:

```sh
STS_DIR="$HOME/.steam/root/steamapps/common/SlayTheSpire" ./build.sh
```

Keep extra language fonts:

```sh
LANGS=eng,jpn ./build.sh
```

Re-encode OGG audio at a lower bitrate:

```sh
AUDIO_KBPS=96 ./build.sh
```

Build a full VPK with assets bundled inside:

```sh
./build.sh --full
```

The full VPK is larger and slower to reinstall. The default slim VPK is usually
better: install the VPK once, copy the assets once, and rebuild/reinstall code
quickly after that.

Run `./build.sh --help` for all script flags.

## Manual Make Targets

The script wraps these make targets:

```sh
make verify STS_DIR=game
make assets STS_DIR=game
make vpk-slim STS_DIR=game
make vpk STS_DIR=game
```

Generated output is written to `work/` and `build/`. Those folders are ignored
by git and should not be distributed.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `build.sh` | One-command public build entrypoint. |
| `game/` | Local drop folder for `desktop-1.0.jar`; ignored by git. |
| `pipeline/` | Python verification, extraction, asset, and LiveArea tools. |
| `backend/` | libGDX Vita backend and desktop-service stubs. |
| `transpiler/` | TeaVM AOT harness and Vita launcher. |
| `native/` | Vita C entrypoint, glue, CMake build, and packaging. |
| `tools/` | Build helper tools, including asset FTP upload. |

## Legal

Slay the Spire is copyright Mega Crit Games. This project is not affiliated
with or endorsed by Mega Crit. Do not distribute built VPKs or generated asset
folders, because they contain game content from your copy. Distribute this
source repository instead.

## License

MIT for this build tooling. See `LICENSE`.
