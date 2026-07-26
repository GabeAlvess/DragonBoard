param(
    [string]$Source = (
        Join-Path $PSScriptRoot '..\Assets\meshes\DragonBoardVR\QuestMarker.nif'
    ),
    [string]$OutputDirectory = (
        Join-Path $PSScriptRoot '..\Assets\meshes\DragonBoardVR'
    )
)

$ErrorActionPreference = 'Stop'

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($outputPath) | Out-Null

$embeddedTexture = 'textures\DBQuestMark.dds'
$embeddedBytes = [System.Text.Encoding]::ASCII.GetBytes($embeddedTexture)
$sourceBytes = [System.IO.File]::ReadAllBytes($sourcePath)
$matches = [System.Collections.Generic.List[int]]::new()

for ($offset = 0; $offset -le $sourceBytes.Length - $embeddedBytes.Length; ++$offset) {
    $matchesAtOffset = $true
    for ($index = 0; $index -lt $embeddedBytes.Length; ++$index) {
        if ($sourceBytes[$offset + $index] -ne $embeddedBytes[$index]) {
            $matchesAtOffset = $false
            break
        }
    }
    if ($matchesAtOffset) {
        $matches.Add($offset)
    }
}

if ($matches.Count -ne 1) {
    throw "Expected one '$embeddedTexture' reference in '$sourcePath'; found $($matches.Count)."
}

$textureOffset = $matches[0]
if ($textureOffset -lt 4) {
    throw "Texture reference does not have a readable length field."
}

$storedLength = [System.BitConverter]::ToUInt32($sourceBytes, $textureOffset - 4)
if ($storedLength -ne $embeddedBytes.Length) {
    throw "Embedded texture length is $storedLength; expected $($embeddedBytes.Length)."
}

$variants = [ordered]@{
    'DBMarkerMain.nif' = 'textures\DBMarkerMain.dds'
    'DBMarkerSide.nif' = 'textures\DBMarkerSide.dds'
    'DBMarkerMisc.nif' = 'textures\DBMarkerMisc.dds'
}

foreach ($variant in $variants.GetEnumerator()) {
    $replacementBytes = [System.Text.Encoding]::ASCII.GetBytes($variant.Value)
    $delta = $replacementBytes.Length - $embeddedBytes.Length
    $variantBytes = [byte[]]::new($sourceBytes.Length + $delta)

    [System.Array]::Copy(
        $sourceBytes,
        0,
        $variantBytes,
        0,
        $textureOffset - 4
    )
    [System.Array]::Copy(
        [System.BitConverter]::GetBytes([uint32]$replacementBytes.Length),
        0,
        $variantBytes,
        $textureOffset - 4,
        4
    )
    [System.Array]::Copy(
        $replacementBytes,
        0,
        $variantBytes,
        $textureOffset,
        $replacementBytes.Length
    )

    $sourceTailOffset = $textureOffset + $embeddedBytes.Length
    $variantTailOffset = $textureOffset + $replacementBytes.Length
    [System.Array]::Copy(
        $sourceBytes,
        $sourceTailOffset,
        $variantBytes,
        $variantTailOffset,
        $sourceBytes.Length - $sourceTailOffset
    )

    $destination = Join-Path $outputPath $variant.Key
    [System.IO.File]::WriteAllBytes($destination, $variantBytes)
    Write-Host "Generated $destination -> $($variant.Value)"
}
