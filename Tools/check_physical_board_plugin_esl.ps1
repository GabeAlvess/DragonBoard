$ErrorActionPreference = 'Stop'

$plugin = Join-Path $PSScriptRoot '..\Assets\DragonBoardVR.esp'
$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $plugin))
if ($bytes.Length -lt 24 -or
    [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'TES4') {
    throw 'DragonBoardVR.esp has an invalid TES4 header'
}

$flags = [BitConverter]::ToUInt32($bytes, 8)
if (($flags -band 0x00000200) -eq 0) {
    throw ('DragonBoardVR.esp is not ESL flagged: 0x{0:X8}' -f $flags)
}

$expectedRecords = @{
    'MISC' = 0x00000800
    'WEAP' = 0x00000801
    'STAT' = 0x00000802
}
$foundRecords = @{}
$containerOverrideCount = 0
$leveledListOverrideCount = 0

function Read-Records([int]$start, [int]$end) {
    $offset = $start
    while ($offset + 24 -le $end) {
        $signature = [System.Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $size = [BitConverter]::ToUInt32($bytes, $offset + 4)
        if ($signature -eq 'GRUP') {
            if ($size -lt 24 -or $offset + $size -gt $end) {
                throw "DragonBoardVR.esp has an invalid GRUP at offset $offset"
            }
            Read-Records ($offset + 24) ($offset + $size)
            $offset += $size
            continue
        }

        $nextOffset = $offset + 24 + $size
        if ($nextOffset -gt $end) {
            throw "DragonBoardVR.esp has an invalid $signature record at offset $offset"
        }
        $localFormId =
            [BitConverter]::ToUInt32($bytes, $offset + 12) -band 0x00FFFFFF
        if ($expectedRecords.ContainsKey($signature)) {
            $foundRecords[$signature] = $localFormId
        }
        if ($signature -eq 'CONT') {
            $script:containerOverrideCount++
        }
        if ($signature -eq 'LVLI') {
            $script:leveledListOverrideCount++
        }
        $offset = $nextOffset
    }

    if ($offset -ne $end) {
        throw "DragonBoardVR.esp has trailing bytes in a record group"
    }
}

$tes4Size = [BitConverter]::ToUInt32($bytes, 4)
Read-Records (24 + $tes4Size) $bytes.Length

foreach ($record in $expectedRecords.GetEnumerator()) {
    if ($foundRecords[$record.Key] -ne $record.Value) {
        throw ('{0} local FormID is 0x{1:X6}; expected 0x{2:X6}' -f
            $record.Key, $foundRecords[$record.Key], $record.Value)
    }
}

if ($containerOverrideCount -ne 0) {
    throw "DragonBoardVR.esp contains $containerOverrideCount merchant container override(s)"
}
if ($leveledListOverrideCount -ne 0) {
    throw "DragonBoardVR.esp contains $leveledListOverrideCount LVLI override(s); expected 0"
}

$skyPatcherConfig = Join-Path $PSScriptRoot `
    '..\Assets\integrations\skypatcher\SKSE\Plugins\SkyPatcher\leveledList\DragonBoardVR\DragonBoardVR.esp.ini'
$skyPatcherRule = @(Get-Content -LiteralPath $skyPatcherConfig |
    Where-Object { $_ -and -not $_.TrimStart().StartsWith(';') })
if ($skyPatcherRule.Count -ne 1 -or
    -not $skyPatcherRule[0].StartsWith('filterByLLs=Skyrim.esm|0009AF0A:addToLLs=')) {
    throw 'DragonBoardVR SkyPatcher leveled-list rule is missing or targets the wrong list'
}
$skyPatcherEntryCount =
    ([regex]::Matches($skyPatcherRule[0], 'DragonBoardVRPhysicalBoard~1~1~0')).Count
if ($skyPatcherEntryCount -ne 10) {
    throw "DragonBoardVR SkyPatcher rule contains $skyPatcherEntryCount weighted entries; expected 10"
}
Write-Output 'physical board plugin ESL contract: ok'
