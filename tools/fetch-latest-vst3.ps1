<#
.SYNOPSIS
    Downloads the newest compiled AnaPlug and puts it in the VST3 folder.

.DESCRIPTION
    One command, no browser:

        powershell -ExecutionPolicy Bypass -File tools\fetch-latest-vst3.ps1

    CI republishes every successful build's VST3 bundle onto the 'ci-artifacts'
    branch (see the "Publish VST3 bundle for local testing" step), because the
    artifact download itself is served from a blob host that cannot be reached
    from every network.  This script reads that branch back:

        1. build.json              -> which commit / run / size the bundle is
        2. AnaPlug-vst3.zip        -> the bundle, verified as a zip and hashed
        3. tools\install-vst3.ps1  -> replaces <Vst3Dir>\AnaPlug.vst3

    It compares the published commit with the newest *green* run on main and says
    so when the bundle is older (a run that failed to build keeps the previous
    bundle on purpose - a broken build must not become the thing you test).

.PARAMETER Repo
    owner/name.  Default: SakuraLuminance/notitle.

.PARAMETER Branch
    Branch CI publishes the bundle to.  Default: ci-artifacts.

.PARAMETER Vst3Dir
    Install location.  Default: C:\Program Files\Common Files\VST3.

.PARAMETER WorkDir
    Where the zip and its extraction live.  Default: %TEMP%\anaplug-vst3.

.PARAMETER NoInstall
    Download and verify only; do not touch the VST3 folder.

.PARAMETER Force
    Install even when the same bundle (same size and timestamp) is already there.

.PARAMETER ClearReaperCache
    Also delete REAPER's VST scan cache so the new build is rescanned.

.PARAMETER ArtifactApi
    Try the run artifact (actions/artifacts/.../zip) first instead of the branch
    copy.  On most networks that is the shortest path; here it is the one that
    fails, so it is not the default.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\fetch-latest-vst3.ps1 -ClearReaperCache

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\fetch-latest-vst3.ps1 -NoInstall
#>
[CmdletBinding()]
param(
    [string] $Repo       = 'SakuraLuminance/notitle',
    [string] $Branch     = 'ci-artifacts',
    [string] $Vst3Dir    = (Join-Path $env:ProgramFiles 'Common Files\VST3'),
    [string] $WorkDir    = (Join-Path $env:TEMP 'anaplug-vst3'),
    [string] $ArtifactName = 'AnaPlug-windows-latest',
    [switch] $NoInstall,
    [switch] $Force,
    [switch] $ClearReaperCache,
    [switch] $ArtifactApi
)

$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

function Get-GitHubToken {
    # git answers on stdout, and piping the request through PowerShell's own
    # pipeline loses it on some hosts, so hand it over through temporary files.
    $inFile  = Join-Path $env:TEMP 'anaplug-cred-in.txt'
    $outFile = Join-Path $env:TEMP 'anaplug-cred-out.txt'
    $errFile = Join-Path $env:TEMP 'anaplug-cred-err.txt'
    try
    {
        Set-Content -Path $inFile -Value ('protocol=https' + [char] 10 + 'host=github.com' + [char] 10) -NoNewline -Encoding ascii
        Start-Process -FilePath 'git' -ArgumentList @('credential', 'fill') -RedirectStandardInput $inFile -RedirectStandardOutput $outFile -RedirectStandardError $errFile -NoNewWindow -Wait | Out-Null
        $answer = Get-Content -Path $outFile -Raw -ErrorAction SilentlyContinue
        $match  = [regex]::Match([string] $answer, 'password=(\S+)')
        if ($match.Success) { return $match.Groups[1].Value }
    }
    catch { }
    finally { Remove-Item -Path $inFile, $outFile, $errFile -Force -ErrorAction SilentlyContinue }
    return ''
}

$token = Get-GitHubToken
$api   = "https://api.github.com/repos/$Repo"

