param(
    [Parameter(Mandatory = $true)]
    [string]$CompilerPath,

    [Parameter(Mandatory = $true)]
    [string]$GameSourcePath,

    [string]$FlagsPath = (Join-Path $PSScriptRoot 'Papyrus\TESV_Papyrus_Flags.flg'),

    [string]$OutputPath = (Join-Path $PSScriptRoot '..\Assets\scripts')
)

$ErrorActionPreference = 'Stop'

$compiler = (Resolve-Path -LiteralPath $CompilerPath).Path
$gameSources = (Resolve-Path -LiteralPath $GameSourcePath).Path
$flags = (Resolve-Path -LiteralPath $FlagsPath).Path
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$dragonBoardSources = Join-Path $projectRoot 'Src\papyrus'
$source = Join-Path $dragonBoardSources 'DragonBoardVR.psc'

New-Item -ItemType Directory -Force -Path $OutputPath | Out-Null
$output = (Resolve-Path -LiteralPath $OutputPath).Path
$imports = "$dragonBoardSources;$gameSources"

if ([System.IO.Path]::GetFileName($compiler) -ieq 'Caprica.exe') {
    & $compiler $source `
        --game skyrim `
        --import $dragonBoardSources `
        --import $gameSources `
        --flags $flags `
        --output $output `
        --release `
        --strict `
        --all-warnings-as-errors `
        --ignorecwd
} else {
    & $compiler $source "-i=$imports" "-o=$output" "-f=$flags"
}
if ($LASTEXITCODE -ne 0) {
    throw "Papyrus compiler failed with exit code $LASTEXITCODE."
}

$pex = Join-Path $output 'DragonBoardVR.pex'
if (-not (Test-Path -LiteralPath $pex)) {
    throw "Papyrus compiler completed without producing $pex."
}

Write-Host "Compiled: $pex"
