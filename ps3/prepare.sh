#!/bin/sh
set -eu
cd /work
python3 pipeline/verify.py --sts-dir game
python3 pipeline/extract.py --sts-dir game --work work
python3 pipeline/make_icon.py --work work
python3 pipeline/assets.py --work work --langs eng --audio-kbps 0 --jobs 8
cd transpiler
gradle --no-daemon transpile
