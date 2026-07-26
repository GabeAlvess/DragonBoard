param(
    [string]$Python = "",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$scannerRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $scannerRoot "dist"
}

$pyInstaller = Get-Command "pyinstaller" -ErrorAction SilentlyContinue
if ($Python) {
    $command = $Python
    $commandArguments = @("-m", "PyInstaller")
} elseif ($pyInstaller) {
    $command = $pyInstaller.Source
    $commandArguments = @()
} else {
    throw "PyInstaller was not found. Install it or pass -Python with a Python executable that provides PyInstaller."
}

& $command @commandArguments `
    --noconfirm `
    --clean `
    --onefile `
    --noconsole `
    --name DragonBoardIniScanner `
    --distpath $OutputDirectory `
    --workpath (Join-Path $scannerRoot ".build") `
    --specpath (Join-Path $scannerRoot ".build") `
    (Join-Path $scannerRoot "dragonboard_ini_scanner.py")

if ($LASTEXITCODE -ne 0) {
    throw "PyInstaller failed with exit code $LASTEXITCODE."
}

Write-Host "Built scanner: $(Join-Path $OutputDirectory 'DragonBoardIniScanner.exe')"
