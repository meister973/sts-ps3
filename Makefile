# STSVITA build orchestration.
#
#   make vpk STS_DIR=~/.steam/root/steamapps/common/SlayTheSpire
#
# Options:
#   LANGS=eng,jpn     game languages whose (CJK) fonts to keep
#   AUDIO_KBPS=96     re-encode OGGs (needs ffmpeg); default: copy through

STS_DIR       ?=
LANGS         ?= eng
AUDIO_KBPS    ?= 0
WORK          := work
PY            := python3
BUNDLE_ASSETS ?= ON
VITA_IP       ?=

.PHONY: all vpk vpk-slim verify extract assets livearea transpile native deploy-assets clean

all: vpk

verify:
	@test -n "$(STS_DIR)" || { echo "set STS_DIR=<path to your Slay the Spire install>"; exit 2; }
	$(PY) pipeline/verify.py --sts-dir "$(STS_DIR)"

extract: verify
	$(PY) pipeline/extract.py --sts-dir "$(STS_DIR)" --work $(WORK)

assets: extract
	$(PY) pipeline/assets.py --work $(WORK) --langs $(LANGS) --audio-kbps $(AUDIO_KBPS)

# LiveArea bubble icon + background + start gate, derived from the game files.
livearea: assets
	$(PY) pipeline/livearea.py --work $(WORK)

transpile:
	@test -f $(WORK)/jar_path.txt || { echo "run 'make extract' first"; exit 2; }
	cd transpiler && gradle transpile

native:
	@test -n "$$VITASDK" || { echo "VITASDK env var not set — install https://vitasdk.org"; exit 2; }
	cmake -S native -B build/native \
	  -DCMAKE_TOOLCHAIN_FILE=$$VITASDK/share/vita.toolchain.cmake \
	  -DGENERATED_C=$(abspath $(WORK)/generated_c) \
	  -DASSETS=$(abspath $(WORK)/assets_vita) \
	  -DBUNDLE_ASSETS=$(BUNDLE_ASSETS) \
	  $(if $(wildcard $(WORK)/sce_sys),-DSCE_SYS=$(abspath $(WORK)/sce_sys),)
	cmake --build build/native -j
	@mkdir -p build
	@cp build/native/STSVITA.vpk build/STSVITA$(if $(filter OFF,$(BUNDLE_ASSETS)),-slim).vpk
	@echo "VPK: build/STSVITA$(if $(filter OFF,$(BUNDLE_ASSETS)),-slim).vpk"

# Full, self-contained VPK (code + assets). Install once; large (~335MB).
vpk: assets livearea transpile native

# Slim, code-only VPK (~7MB) — reinstalls in seconds while iterating on code.
# The game reads assets from ux0:data/stsvita/assets (VitaFiles auto-detects
# when app0:/assets is absent), so push them once: make deploy-assets VITA_IP=..
vpk-slim: livearea transpile
	$(MAKE) native BUNDLE_ASSETS=OFF
	@echo "Slim VPK needs assets at ux0:data/stsvita/assets — make deploy-assets VITA_IP=.."

# One-time upload of the processed asset payload to the Vita over VitaShell FTP
# (enable it with SELECT in VitaShell; default port 1337).
deploy-assets:
	@test -d "$(WORK)/assets_vita" || { echo "no $(WORK)/assets_vita — run 'make assets' first"; exit 2; }
	@test -n "$(VITA_IP)" || { echo "set VITA_IP=<vita ip>  (VitaShell FTP)"; exit 2; }
	$(PY) tools/deploy_assets.py --src $(WORK)/assets_vita \
	  --host $(VITA_IP) --dest ux0:/data/stsvita/assets

clean:
	rm -rf $(WORK) build transpiler/build transpiler/.gradle
