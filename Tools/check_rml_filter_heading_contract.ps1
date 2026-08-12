$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$inventoryRml = Get-Content (Join-Path $root 'Assets/ui/rml/inventory.rml') -Raw
$magicRml = Get-Content (Join-Path $root 'Assets/ui/rml/magic.rml') -Raw
$inventoryRcss = Get-Content (Join-Path $root 'Assets/ui/rml/inventory.rcss') -Raw
$magicRcss = Get-Content (Join-Path $root 'Assets/ui/rml/magic.rcss') -Raw
$source = Get-Content (Join-Path $root 'Src/ui/rml/DragonBoardRmlUi.cpp') -Raw

if ($inventoryRml -notmatch 'id="inventory-filter-heading"') {
    throw 'Inventory filter heading is missing.'
}
if ($magicRml -notmatch 'id="magic-filter-heading"') {
    throw 'Magic filter heading is missing.'
}

$geometry = @('left: 67px;', 'width: 873px;', 'height: 96px;', 'margin-right: 15px;', 'padding: 8px;', 'width: 72px;', 'height: 72px;')
foreach ($rule in $geometry) {
    if ($inventoryRcss -notmatch [regex]::Escape($rule) -or $magicRcss -notmatch [regex]::Escape($rule)) {
        throw "Filter geometry mismatch: $rule"
    }
}

foreach ($text in @('All', 'Quest Items', 'Miscellaneous', 'Passive Effects')) {
    if ($source -notmatch [regex]::Escape('"' + $text + '"')) {
        throw "Missing dynamic filter heading: $text"
    }
}

foreach ($rule in @('left: 980px;', 'top: 92px;', 'width: 876px;', 'height: 96px;')) {
    if ($inventoryRcss -notmatch [regex]::Escape($rule) -or $magicRcss -notmatch [regex]::Escape($rule)) {
        throw "Selected-name card geometry mismatch: $rule"
    }
}

Write-Host 'RML filter heading contract passed.'
