# Boot the sprxlinked original-game ELF, retain evidence, and stop only this run.
# Default: hidden headless Null renderer for logic. -Capture: hidden Vulkan readback.
[CmdletBinding()]
param(
    [switch]$Capture,
    [ValidateRange(1, 86400)][int]$TimeoutSeconds = 180,
    [switch]$KeepRunning,
    [string]$PpuDecoder = 'Recompiler (LLVM)',
    # One '<pollFrame> <hex Vita mask>' per event, held until the next event.
    [string[]]$PadScript,
    [string]$Rpcs3Directory = $(if ($env:RPCS3_DIR) { $env:RPCS3_DIR } else { Join-Path $env:USERPROFILE 'Desktop\rpcs3' })
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$rpcs3Root = [IO.Path]::GetFullPath($Rpcs3Directory)
$executable = Join-Path $rpcs3Root 'rpcs3.exe'
$elfPath = Join-Path $repoRoot 'build\ps3\slaythespire.elf'
$sourceAssets = Join-Path $repoRoot 'work\assets_vita'
$gameDirectory = Join-Path $rpcs3Root 'dev_hdd0\game\STSP00001'
$usrDirectory = Join-Path $gameDirectory 'USRDIR'
$assetDirectory = Join-Path $usrDirectory 'assets'
$temporaryDirectory = Join-Path $rpcs3Root 'dev_hdd0\tmp'
$gameLog = Join-Path $temporaryDirectory 'stsps3.log'
# RPCS3 empties dev_hdd0/tmp at boot, so the capture request lives in the title's USRDIR.
$captureFlag = Join-Path $usrDirectory 'sts-capture-menu'
$capturePath = Join-Path $temporaryDirectory 'sts-menu.ppm'
$padScriptPath = Join-Path $temporaryDirectory 'sts-pad-script.txt'

foreach ($required in @($executable, $elfPath, $sourceAssets)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing required path: $required" }
}
if (Get-Process -Name rpcs3 -ErrorAction SilentlyContinue) {
    throw 'RPCS3 is already running. Its process and logs belong to that session; close it before this validation run.'
}

function Read-SharedText([string]$Path) {
    $stream = $null
    $reader = $null
    try {
        $stream = [IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite, Delete')
        $reader = [IO.StreamReader]::new($stream)
        return $reader.ReadToEnd()
    } catch { return '' }
    finally {
        if ($null -ne $reader) { $reader.Dispose() }
        elseif ($null -ne $stream) { $stream.Dispose() }
    }
}

function Copy-SharedFile([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { return }
    $inputStream = $null
    $outputStream = $null
    try {
        $inputStream = [IO.File]::Open($Source, 'Open', 'Read', 'ReadWrite, Delete')
        $outputStream = [IO.File]::Open($Destination, 'Create', 'Write', 'Read')
        $inputStream.CopyTo($outputStream)
    } finally {
        if ($null -ne $outputStream) { $outputStream.Dispose() }
        if ($null -ne $inputStream) { $inputStream.Dispose() }
    }
}

function Copy-IfChanged([IO.FileInfo]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $target = Get-Item -LiteralPath $Destination
        if ($target.Length -eq $Source.Length) {
            if ($target.LastWriteTimeUtc -eq $Source.LastWriteTimeUtc) { return $false }
            # Same bytes with different timestamps also need no duplicate copy.
            if ((Get-FileHash -LiteralPath $Source.FullName -Algorithm SHA256).Hash -eq
                (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash) { return $false }
        }
    }
    [IO.Directory]::CreateDirectory((Split-Path -Parent $Destination)) | Out-Null
    Copy-Item -LiteralPath $Source.FullName -Destination $Destination
    return $true
}

function Test-FreshFile([string]$Path, [datetime]$Since) {
    $item = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
    return $null -ne $item -and $item.LastWriteTimeUtc -ge $Since
}

# Hidden launch alone does not hide a later-created render window. Hide only our PID.
if ($Capture -and -not ('StsPs3ValidationWindows' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class StsPs3ValidationWindows {
    public delegate bool WindowCallback(IntPtr window, IntPtr argument);
    [DllImport("user32.dll")] static extern bool EnumWindows(WindowCallback callback, IntPtr argument);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window, int command);
    public static void Hide(uint processId) {
        EnumWindows((window, argument) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == processId) ShowWindow(window, 0);
            return true;
        }, IntPtr.Zero);
    }
}
'@
}

$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss-fff')
$runDirectory = Join-Path $repoRoot "runs\$stamp-stsps3"
[IO.Directory]::CreateDirectory($runDirectory) | Out-Null
[IO.Directory]::CreateDirectory($assetDirectory) | Out-Null
[IO.Directory]::CreateDirectory($temporaryDirectory) | Out-Null

$copied = 0
$unchanged = 0
$sourcePrefix = [IO.Path]::GetFullPath($sourceAssets).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
foreach ($asset in Get-ChildItem -LiteralPath $sourceAssets -Recurse -File) {
    $relative = $asset.FullName.Substring($sourcePrefix.Length)
    $destination = [IO.Path]::GetFullPath((Join-Path $assetDirectory $relative))
    $targetPrefix = [IO.Path]::GetFullPath($assetDirectory).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $destination.StartsWith($targetPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Asset destination escaped the title's assets directory: $destination"
    }
    if (Copy-IfChanged $asset $destination) { ++$copied } else { ++$unchanged }
}
foreach ($name in @('PARAM.SFO', 'ICON0.PNG')) {
    $packaged = Join-Path $repoRoot "build\ps3\pkg\$name"
    if (Test-Path -LiteralPath $packaged -PathType Leaf) {
        $null = Copy-IfChanged (Get-Item -LiteralPath $packaged) (Join-Path $gameDirectory $name)
    }
}
Write-Output "Assets staged: $copied copied, $unchanged unchanged."

# Preserve prior evidence before this game's main truncates its own log.
Copy-SharedFile $gameLog (Join-Path $runDirectory 'previous-stsps3.log')
if ($Capture) { Copy-SharedFile $capturePath (Join-Path $runDirectory 'previous-sts-menu.ppm') }
$hadCaptureFlag = Test-Path -LiteralPath $captureFlag -PathType Leaf
$previousCaptureFlag = if ($hadCaptureFlag) { [IO.File]::ReadAllBytes($captureFlag) } else { $null }
$hadPadScript = Test-Path -LiteralPath $padScriptPath -PathType Leaf
$previousPadScript = if ($hadPadScript) { [IO.File]::ReadAllBytes($padScriptPath) } else { $null }
$process = $null
$found = $false
$failure = ''
$processWasExited = $false
$bootStarted = [datetime]::UtcNow

try {
    if ($Capture) {
        [IO.File]::WriteAllText($captureFlag, "frame900`n", [Text.UTF8Encoding]::new($false))
    } elseif ($hadCaptureFlag) {
        # This title-specific sentinel must not request readback in a Null run.
        Remove-Item -LiteralPath $captureFlag
    }
    if ($PadScript) {
        $previousFrame = [uint64]0
        foreach ($eventLine in $PadScript) {
            if ($eventLine -notmatch '^\s*(\d+)\s+(?:0x)?([0-9a-fA-F]{1,8})\s*$') {
                throw "Invalid pad event '$eventLine'; expected '<pollFrame> <hex Vita mask>'."
            }
            $eventFrame = [uint64]$Matches[1]
            if ($eventFrame -lt $previousFrame) { throw 'Pad events must be ordered by pollFrame.' }
            $previousFrame = $eventFrame
        }
        [IO.File]::WriteAllText($padScriptPath, ($PadScript -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
        Copy-SharedFile $padScriptPath (Join-Path $runDirectory 'sts-pad-script.txt')
    } elseif ($hadPadScript) {
        Remove-Item -LiteralPath $padScriptPath
    }
    $configuration = Join-Path $runDirectory 'rpcs3.yml'
    $renderer = if ($Capture) { 'Vulkan' } else { 'Null' }
    $writeColor = if ($Capture) { 'true' } else { 'false' }
    $yaml = "Core:`n  PPU Decoder: $PpuDecoder`nVideo:`n  Renderer: $renderer`n  Write Color Buffers: $writeColor`n  Resolution: 1280x720`nAudio:`n  Renderer: Null`nMiscellaneous:`n  Start games in fullscreen mode: false`n"
    [IO.File]::WriteAllText($configuration, $yaml, [Text.UTF8Encoding]::new($false))
    $mode = if ($Capture) { '--no-gui' } else { '--headless' }
    $arguments = @($mode, '--config', ('"{0}"' -f $configuration), ('"{0}"' -f $elfPath))
    if (Get-Process -Name rpcs3 -ErrorAction SilentlyContinue) {
        throw 'RPCS3 was started by another session during asset staging; validation will not launch a second instance.'
    }
    $bootStarted = [datetime]::UtcNow
    $process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $rpcs3Root -WindowStyle Hidden -PassThru
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Write-Output "RPCS3 PID $($process.Id), renderer $renderer, deadline ${TimeoutSeconds}s."

    while ($watch.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $process.Refresh()
        if ($Capture -and -not $process.HasExited) { [StsPs3ValidationWindows]::Hide([uint32]$process.Id) }
        $text = if (Test-FreshFile $gameLog $bootStarted) { Read-SharedText $gameLog } else { '' }
        if ($Capture) {
            $found = $text -match '\[PS3 capture\].*sts-menu\.ppm.*: saved' -and
                     $text -match '\[PS3 frame\]\s+(?:900|[1-9]\d{3,})\b' -and
                     (Test-FreshFile $capturePath $bootStarted)
            if ($text -match '\[PS3 capture\].*: failed') { $failure = 'Game framebuffer capture failed.' }
        } else {
            $found = $text -match '\[PS3 frame\]\s+300\b|\[ps3(?: gfx)?\].*\bframe(?:s)?\s*[=:]\s*300\b'
        }
        if ($found -or $failure) { break }
        if ($text -match '(?i)uncaught exception|fatal error|segmentation fault|out of memory|assertion .* failed') {
            $failure = 'Game log reports a runtime failure.'
            break
        }
        foreach ($logPath in @((Join-Path $rpcs3Root 'log\RPCS3.log'), (Join-Path $rpcs3Root 'RPCS3.log'))) {
            # RPCS3 embeds a multiline YAML config dump in this file. Match only
            # severity-prefixed records, never settings such as 'Show fatal error hints'.
            if ((Test-FreshFile $logPath $bootStarted) -and
                (Read-SharedText $logPath) -match '(?im)^[·.]F[ \t]+[^\r\n]*|^[·.]E[ \t]+[^\r\n]*(?:access violation|unhandled[^\r\n]*exception|fatal error)') {
                $failure = 'RPCS3 log reports a fatal error or access violation.'
                break
            }
        }
        if ($failure) { break }
        if ($process.HasExited) {
            $failure = "RPCS3 exited before the expected marker (exit $($process.ExitCode))."
            break
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $found -and -not $failure) { $failure = "Timed out after ${TimeoutSeconds}s before the expected marker." }
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($null -ne $process) {
        $process.Refresh()
        $processWasExited = $process.HasExited
        if (-not $KeepRunning -and -not $process.HasExited) {
            # The Process object retains the handle for the instance this script started.
            $process.Kill()
            $null = $process.WaitForExit(10000)
        }
    }
    $leftRunning = $null -ne $process -and -not $process.HasExited
    if (-not $leftRunning) {
        if ($hadCaptureFlag) { [IO.File]::WriteAllBytes($captureFlag, $previousCaptureFlag) }
        elseif (Test-Path -LiteralPath $captureFlag) { Remove-Item -LiteralPath $captureFlag }
        if ($hadPadScript) { [IO.File]::WriteAllBytes($padScriptPath, $previousPadScript) }
        elseif (Test-Path -LiteralPath $padScriptPath) { Remove-Item -LiteralPath $padScriptPath }
    }
    foreach ($entry in @(
        @($gameLog, 'stsps3.log'),
        @((Join-Path $rpcs3Root 'log\TTY.log'), 'TTY.log'),
        @((Join-Path $rpcs3Root 'log\RPCS3.log'), 'RPCS3.log'),
        @((Join-Path $rpcs3Root 'RPCS3.log'), 'RPCS3-root.log')
    )) {
        Copy-SharedFile $entry[0] (Join-Path $runDirectory $entry[1])
    }
    if ($Capture -and (Test-FreshFile $capturePath $bootStarted)) {
        Copy-SharedFile $capturePath (Join-Path $runDirectory 'sts-menu.ppm')
    }
    $summary = [ordered]@{
        success = $found; capture = $Capture.IsPresent; failure = $failure; padEvents = @($PadScript).Count
        elf = $elfPath; processId = $(if ($null -ne $process) { $process.Id } else { $null })
        exitedBeforeCleanup = $processWasExited; leftRunning = $leftRunning
        bootUtc = $bootStarted.ToString('o'); assetsCopied = $copied; assetsUnchanged = $unchanged
    }
    $summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDirectory 'result.json') -Encoding UTF8
    Write-Output "Evidence: $runDirectory"
    if ($leftRunning) { Write-Output "RPCS3 PID $($process.Id) remains running; capture sentinel state is retained for that process." }
}

if ($found) {
    Write-Output $(if ($Capture) { 'PASS: Vulkan capture run reached frame 900 and saved the original game framebuffer.' } else { 'PASS: headless Null logic run reached original game frame 300.' })
    exit 0
}
Write-Output "FAIL: $failure"
exit 1
