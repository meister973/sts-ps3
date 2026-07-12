#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

STS_DIR="${STS_DIR:-$ROOT/game}"
LANGS="${LANGS:-eng}"
AUDIO_KBPS="${AUDIO_KBPS:-0}"
MODE="${BUILD_MODE:-slim}"

usage() {
    cat <<'USAGE'
Usage: ./build.sh [options]

Options:
  --sts-dir PATH      Folder containing desktop-1.0.jar
  --langs LIST        Comma-separated languages to keep, default: eng
  --audio-kbps N      Re-encode OGG audio at N kbps, default: 0 (copy)
  --slim              Build code-only VPK plus external assets, default
  --full              Bundle assets into the VPK
  -h, --help          Show this help

Environment variables are also supported:
  STS_DIR, LANGS, AUDIO_KBPS, BUILD_MODE
USAGE
}

die() {
    echo "error: $*" >&2
    exit 2
}

need_command() {
    command -v "$1" >/dev/null 2>&1 || die "$1 not found on PATH"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sts-dir)
            [[ $# -ge 2 ]] || die "--sts-dir needs a path"
            STS_DIR="$2"
            shift 2
            ;;
        --langs)
            [[ $# -ge 2 ]] || die "--langs needs a value"
            LANGS="$2"
            shift 2
            ;;
        --audio-kbps)
            [[ $# -ge 2 ]] || die "--audio-kbps needs a value"
            AUDIO_KBPS="$2"
            shift 2
            ;;
        --slim)
            MODE="slim"
            shift
            ;;
        --full)
            MODE="full"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

[[ "$MODE" == "slim" || "$MODE" == "full" ]] || die "BUILD_MODE must be slim or full"
[[ -f "$STS_DIR/desktop-1.0.jar" ]] || die "$STS_DIR/desktop-1.0.jar not found"
[[ -n "${VITASDK:-}" ]] || die "VITASDK is not set"

need_command python3
need_command gcc
need_command gradle
need_command cmake
need_command magick

if [[ "$AUDIO_KBPS" != "0" ]]; then
    need_command ffmpeg
fi

target="vpk-slim"
vpk="build/STSVITA-slim.vpk"
if [[ "$MODE" == "full" ]]; then
    target="vpk"
    vpk="build/STSVITA.vpk"
fi

make "$target" STS_DIR="$STS_DIR" LANGS="$LANGS" AUDIO_KBPS="$AUDIO_KBPS"

echo
echo "Build complete."
echo "VPK: $vpk"
if [[ "$MODE" == "slim" ]]; then
    echo "Assets: work/assets_vita"
    echo "Copy work/assets_vita contents to ux0:data/stsvita/assets/ on the Vita."
fi
