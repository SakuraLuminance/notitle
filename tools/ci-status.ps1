<#
.SYNOPSIS
    AnaPlug CI status reader (works when GitHub's log/artifact downloads do not).

.DESCRIPTION
    GitHub's log and artifact downloads redirect to *.blob.core.windows.net,
    which is unreachable from this machine (DNS answers 198.18.x.x).  The
    workflow therefore publishes a 'ci-diagnostics' check run whose summary
    carries the compile errors, the Catch2 counts, pluginval's strictness level
    and every CRASH-OR-FAIL entry.  This script reads it back through the plain
    REST API, which does work.

.PARAMETER Sha
    Show the newest run for this commit (short or full sha).  Default: the
    newest run on main.

.PARAMETER Count
    How many recent runs to list.  Default 5.

.EXAMPLE
    pwsh -File tools/ci-status.ps1
    pwsh -File tools/ci-status.ps1 -Sha 578253a
#>
[CmdletBinding()]
param(
    [string] $Sha = '',
    [int]    $Count = 5,
    [string] $Repo = 'SakuraLuminance/notitle'
)

$ErrorActionPreference = 'Stop'

function Get-GitHubToken {
    # git reads its request on stdin and answers on stdout.  Piping the request
    # through PowerShell's own pipeline gets lost on some hosts (git then fails
    # with 'refusing to work with credential missing protocol field'), so hand
    # it over through temporary FILES instead: no pipes involved.
    $inFile  = Join-Path $env:TEMP 'anaplug-cred-in.txt'
    $outFile = Join-Path $env:TEMP 'anaplug-cred-out.txt'
    $errFile = Join-Path $env:TEMP 'anaplug-cred-err.txt'
    $request = 'protocol=https' + [char] 10 + 'host=github.com' + [char] 10

    try
    {
        Set-Content -Path $inFile -Value $request -NoNewline -Encoding ascii
        Start-Process -FilePath 'git' -ArgumentList @('credential', 'fill') -RedirectStandardInput $inFile -RedirectStandardOutput $outFile -RedirectStandardError $errFile -NoNewWindow -Wait | Out-Null

        $answer = Get-Content -Path $outFile -Raw -ErrorAction SilentlyContinue
        $match  = [regex]::Match([string] $answer, 'password=(\S+)')
        if (-not $match.Success)
        {
            throw 'git credential fill returned no password (is the PAT stored?)'
        }

        return $match.Groups[1].Value
    }
    finally
    {
        Remove-Item -Path $inFile, $outFile, $errFile -Force -ErrorAction SilentlyContinue
    }
}

$token   = Get-GitHubToken
$headers = @{
    Authorization = "token $token"
    Accept        = 'application/vnd.github+json'
    'User-Agent'  = 'anaplug-ci-status'
}
$api = "https://api.github.com/repos/$Repo"

$runs = (Invoke-RestMethod -Headers $headers -Uri "$api/actions/runs?branch=main&per_page=$Count").workflow_runs

Write-Host "=== recent runs on main ==="
foreach ($r in $runs) {
    '{0}  {1}  {2,-10} {3,-12} {4}' -f $r.id, $r.head_sha.Substring(0, 7), $r.status, $r.conclusion, $r.display_title
}

if ($Sha) {
    $run = $runs | Where-Object { $_.head_sha.StartsWith($Sha) } | Select-Object -First 1
    if (-not $run) { $run = (Invoke-RestMethod -Headers $headers -Uri "$api/actions/runs?per_page=20").workflow_runs | Where-Object { $_.head_sha.StartsWith($Sha) } | Select-Object -First 1 }
    if (-not $run) { Write-Warning "no run found for $Sha"; return }
} else {
    $run = $runs | Select-Object -First 1
}
if (-not $run) { Write-Warning 'no runs found'; return }

$full = $run.head_sha
Write-Host ""
Write-Host "=== run $($run.id) ($($full.Substring(0,7))) status=$($run.status) conclusion=$($run.conclusion) ==="

$jobs = (Invoke-RestMethod -Headers $headers -Uri "$api/actions/runs/$($run.id)/jobs").jobs
foreach ($job in $jobs) {
    Write-Host "job $($job.name): $($job.status) / $($job.conclusion)"
    foreach ($s in $job.steps) {
        if ($s.status -ne 'pending') { '   {0,-45} {1,-10} {2}' -f $s.name, $s.status, $s.conclusion }
    }
}

$checks = (Invoke-RestMethod -Headers $headers -Uri "$api/commits/$full/check-runs").check_runs
$diag = $checks | Where-Object { $_.name -eq 'ci-diagnostics' } | Select-Object -First 1
Write-Host ""
if ($diag) {
    Write-Host "=== ci-diagnostics check run ==="
    Write-Host $diag.output.summary
} else {
    Write-Host '=== ci-diagnostics check run: none (that permission path is refused) ==='
}

# Annotations are written by the runner itself ('::error title=..::message'),
# so they survive a token that may not create check runs.  ci-annotations.ps1
# owns that parsing: it reads the JSON as text, because deserialising it has
# produced objects with empty title/message and made this report 'no
# annotations' while annotations existed.
$annotationsScript = Join-Path $PSScriptRoot 'ci-annotations.ps1'
if (Test-Path $annotationsScript) {
    Write-Host ''
    & $annotationsScript -Sha $Sha -Repo $Repo
} else {
    Write-Warning "ci-annotations.ps1 not found next to this script"
}
