<#
.SYNOPSIS
    Prints the ci-diagnostics annotations the Windows runner attaches to a build.

.DESCRIPTION
    The only CI forensic channel this machine can reach: GitHub Actions logs and
    artifacts live on *.blob.core.windows.net / pipelines.azure.com, which resolve
    to 198.18.x.x here, so the job's own runner annotations (written by the
    'Publish CI diagnostics' step) are all we can read.

    Two traps are avoided on purpose:
    * the credential is fetched with Start-Process redirections (a PowerShell
      pipeline or cmd redirection loses git's stdin inside a script), and
    * the annotations JSON is parsed as TEXT with a regex - deserialising it
      through ConvertFrom-Json has silently yielded objects with empty
      title/message, which made this tool report 'no annotations' while
      annotations existed.

.PARAMETER Sha
    Any commit-ish on the repository (short sha, full sha, branch).

.PARAMETER Repo
    owner/name.  Defaults to SakuraLuminance/notitle.

.EXAMPLE
    pwsh -File tools/ci-annotations.ps1 -Sha bcde027
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Sha,
    [string] $Repo = 'SakuraLuminance/notitle'
)

$ErrorActionPreference = 'Stop'

# Backslash and quote as characters: this script never spells them inside a
# string literal, so no escaping layer can mangle it on the way to disk.
$BS = [char] 92
$CR = [char] 13
$LF = [char] 10
$DQ = [char] 34

function Get-GitHubToken {
    $inFile  = Join-Path $env:TEMP 'anaplug-cred-in.txt'
    $outFile = Join-Path $env:TEMP 'anaplug-cred-out.txt'
    $errFile = Join-Path $env:TEMP 'anaplug-cred-err.txt'
    $request = 'protocol=https' + $LF + 'host=github.com' + $LF

    try {
        Set-Content -Path $inFile -Value $request -NoNewline -Encoding ascii
        Start-Process -FilePath 'git' -ArgumentList @('credential', 'fill') -RedirectStandardInput $inFile -RedirectStandardOutput $outFile -RedirectStandardError $errFile -NoNewWindow -Wait | Out-Null

        $answer = [string] (Get-Content -Path $outFile -Raw -ErrorAction SilentlyContinue)
        $match  = [regex]::Match($answer, 'password=(.+)')
        if (-not $match.Success) { throw 'git credential fill returned no password (is the PAT stored?)' }
        return $match.Groups[1].Value.Trim()
    } finally {
        Remove-Item -Path $inFile, $outFile, $errFile -Force -ErrorAction SilentlyContinue
    }
}

function Convert-FromRunnerEscaping {
    param([string] $Text)
    # GitHub escapes annotation bodies: %0D -> CR, %0A -> LF, %25 -> '%'.
    return $Text.Replace('%0D', '').Replace('%0A', $LF).Replace('%25', '%')
}

function Convert-FromJsonString {
    param([string] $Text)
    $text = $Text.Replace($BS + 'r' + $BS + 'n', $LF)
    $text = $text.Replace($BS + 'n', $LF)
    $text = $text.Replace($BS + 'r', '')
    $text = $text.Replace($BS + $DQ, $DQ)
    $text = $text.Replace($BS + $BS, $BS)
    return $text
}

$headers = @{
    Authorization = "token $(Get-GitHubToken)"
    Accept        = 'application/vnd.github+json'
    'User-Agent'  = 'anaplug-ci-annotations'
}
$api = "https://api.github.com/repos/$Repo"

$full = (Invoke-RestMethod -Headers $headers -Uri "$api/commits/$Sha").sha
$checks = (Invoke-RestMethod -Headers $headers -Uri "$api/commits/$full/check-runs").check_runs
$job = @($checks) | Where-Object { $_.name -like 'build (*' } | Select-Object -First 1

if (-not $job) { throw "no build check run on $Sha yet" }

Write-Host ("=== $Sha  job: " + $job.name + '  ' + $job.status + '/' + $job.conclusion + ' ===')

$json = [string] (Invoke-WebRequest -Headers $headers -Uri "$api/check-runs/$($job.id)/annotations").Content
$pattern = '"title":"(?<title>[^"]*)","message":"(?<message>.*?)","raw_details"'
$found = 0

foreach ($m in [regex]::Matches($json, $pattern)) {
    $title = Convert-FromJsonString $m.Groups['title'].Value
    if ($title -notlike 'ci-diagnostics*') { continue }

    $found++
    Write-Host ''
    Write-Host ('--- ' + $title + ' ---')
    Write-Host (Convert-FromRunnerEscaping (Convert-FromJsonString $m.Groups['message'].Value))
}

if ($found -eq 0) {
    Write-Host ''
    Write-Host "no ci-diagnostics annotation on the job check run yet (status $($job.status))"
    exit 2
}
