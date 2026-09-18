<#
.SYNOPSIS
    Installs a built AnaPlug.vst3 into the system VST3 folder (needs UAC).

.DESCRIPTION
    Roadmap item 5 (shipping install).  The CI artifact download is blocked from
    the dev machine, so download 'AnaPlug-windows-latest' from the green run in a
    browser, unzip it, and point this script at the folder that contains
    AnaPlug.vst3 (usually ...\AnaPlug_artefacts\Release\VST3\AnaPlug.vst3).

    The script re-launches itself elevated, copies the bundle over the installed
    one, and optionally clears REAPER's VST cache so the new build is rescanned.

.PARAMETER Source
    Path to AnaPlug.vst3 (or to a folder containing it).  Default: the newest
    artifacts/<run>/AnaPlug.vst3 in this checkout.

.PARAMETER ClearReaperCache
    Also delete REAPER's VST scan cache (reaper-vstplugins64.ini / .ini).

.EXAMPLE
    pwsh -File tools/install-vst3.ps1 -Source "$HOME\Downloads\AnaPlug-windows-latest\AnaPlug.vst3" -ClearReaperCache
#>
[CmdletBinding()]
param(
    [string] $Source = '',
    [switch] $ClearReaperCache,
    [string] $Vst3Dir = (Join-Path $env:ProgramFiles 'Common Files\VST3')
)

$ErrorActionPreference = 'Stop'

function Resolve-Bundle([string] $path) {
    if ([string]::IsNullOrWhiteSpace($path)) { return $null }
    if (Test-Path -LiteralPath $path -PathType Container) {
        $inner = Join-Path $path 'AnaPlug.vst3'
        if (Test-Path -LiteralPath $inner) { return $inner }
        return $null
    }
    return $path
}

if (-not (Resolve-Bundle $Source)) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $candidate = Get-ChildItem -Path (Join-Path $repoRoot 'artifacts') -Recurse -Filter 'AnaPlug.vst3' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $candidate) {
        throw "No VST3 bundle found. Pass -Source <path to AnaPlug.vst3>."
    }
    $Source = $candidate.FullName
    Write-Host "using newest local bundle: $Source"
} else {
    $Source = (Resolve-Bundle $Source)
}

if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
    throw "$Source is not a VST3 bundle folder."
}

$dll = Get-ChildItem -LiteralPath $Source -Recurse -Filter 'AnaPlug.vst3' -ErrorAction SilentlyContinue |
    Where-Object { -not $_.PSIsContainer } | Select-Object -First 1
Write-Host ("bundle : " + $Source)
Write-Host ("binary : " + $(if ($dll) { "$($dll.FullName) ($([math]::Round($dll.Length / 1MB, 2)) MB)" } else { 'MISSING - the bundle looks broken' }))

$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host 'not elevated - re-launching with UAC...'
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Source', "`"$Source`"")
    if ($ClearReaperCache) { $argList += '-ClearReaperCache' }
    Start-Process -FilePath (Get-Process -Id $PID).Path -Verb RunAs -ArgumentList $argList
    return
}

$target = Join-Path $Vst3Dir 'AnaPlug.vst3'
Write-Host "installing -> $target"

# A host that has the plugin loaded keeps a lock on the DLL; fail loudly instead
# of leaving a half-copied bundle behind.
try {
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    Copy-Item -LiteralPath $Source -Destination $target -Recurse -Force
} catch {
    throw "copy failed ($($_.Exception.Message)). Close REAPER / any host that loaded AnaPlug and retry."
}

$installed = Get-ChildItem -LiteralPath $target -Recurse -Filter 'AnaPlug.vst3' -ErrorAction SilentlyContinue |
    Where-Object { -not $_.PSIsContainer } | Select-Object -First 1
if (-not $installed) { throw "copy finished but $target contains no AnaPlug.vst3 binary." }
Write-Host ("installed: " + $installed.FullName + "  " + $installed.LastWriteTime)

if ($ClearReaperCache) {
    foreach ($name in 'reaper-vstplugins64.ini', 'reaper-vstplugins.ini') {
        $cache = Join-Path $env:APPDATA "REAPER\$name"
        if (Test-Path -LiteralPath $cache) {
            Remove-Item -LiteralPath $cache -Force
            Write-Host "cleared $cache"
        }
    }
}

Write-Host 'done - restart the host so it rescans the plugin.'
