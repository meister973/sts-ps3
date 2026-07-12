#!/usr/bin/env python3
"""Fingerprint the user's Slay the Spire jar and sanity-check its contents.

Usage: verify.py --sts-dir <install dir>
"""

from __future__ import annotations

import argparse
from pathlib import Path

from stsvita_common import (
    ASSET_DIRS, KNOWN_JARS, find_jar, is_asset, open_jar, sha256_file,
)

EXPECTED_MAIN = "com.megacrit.cardcrawl.desktop.DesktopLauncher"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--sts-dir", type=Path, required=True)
    args = ap.parse_args()

    jar = find_jar(args.sts_dir)
    size_mb = jar.stat().st_size / 1048576
    digest = sha256_file(jar)

    print(f"jar:    {jar}")
    print(f"size:   {size_mb:.1f} MB")
    print(f"sha256: {digest}")

    problems = []
    with open_jar(jar) as z:
        names = set(z.namelist())

        manifest = z.read("META-INF/MANIFEST.MF").decode("utf-8", "replace") \
            if "META-INF/MANIFEST.MF" in names else ""
        if EXPECTED_MAIN.replace(".", "/") + ".class" not in names:
            problems.append(f"missing main class {EXPECTED_MAIN}")
        if "com/badlogic/gdx/Version.class" in names:
            raw = z.read("com/badlogic/gdx/Version.class")
            # the version literal is the only dotted-triple string constant
            import re
            m = re.search(rb"\x01\x00.(\d+\.\d+\.\d+)", raw)
            gdx = m.group(1).decode() if m else "unknown"
            print(f"libGDX: {gdx}")
            if not gdx.startswith("1.9."):
                problems.append(f"unexpected libGDX version {gdx} (pipeline "
                                f"targets 1.9.x backend interfaces)")
        else:
            problems.append("libGDX Version class not found")

        missing_dirs = {d for d in ASSET_DIRS
                        if not any(n.startswith(d + "/") for n in names)}
        if missing_dirs:
            problems.append(f"asset dirs missing from jar: {sorted(missing_dirs)}")

        asset_bytes = sum(i.file_size for i in z.infolist()
                          if is_asset(i.filename))
        print(f"assets: {asset_bytes / 1048576:.1f} MB across "
              f"{sum(1 for n in names if is_asset(n))} files")

    if digest in KNOWN_JARS:
        print(f"status: KNOWN GOOD — {KNOWN_JARS[digest]}")
    else:
        print("status: unrecognized build — probably fine (platform/storefront "
              "variants differ); please report your fingerprint if the build "
              "succeeds")

    if manifest and "Main-Class: " in manifest:
        main_cls = manifest.split("Main-Class: ", 1)[1].split()[0]
        if main_cls != EXPECTED_MAIN:
            problems.append(f"unexpected Main-Class {main_cls}")

    for p in problems:
        print(f"WARNING: {p}")
    if problems:
        return 1
    print("verify: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
