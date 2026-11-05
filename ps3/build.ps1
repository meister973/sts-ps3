param([switch]$Prepare, [switch]$Package)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$image = 'sts-ps3dev:2'
docker image inspect $image *> $null
if ($LASTEXITCODE -ne 0) {
    docker build -t $image -f "$PSScriptRoot/Dockerfile" $root
    if ($LASTEXITCODE -ne 0) { throw 'Toolchain image build failed' }
}
if ($Prepare) {
    docker run --rm -v "${root}:/work" -v sts-ps3-gradle:/root/.gradle -w /work $image sh ps3/prepare.sh
    if ($LASTEXITCODE -ne 0) { throw 'Game conversion failed' }
}
$target = if ($Package) { 'pkg' } else { 'all' }
# Thousands of compiler/header/linker reads are very slow over Docker's Windows
# file share. Transfer one tar, then build on the named Linux filesystem cache.
$bundle = Join-Path $root 'work/ps3-build-input.tar'
$inputs = @('ps3', 'native/src/glue', 'native/third_party', 'work/generated_c')
if (Test-Path (Join-Path $root 'work/icon/ICON0.PNG')) { $inputs += 'work/icon' }
& tar.exe -cf $bundle -C $root @inputs
if ($LASTEXITCODE -ne 0) { throw 'Build input archive failed' }
$buildCommand = "tar -xf /host/work/ps3-build-input.tar && make -f ps3/Makefile -j8 ASSETS=/host/work/assets_vita $target && mkdir -p /host/build/ps3 && cp build/ps3/slaythespire.elf build/ps3/slaythespire.self build/ps3/slaythespire.map /host/build/ps3/"
if ($Package) {
    $buildCommand += ' && cp build/ps3/slaythespire.pkg /host/build/ps3/ && mkdir -p /host/build/ps3/pkg && cp build/ps3/pkg/PARAM.SFO build/ps3/pkg/ICON0.PNG /host/build/ps3/pkg/'
}
docker run --rm -v "${root}:/host" -v sts-ps3-native:/work -w /work $image sh -c $buildCommand
if ($LASTEXITCODE -ne 0) { throw 'PS3 build failed' }
