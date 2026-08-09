$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$rmlFiles = @(
    Join-Path $root 'Assets/ui/rml/inventory.rml'
    Join-Path $root 'Assets/ui/rml/inventory_vr_improved.rml'
)

foreach ($rmlFile in $rmlFiles) {
    $rml = Get-Content -LiteralPath $rmlFile -Raw
    $potions = $rml.IndexOf('id="inventory-filter-potions"')
    $food = $rml.IndexOf('id="inventory-filter-food"')
    if ($potions -lt 0 -or $food -lt 0 -or $food -le $potions) {
        throw "Food filter must appear directly after Potions in $rmlFile"
    }
    if (-not $rml.Contains('assets/Icons/foodincon.png')) {
        throw "Food icon missing from $rmlFile"
    }
    if ($rml.Contains('inventory-filter-consumables')) {
        throw "Legacy consumables filter remains in $rmlFile"
    }
}

$ui = Get-Content -LiteralPath (Join-Path $root 'Src/ui/rml/DragonBoardRmlUi.cpp') -Raw
$hostSource = Get-Content -LiteralPath (Join-Path $root 'Src/ui/rml/RmlPanelHost.cpp') -Raw
$inventory = Get-Content -LiteralPath (Join-Path $root 'Src/vrui/VRUIInventoryContainer.cpp') -Raw
foreach ($token in @('inventory-filter-potions', 'inventory-filter-food', 'kFilterPotions', 'kFilterFood')) {
    if (-not $ui.Contains($token)) { throw "Missing UI routing token: $token" }
}
foreach ($token in @('Filter::Potions', 'Filter::Food', 'kFilterPotions', 'kFilterFood')) {
    if (-not $hostSource.Contains($token)) { throw "Missing host routing token: $token" }
}
foreach ($token in @('case FM::Potions: return !alch->IsPoison() && !alch->IsFood();', 'case FM::Food:    return !alch->IsPoison() &&  alch->IsFood();')) {
    if (-not $inventory.Contains($token)) { throw "Missing separated inventory classification: $token" }
}
if (-not (Test-Path -LiteralPath (Join-Path $root 'Assets/ui/rml/assets/Icons/foodincon.png'))) {
    throw 'foodincon.png is missing'
}

Write-Output 'inventory food filter contract check: ok'