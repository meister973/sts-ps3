# STSPSVITA

Build your own PlayStation Vita homebrew package for Slay the Spire from your
legally purchased PC copy.

This repository contains no Slay the Spire code, assets, or compiled game
output. The build runs locally, reads your `desktop-1.0.jar`, produces Vita
assets, and builds a VPK for your own hacked PS Vita.

Status: pre-alpha. Expect rough edges, long build times, and
hardware-specific issues.

## No Prebuilt VPK

This project cannot ship a ready-made VPK. The Vita executable is generated
from your local `desktop-1.0.jar`, so a public VPK would contain transformed
Slay the Spire game code. The safe distribution model is:

1. Clone this source repo.
2. Provide your own legally purchased `desktop-1.0.jar`.
3. Run the build locally.
4. Install the locally generated VPK on your own Vita.

Do not upload generated VPKs or generated asset folders to GitHub Releases,
VitaDB, forums, Discord, or mirrors.

## Requirements

- PS Vita or PSTV with HENkaku/h-encore.
- `libshacccg.suprx` installed on the Vita.
- A legal PC copy of Slay the Spire containing `desktop-1.0.jar`.
- A Linux, macOS, or WSL2 build machine.
- VitaSDK, with `VITASDK` set and `$VITASDK/bin` on `PATH`.
- JDK 17 or newer.
- Gradle.
- CMake, Make, Python 3, GCC.
- ImageMagick, either `magick` or `convert`.
- `ffmpeg`, optional, only when re-encoding audio.

## Windows Setup

Native Windows builds are not supported. Use WSL2 with Ubuntu.

Install WSL2 from an elevated PowerShell:

```powershell
wsl --install -d Ubuntu
```

Open Ubuntu, then install host build tools:

```sh
sudo apt update
sudo apt install -y \
  build-essential git make cmake python3 \
  openjdk-17-jdk imagemagick ffmpeg \
  unzip zip wget ca-certificates
```

Install Gradle. If your Ubuntu package is modern enough, this is fine:

```sh
sudo apt install -y gradle
```

Check versions:

```sh
java -version
gradle --version
```

Use JDK 17 or newer. If Ubuntu's Gradle package is too old, install Gradle via
SDKMAN or the official Gradle binary instead.

For reliability, keep the repo inside the WSL Linux filesystem, such as
`~/stspsvita`, not under `/mnt/c/...`.

If WSL runs out of memory during TeaVM transpilation, create this file on the
Windows side:

```text
C:\Users\<you>\.wslconfig
```

with:

```ini
[wsl2]
memory=8GB
processors=4
swap=8GB
```

Then restart WSL:

```powershell
wsl --shutdown
```

## VitaSDK Setup

Inside Linux, macOS, or WSL2:

```sh
sudo mkdir -p /usr/local/vitasdk
sudo chown "$USER:$USER" /usr/local/vitasdk

echo 'export VITASDK=/usr/local/vitasdk' >> ~/.bashrc
echo 'export PATH=$VITASDK/bin:$PATH' >> ~/.bashrc
source ~/.bashrc

git clone https://github.com/vitasdk/vdpm
cd vdpm
./bootstrap-vitasdk.sh
./install-all.sh
```

Open a new shell and verify:

```sh
echo "$VITASDK"
arm-vita-eabi-gcc --version
```

If the native link step fails with `cannot find -l...`, install or update the
missing VitaSDK package, then rerun the build.

## Quick Start

Clone the repo and copy in your game jar:

```sh
git clone <repo-url> stspsvita
cd stspsvita

mkdir -p game
cp /path/to/SlayTheSpire/desktop-1.0.jar game/
```

On WSL, a Steam install is usually reachable through `/mnt/c`, for example:

```sh
cp "/mnt/c/Program Files (x86)/Steam/steamapps/common/SlayTheSpire/desktop-1.0.jar" game/
```

Build:

```sh
./build.sh
```

Default output:

- `build/STSVITA-slim.vpk`
- `work/assets_vita/`

Install `build/STSVITA-slim.vpk` with VitaShell.

Copy the contents of `work/assets_vita/` to:

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

## Troubleshooting

`desktop-1.0.jar not found`

Point `STS_DIR` at the folder containing `desktop-1.0.jar`, or copy the jar to
`game/desktop-1.0.jar`.

`VITASDK is not set`

Reload your shell after adding `VITASDK` to `.bashrc`, or run:

```sh
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH
```

`magick not found` or `convert not found`

Install ImageMagick:

```sh
sudo apt install -y imagemagick
```

TeaVM or Gradle runs out of memory

Give WSL more memory, close other applications, and rerun `./build.sh`.

Vita linker error: `cannot find -l<name>`

Your VitaSDK packages are incomplete or outdated. Run:

```sh
vitasdk-update
```

or install the missing package through VitaSDK's package manager.

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