$headers = @{ Accept = 'application/vnd.github+json'; 'User-Agent' = 'anaplug-fetch-vst3' }
if ($token) { $headers.Authorization = "token $token" }

$rawHeaders = @{ Accept = 'application/vnd.github.raw'; 'User-Agent' = 'anaplug-fetch-vst3' }
if ($token) { $rawHeaders.Authorization = "token $token" }

function Get-Json([string] $uri) {
    return Invoke-RestMethod -Headers $headers -Uri $uri -TimeoutSec 60
}

function Get-PublishedMeta {
    # The contents API is used rather than raw.githubusercontent.com because raw
    # is served through a CDN cache: it can hand back the previous build's json
    # for a few minutes after a publish, and the sha is the whole point of it.
    try
    {
        $body = Invoke-RestMethod -Headers $rawHeaders -Uri "$api/contents/build.json?ref=$Branch" -TimeoutSec 60
        return ($body | ConvertFrom-Json)
    }
    catch
    {
        throw "no build.json on branch '$Branch' yet - push a commit and let CI finish one run ($($_.Exception.Message))"
    }
}

function Get-LatestGreenRun {
    if (-not $token) { return $null }
    try
    {
        $runs = (Get-Json "$api/actions/runs?branch=main&status=success&per_page=1").workflow_runs
        if ($runs -and $runs.Count -gt 0) { return $runs[0] }
    }
    catch { }
    return $null
}

function Save-BranchFile([string] $path, [string] $outFile) {
    $entry = Get-Json "$api/contents/$path" + "?ref=$Branch"
    if (-not $entry.sha) { throw "branch '$Branch' has no $path" }
    Invoke-WebRequest -Headers $rawHeaders -Uri "$api/git/blobs/$($entry.sha)" -OutFile $outFile -TimeoutSec 600
    return $outFile
}

function Save-Artifact([string] $outFile) {
    $runs = (Get-Json "$api/actions/runs?branch=main&status=success&per_page=5").workflow_runs
    foreach ($run in $runs)
    {
        $art = (Get-Json "$api/actions/runs/$($run.id)/artifacts").artifacts |
               Where-Object { $_.name -eq $ArtifactName -and -not $_.expired } |
               Select-Object -First 1
        if (-not $art) { continue }

        $url = "$api/actions/artifacts/$($art.id)/zip"
        if (Get-Command curl.exe -ErrorAction SilentlyContinue)
        {
            & curl.exe -sSL --max-time 600 -o $outFile -H "Authorization: Bearer $token" $url
            if ($LASTEXITCODE -ne 0) { throw "curl exit $LASTEXITCODE (the artifact host is unreachable from here)" }
        }
        else
        {
            Invoke-WebRequest -Uri $url -Headers @{ Authorization = "Bearer $token" } -OutFile $outFile -TimeoutSec 600
        }

        return [pscustomobject]@{ sha = $run.head_sha; run = $run.run_number; bytes = (Get-Item $outFile).Length }
    }

    throw "no unexpired '$ArtifactName' artifact in the last five green runs"
}

function Assert-Zip([string] $path) {
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 4 -or $bytes[0] -ne 0x50 -or $bytes[1] -ne 0x4B)
    {
        $head = [Text.Encoding]::UTF8.GetString($bytes[0..([Math]::Min(160, $bytes.Length - 1))])
        throw "downloaded file is not a zip: $head"
    }
    return $bytes.Length
}

function Find-Bundle([string] $root) {
    $found = Get-ChildItem -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue |
             Where-Object { $_.Name -eq 'AnaPlug.vst3' } |
             Sort-Object { $_.FullName.Length } | Select-Object -First 1
    if ($found) { return $found.FullName }
    return $null
}

# ---------------------------------------------------------------------------
Write-Host "repo       : $Repo"
Write-Host ("token      : " + $(if ($token) { 'yes (private API calls enabled)' } else { 'none - public reads only' }))

$meta = Get-PublishedMeta
$green = Get-LatestGreenRun

