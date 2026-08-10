$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$layoutSource = Get-Content -LiteralPath (Join-Path $root 'Src\vrui\VRUILayoutManager.cpp') -Raw
$pinSource = Get-Content -LiteralPath (Join-Path $root 'Src\vrui\VRUIItemEditPanel.cpp') -Raw

if ($layoutSource -notmatch 'WriteLayoutSnapshotAsync\(_filePath, root\.dump\(4\)\)') {
    throw 'Layout persistence is not dispatched to the background writer.'
}
if ($layoutSource -match 'std::ofstream file\(_filePath') {
    throw 'Layout persistence writes directly on the game thread.'
}
if ($layoutSource -notmatch 'layoutSaveGeneration' -or
    $layoutSource -notmatch 'MOVEFILE_REPLACE_EXISTING') {
    throw 'Background layout writes are not ordered and atomically replaced.'
}
if ($pinSource -notmatch 'reusing existing pin') {
    throw 'Existing inventory pins cannot be recovered through the Pin action.'
}

Write-Output 'Layout background-save contract passed.'
