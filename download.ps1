$ErrorActionPreference = 'Stop'

$AppDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $AppDir
$BundleName = 'parakeet-v0.3.2-bin-win-cpu-x64'
$BundleDir = Join-Path $RootDir $BundleName
$ZipPath = Join-Path $RootDir "$BundleName.zip"
$CliPath = Join-Path $BundleDir 'parakeet-cli.exe'
$ModelDir = Join-Path $BundleDir 'models'
$ModelName = 'tdt-0.6b-v3-q8_0.gguf'
$ModelPath = Join-Path $ModelDir $ModelName

$BundleUrl = 'https://github.com/mudler/parakeet.cpp/releases/download/v0.3.2/parakeet-v0.3.2-bin-win-cpu-x64.zip'
$ModelUrl = "https://huggingface.co/mudler/parakeet-cpp-gguf/resolve/main/$ModelName`?download=true"

function Get-File {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }

    $partial = "$Path.partial"
    if (Test-Path -LiteralPath $partial) {
        Remove-Item -LiteralPath $partial -Force
    }

    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $partial

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Force
    }
    Move-Item -LiteralPath $partial -Destination $Path
}

if (-not (Test-Path -LiteralPath $CliPath)) {
    Get-File -Url $BundleUrl -Path $ZipPath
    Write-Host "Extracting $ZipPath"
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $RootDir -Force
}

if (-not (Test-Path -LiteralPath $CliPath)) {
    throw "Missing parakeet-cli.exe after extraction: $CliPath"
}

if (-not (Test-Path -LiteralPath $ModelPath)) {
    Get-File -Url $ModelUrl -Path $ModelPath
}

if (-not (Test-Path -LiteralPath $ModelPath)) {
    throw "Missing model after download: $ModelPath"
}

Write-Host "Parakeet assets are ready:"
Write-Host "  $CliPath"
Write-Host "  $ModelPath"
