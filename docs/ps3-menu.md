# Slay the Spire PS3: first menu milestone

Use the owner's Steam desktop-1.0.jar read-only, copied into ignored game/.
Convert the original Java game through STSPSVITA's TeaVM pipeline and replace
the Vita backend's native services with PSL1GHT implementations. Reach the
original title menu with rendering and controller navigation. Gameplay is a
later milestone. This is an experimental port, not a screenshot recreation.

Build in Docker using the existing balatro-ps3dev:2 toolchain plus JDK/Gradle.
Use original English fonts initially; CJK font payload is excluded. Reuse
the existing PS3 projects' native rendering and platform patterns, retaining
their notices. Compile shaders offline; no runtime shader compiler is assumed.

Verification: cross-build the actual PPU ELF, boot in RPCS3, capture the menu
and log. Real PS3 validation remains necessary. No game-derived files are
committed or published. Output: build/ps3/slaythespire.elf and a private local PKG.

## Local build

The Steam installation stays unchanged. A copy of desktop-1.0.jar lives in
ignored game/. All extracted assets and generated game code stay under work/.

```powershell
ps3/build.ps1 -Prepare
ps3/run-rpcs3.ps1 -Capture -PadScript @('600 4000', '602 0')
# Optional private installable package, containing your own transformed game:
ps3/build.ps1 -Package
```

The build reuses the local balatro-ps3dev:2 image and extends it with JDK 21,
Gradle 8.14.3, and Pillow. TeaVM is pinned upstream to 0.15.0. Compilation and
linking run in the sts-ps3-native Docker volume to avoid Windows file-share
overhead. Re-run -Prepare after changing Java; native changes need only build.

APPID STSP00001 reads /dev_hdd0/game/STSP00001/USRDIR/assets and writes its
log to /dev_hdd0/tmp/stsps3.log. The emulator script stages assets, preserves
run evidence, and only stops the emulator process it starts. Capture mode
uses hidden Vulkan and requests a native framebuffer dump at frame 900.

## Scope and limits

This milestone uses the original CardCrawlGame and original English fonts.
The starting Java heap is 80 MiB within the PS3's separate main-memory budget.
The RSX sprite renderer uses offline Cg shaders and DXT textures. Audio is
explicitly unavailable/muted. Custom shader effects currently use the basic
sprite shader; mipmaps sample the base level. Unsupported GL operations report
errors. Full gameplay, audio, and hardware performance are outside this spike.

Upstream source: https://github.com/mmrmagno/stspsvita (MIT). The PS3 layer uses
PSL1GHT and rendering/platform patterns from the local Space Cadet and CellJoker
ports. No Sony SDK or runtime shader compiler is used.
