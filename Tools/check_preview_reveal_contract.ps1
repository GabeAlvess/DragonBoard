$ErrorActionPreference = 'Stop'

$header = Get-Content -LiteralPath "$PSScriptRoot/../Src/vrui/VRUIItemEditPanel.h" -Raw
$preview = Get-Content -LiteralPath "$PSScriptRoot/../Src/vrui/VRUIItemEditPanel.cpp" -Raw
$hostSource = Get-Content -LiteralPath "$PSScriptRoot/../Src/ui/rml/RmlPanelHost.cpp" -Raw

foreach ($required in @(
    'void update(float deltaTime) override;',
    'int _previewRevealFrames = 0;'
)) {
    if (-not $header.Contains($required)) {
        throw "Missing preview reveal header contract: $required"
    }
}

foreach ($required in @(
    '_previewRevealFrames > 0 ?',
    '_previewRevealFrames = 1;',
    '_previewRootTransformConfigured = false;',
    '_previewWidget->setLocalScale(rootScale);'
)) {
    if (-not $preview.Contains($required)) {
        throw "Missing preview reveal source contract: $required"
    }
}

foreach ($required in @(
    'const auto previousEntry = _inventoryPresenter.SelectedEntry();',
    'previousEntry->formID != selectedEntry->formID',
    'const auto previousSelected = _magicPresenter.SelectedEntry();',
    'previousSelected->formID != selected->formID'
)) {
    if (-not $hostSource.Contains($required)) {
        throw "Missing repeated-selection contract: $required"
    }
}

if ($preview.Contains('PreviewLightProbe')) {
    throw 'Diagnostic light probe must be removed from release candidate.'
}

Write-Output 'Preview reveal contract OK'
