# opencode-usage.ps1 - Track opencode usage statistics
param(
    [string]$LogPath = "$env:USERPROFILE\.local\share\opencode\log\opencode.log"
)

if (!(Test-Path $LogPath)) {
    Write-Host "Log file not found: $LogPath" -ForegroundColor Red
    exit 1
}

Write-Host "`n=== opencode Usage Statistics ===" -ForegroundColor Cyan

# Count API calls by model
Write-Host "`n[Model Usage]" -ForegroundColor Yellow
$streamLines = Select-String -Path $LogPath -Pattern "message=stream providerID=(\S+) modelID=(\S+)" -CaseSensitive:$false

$streamLines | ForEach-Object {
    if ($_.Line -match 'providerID=(\S+) modelID=(\S+)') {
        [PSCustomObject]@{
            Provider = $matches[1]
            Model = $matches[2]
        }
    }
} | Group-Object Provider, Model | ForEach-Object {
    Write-Host "  $($_.Name): $($_.Count) calls"
}

# Count Agent usage
Write-Host "`n[Agent Usage]" -ForegroundColor Yellow
$agentLines = Select-String -Path $LogPath -Pattern 'agent="([^"]+)"' -CaseSensitive:$false

$agentLines | ForEach-Object {
    if ($_.Line -match 'agent="([^"]+)"') {
        $matches[1]
    }
} | Group-Object | Sort-Object Count -Descending | ForEach-Object {
    Write-Host "  $($_.Name): $($_.Count) calls"
}

# Count sessions
Write-Host "`n[Sessions]" -ForegroundColor Yellow
$sessions = Select-String -Path $LogPath -Pattern 'session.id=(\S+)' -CaseSensitive:$false
$uniqueSessions = $sessions | ForEach-Object {
    if ($_.Line -match 'session.id=(\S+)') {
        $matches[1]
    }
} | Sort-Object -Unique

Write-Host "  Active sessions: $($uniqueSessions.Count)"

Write-Host "`n=================================="
Write-Host "Note: opencode logs API calls, not token counts." -ForegroundColor DarkGray
Write-Host "For exact tokens, check your API provider dashboard." -ForegroundColor DarkGray
