param(
    [string]$AppDir = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'

$startupDir = [Environment]::GetFolderPath('Startup')
$launcherPath = Join-Path $startupDir 'keeter-startup.vbs'

if (Test-Path -LiteralPath $launcherPath) {
    Remove-Item -LiteralPath $launcherPath -Force
    Write-Host "Removed startup launcher: $launcherPath"
} else {
    Write-Host "No startup launcher found at: $launcherPath"
}
