# Slay the Spire PS3: developer notes

The port takes the owner's Steam `desktop-1.0.jar` read-only and converts the
original Java game through STSPSVITA's TeaVM pipeline. PSL1GHT implementations
replace the Vita backend's native services. All four characters play in RPCS3
with sound. Real PS3 validation is still open. No game-derived files are
committed or published.

## Build

Players use `build_pkg.bat` (see `HOW_TO_BUILD_PKG.txt`). It runs the same
script that developers use:

```powershell
ps3/build.ps1 -Prepare            # convert the JAR in game/ and compile
ps3/build.ps1                     # recompile native changes only
ps3/build.ps1 -Package            # also write build/ps3/slaythespire.pkg
ps3/run-rpcs3.ps1 -Capture -CaptureFrames @(900,1800) -PadScript @('600 4000','606 0')
```

`ps3/Dockerfile` is self-contained. It pins the ps3dev nightly (toolchain,
PSL1GHT, portlibs) and Gradle by hash, and adds JDK 21 and Pillow. TeaVM is
pinned upstream to 0.15.0. Compilation runs in the `sts-ps3-native` Docker
volume to avoid Windows file-share overhead. Re-run `-Prepare` after Java
changes. Native changes need only a build.

`ps3/make_pkg.py` writes the PKG. PSL1GHT's `pkg` tool aborts past about 1024
entries and the assets alone are about 3700. For the same folder, the output of
`make_pkg.py` is byte-identical to PSL1GHT's. The XMB icon comes from the
player's own title art (`pipeline/make_icon.py`).

## Runtime layout

APPID `STSP00001` reads `/dev_hdd0/game/STSP00001/USRDIR/assets` and writes its
log to `/dev_hdd0/tmp/stsps3.log`. The log reports memory headroom (`[ps3 mem]`)
and frame statistics (`[PS3 frame]`). RPCS3 empties `/dev_hdd0/tmp` at boot, so
`run-rpcs3.ps1` puts its capture and pad-script files in `USRDIR`.

## Known limits

- Custom shader effects (for example greyscale) use the basic sprite shader.
- Mipmaps sample the base level only.
- The Java heap is 128 MiB, which leaves about 25 MiB of system memory free in
  combat.

Upstream source: https://github.com/mmrmagno/stspsvita (MIT). The PS3 layer uses
PSL1GHT and rendering and platform patterns from the Space Cadet and Balatro PS3
ports. No Sony SDK is used.
