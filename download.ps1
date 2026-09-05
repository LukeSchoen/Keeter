$ErrorActionPreference = 'Stop'

$AppDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $AppDir
$BundleName = 'parakeet-v0.5.0-bin-win-cpu-x64'
$BundleDir = Join-Path $RootDir $BundleName
$ZipPath = Join-Path $RootDir "$BundleName.zip"
$CliPath = Join-Path $BundleDir 'parakeet-cli.exe'
$ModelDir = Join-Path $BundleDir 'models'
$ModelName = 'tdt-0.6b-v2-q8_0.gguf'
$ModelPath = Join-Path $ModelDir $ModelName

$BundleUrl = 'https://github.com/mudler/parakeet.cpp/releases/download/v0.5.0/parakeet-v0.5.0-bin-win-cpu-x64.zip'
$ModelRevision = 'bf0af9f425fa01809cadec671b3cb672709d13e9'
$ModelSha256 = '2027e2e1a4dc60ccdd8558f93b15e7c0db4ef8895b4e82e889f3a6275d8119c6'
$ModelUrl = "https://huggingface.co/mudler/parakeet-cpp-gguf/resolve/$ModelRevision/$ModelName`?download=true"

function Get-File {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$Sha256 = ''
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
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $partial
    if ($Sha256 -and (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $Sha256) {
        throw "Checksum mismatch for $Path"
    }

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
    Get-File -Url $ModelUrl -Path $ModelPath -Sha256 $ModelSha256
}

if (-not (Test-Path -LiteralPath $ModelPath)) {
    throw "Missing model after download: $ModelPath"
}

Write-Host "Parakeet assets are ready:"
Write-Host "  $CliPath"
Write-Host "  $ModelPath"