$shortSha = $meta.sha.Substring(0, 7)
Write-Host "published  : run $($meta.run)  $shortSha  $($meta.date)  $([math]::Round($meta.bytes / 1MB, 2)) MB"

if ($green)
{
    $greenSha = $green.head_sha.Substring(0, 7)
    if ($green.head_sha -ne $meta.sha)
    {
        Write-Host "newest green: run $($green.run_number)  $greenSha  - NEWER than the published bundle"
        Write-Host "              (a run that fails to build keeps the previous bundle on purpose)"
    }
    else
    {
        Write-Host "newest green: run $($green.run_number)  $greenSha  - in sync"
    }
}

if (-not (Test-Path -LiteralPath $WorkDir)) { New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null }
$zip = Join-Path $WorkDir 'AnaPlug-vst3.zip'
Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue

$usedArtifact = $false
if ($ArtifactApi)
{
    try
    {
        $got = Save-Artifact $zip
        $usedArtifact = $true
        Write-Host "downloaded : $([math]::Round($got.bytes / 1MB, 2)) MB from run $($got.run) $($got.sha.Substring(0, 7)) (artifact)"
    }
    catch
    {
        Write-Host "artifact   : $($_.Exception.Message)"
        Write-Host "artifact   : falling back to the published branch"
    }
}

if (-not $usedArtifact)
{
    Save-BranchFile 'AnaPlug-vst3.zip' $zip | Out-Null
}

$size = Assert-Zip $zip
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
Write-Host "downloaded : $([math]::Round($size / 1MB, 2)) MB  sha256 $($hash.Substring(0, 16))..."
if (-not $usedArtifact -and $meta.bytes -and $size -ne $meta.bytes)
{
    throw "size mismatch: branch says $($meta.bytes) bytes, the download is $size"
}

$extract = Join-Path $WorkDir 'bundle'
Remove-Item -LiteralPath $extract -Recurse -Force -ErrorAction SilentlyContinue
Expand-Archive -LiteralPath $zip -DestinationPath $extract -Force

$staged = Find-Bundle $extract
if (-not $staged) { throw "$zip contains no AnaPlug.vst3" }

$stagedBinary = Get-ChildItem -LiteralPath $staged -Recurse -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -like 'AnaPlug*' } | Select-Object -First 1
if (-not $stagedBinary) { throw "$staged contains no AnaPlug binary" }
Write-Host ("staged     : " + $stagedBinary.FullName.Replace($extract, '').TrimStart('\') + "  " + [math]::Round($stagedBinary.Length / 1MB, 2) + " MB")

if ($NoInstall)
{
    Write-Host "no install : -NoInstall was given; the bundle is at $staged"
    return
}

$target     = Join-Path $Vst3Dir 'AnaPlug.vst3'
$targetBin  = Join-Path $target 'Contents\x86_64-win\AnaPlug.vst3'
if ((Test-Path -LiteralPath $targetBin) -and -not $Force)
{
    $old = Get-Item -LiteralPath $targetBin
    if ($old.Length -eq $stagedBinary.Length -and [math]::Round(($old.LastWriteTime - $stagedBinary.LastWriteTime).TotalSeconds) -eq 0)
    {
        Write-Host "installed  : already the same build ($($old.Length) bytes) - use -Force to reinstall"
        return
    }
    Write-Host "previous   : $([math]::Round($old.Length / 1MB, 2)) MB  $($old.LastWriteTime)"
}

& (Join-Path $PSScriptRoot 'install-vst3.ps1') -Source $staged -Vst3Dir $Vst3Dir -ClearReaperCache:$ClearReaperCache

if (Test-Path -LiteralPath $targetBin)
{
    $new = Get-Item -LiteralPath $targetBin
    Write-Host "installed  : $targetBin"
    Write-Host "             $([math]::Round($new.Length / 1MB, 2)) MB  $($new.LastWriteTime)  (from $shortSha)"
    Write-Host ""
    Write-Host "done - restart the host (or rescan) and it will load this build."
}
