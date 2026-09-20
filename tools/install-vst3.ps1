<#
.SYNOPSIS
    Installs a built AnaPlug.vst3 into the system VST3 folder (needs UAC).

.DESCRIPTION
    Roadmap item 5 (shipping install).  The CI artifact download is blocked from
    this machine (the blob host resolves to 198.18.x.x), so the human downloads
    'AnaPlug-windows-latest' from the green run in a browser and then runs this
    script - it finds that zip on its own:

        powershell -ExecutionPolicy Bypass -File tools\install-vst3.ps1

    It accepts a .zip, a .vst3 (file or bundle folder), a folder containing one,
    or nothing at all (newest *.zip in Downloads, else the newest bundle under
    artifacts/).  The script re-launches itself elevated, replaces the installed
    bundle and optionally clears REAPER's VST cache so the new build is rescanned.

.PARAMETER Source
    Path to a .zip, a .vst3, or a folder containing AnaPlug.vst3.

.PARAMETER ClearReaperCache
    Also delete REAPER's VST scan cache (reaper-vstplugins64.ini / .ini).

.PARAMETER Vst3Dir
    Install location.  Default: C:\Program Files\Common Files\VST3.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\install-vst3.ps1 -ClearReaperCache
#>
[CmdletBinding()]
param(
    [string] $Source = '',
    [switch] $ClearReaperCache,
    [string] $Vst3Dir = (Join-Path $env:ProgramFiles 'Common Files\VST3')
)

$ErrorActionPreference = 'Stop'

$script:extracted = $null

function Expand-ArtifactZip([string] $zipPath) {
    $dest = Join-Path $env:TEMP ('anaplug-vst3-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    Expand-Archive -LiteralPath $zipPath -DestinationPath $dest -Force
    $script:extracted = $dest
    Write-Host "extracted : $zipPath -> $dest"
    return $dest
}

# Returns the path of the AnaPlug.vst3 to install (a file, or a bundle folder).
function Find-Bundle([string] $root) {
    if ([string]::IsNullOrWhiteSpace($root) -or -not (Test-Path -LiteralPath $root)) { return $null }

    if (Test-Path -LiteralPath $root -PathType Leaf) {
        switch ([IO.Path]::GetExtension($root).ToLowerInvariant()) {
            '.vst3' { return (Get-Item -LiteralPath $root).FullName }
            '.zip'  { return (Find-Bundle (Expand-ArtifactZip $root)) }
            default { return $null }
        }
    }

    # A folder: either the bundle itself, or something that contains it.
    $direct = Join-Path $root 'AnaPlug.vst3'
    if (Test-Path -LiteralPath $direct) { return $direct }

    $found = Get-ChildItem -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq 'AnaPlug.vst3' } |
        Sort-Object { $_.FullName.Length } | Select-Object -First 1

    if ($found) { return $found.FullName }
    return $null
}

function Get-Binary([string] $bundle) {
    if (Test-Path -LiteralPath $bundle -PathType Leaf) { return (Get-Item -LiteralPath $bundle) }
    return (Get-ChildItem -LiteralPath $bundle -Recurse -ErrorAction SilentlyContinue |
        Where-Object { -not $_.PSIsContainer -and $_.Name -like 'AnaPlug*' } |
        Select-Object -First 1)
}

if ([string]::IsNullOrWhiteSpace($Source)) {
    $downloads = Join-Path $HOME 'Downloads'
    $newestZip = Get-ChildItem -Path $downloads -Filter 'AnaPlug*.zip' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($newestZip) {
        $Source = $newestZip.FullName
        Write-Host "using newest download: $Source"
    }
}

$bundle = Find-Bundle $Source

if (-not $bundle) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $local = Get-ChildItem -Path (Join-Path $repoRoot 'artifacts') -Recurse -Filter 'AnaPlug.vst3' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($local) {
        $bundle = $local.FullName
        Write-Host "using newest local bundle: $bundle"
    }
}

if (-not $bundle) {
    throw "No AnaPlug.vst3 found. Download the 'AnaPlug-windows-latest' artifact, then pass -Source <path to the zip>."
}

$binary = Get-Binary $bundle
Write-Host ("bundle    : " + $bundle)
Write-Host ("binary    : " + $(if ($binary) { "$($binary.FullName) ($([math]::Round($binary.Length / 1MB, 2)) MB, built $($binary.LastWriteTime))" } else { 'MISSING - the bundle looks broken' }))

if (-not $binary) { throw "$bundle contains no AnaPlug binary." }

$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

# Membership of Administrators is not what matters: what matters is whether this
# process may write the folder.  Asking for UAC when the folder is already
# writable turns an unattended "fetch and install" into a dialog nobody clicks.
if (-not $isAdmin -and (Test-Path -LiteralPath $Vst3Dir)) {
    try
    {
        $probe = Join-Path $Vst3Dir 'anaplug-writetest.tmp'
        Set-Content -Path $probe -Value 'x' -ErrorAction Stop
        Remove-Item -LiteralPath $probe -Force
        $isAdmin = $true
        Write-Host 'vst3 folder is writable - no elevation needed'
    }
    catch { }
}

if (-not $isAdmin) {
    Write-Host 'not elevated - re-launching with UAC...'
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"",
                 '-Source', "`"$bundle`"", '-Vst3Dir', "`"$Vst3Dir`"")
    if ($ClearReaperCache) { $argList += '-ClearReaperCache' }
    Start-Process -FilePath (Get-Process -Id $PID).Path -Verb RunAs -ArgumentList $argList
    return
}

$target = Join-Path $Vst3Dir 'AnaPlug.vst3'
Write-Host "installing: $target"

# A host that has the plugin loaded keeps a lock on the binary; fail loudly
# instead of leaving a half-copied bundle behind.
try {
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    Copy-Item -LiteralPath $bundle -Destination $target -Recurse -Force
} catch {
    throw "copy failed ($($_.Exception.Message)). Close REAPER / any host that loaded AnaPlug and retry."
}

$installed = Get-Binary $target
if (-not $installed) { throw "copy finished but $target contains no AnaPlug binary." }
Write-Host ("installed : " + $installed.FullName + "  " + $installed.LastWriteTime)

if ($ClearReaperCache) {
    foreach ($name in 'reaper-vstplugins64.ini', 'reaper-vstplugins.ini') {
        $cache = Join-Path $env:APPDATA "REAPER\$name"
        if (Test-Path -LiteralPath $cache) {
            Remove-Item -LiteralPath $cache -Force
            Write-Host "cleared   : $cache"
        }
    }
}

if ($script:extracted) { Write-Host "temp copy : $script:extracted (safe to delete)" }
Write-Host 'done - restart the host so it rescans the plugin.'
