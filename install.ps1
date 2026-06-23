param(
    [string]$AppDir = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'

$exePath = Join-Path $AppDir 'keeter.exe'
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Missing keeter.exe at $exePath. Build the app first."
}

$startupDir = [Environment]::GetFolderPath('Startup')
$launcherPath = Join-Path $startupDir 'keeter-startup.vbs'

$vbs = @"
Set shell = CreateObject("WScript.Shell")
shell.Run Chr(34) & "$exePath" & Chr(34), 0, False
"@

Set-Content -LiteralPath $launcherPath -Value $vbs -Encoding ASCII
Write-Host "Installed startup launcher: $launcherPath"
