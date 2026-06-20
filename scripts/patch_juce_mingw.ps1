param([string]$SourceDir)
$file = Join-Path $SourceDir "modules\juce_core\system\juce_TargetPlatform.h"
$content = Get-Content $file -Raw
$content = $content -replace '#error "MinGW is not supported. Please use an alternative compiler."', '// #error patched: MinGW not officially supported, but trying anyway'
Set-Content $file -Value $content -NoNewline
Write-Output "JUCE MinGW check patched"
