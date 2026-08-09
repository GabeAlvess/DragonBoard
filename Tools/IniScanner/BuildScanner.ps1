param(
    [string]$Python = "",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$scannerRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $scannerRoot "dist"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$outputRoot = [System.IO.Path]::GetPathRoot($OutputDirectory)
if ($OutputDirectory.TrimEnd('\') -eq $outputRoot.TrimEnd('\')) {
    throw "Refusing to use a drive root as the scanner output directory: $OutputDirectory"
}

$buildRoot = Join-Path $scannerRoot ".build"
$distRoot = Join-Path $buildRoot "dist"
$bundleRoot = Join-Path $distRoot "DragonBoardIniScanner"

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
    --onedir `
    --noupx `
    --noconsole `
    --name DragonBoardIniScanner `
    --distpath $distRoot `
    --workpath (Join-Path $buildRoot "work") `
    --specpath $buildRoot `
    (Join-Path $scannerRoot "dragonboard_ini_scanner.py")

if ($LASTEXITCODE -ne 0) {
    throw "PyInstaller failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path -LiteralPath (Join-Path $bundleRoot "DragonBoardIniScanner.exe") -PathType Leaf)) {
    throw "PyInstaller did not create the expected onedir bundle: $bundleRoot"
}

$internalRoot = Join-Path $bundleRoot "_internal"
$baseLibraryArchive = Join-Path $internalRoot "base_library.zip"
if (Test-Path -LiteralPath $baseLibraryArchive -PathType Leaf) {
    Expand-Archive -LiteralPath $baseLibraryArchive -DestinationPath $internalRoot -Force
    Remove-Item -LiteralPath $baseLibraryArchive -Force
}

$nestedArchives = Get-ChildItem -LiteralPath $bundleRoot -File -Recurse | Where-Object {
    $_.Extension.ToLowerInvariant() -in @(".zip", ".7z", ".rar", ".tar", ".gz", ".bz2", ".xz")
}
if ($nestedArchives) {
    $archiveList = ($nestedArchives.FullName -join [Environment]::NewLine)
    throw "The scanner bundle still contains nested archives:`n$archiveList"
}

if (Test-Path -LiteralPath $OutputDirectory) {
    Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Copy-Item -Path (Join-Path $bundleRoot "*") -Destination $OutputDirectory -Recurse -Force

Write-Host "Built scanner bundle: $OutputDirectory"
Write-Host "Executable: $(Join-Path $OutputDirectory 'DragonBoardIniScanner.exe')"
