$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content -LiteralPath (Join-Path $root 'Src/ui/panels/PanelTransformUpdater.cpp') -Raw

foreach ($token in @(
    'kPersistentPanelName = "Persistent_Panel"',
    'kFixedWidgetsContainerName = "FixedWidgetsContainer"',
    'scale = settings.menuScale / physicalScale;',
    'float scale = 1.0f;',
    'ApplyFixedWidgetScaleCompensation(*panel, physicalAnchor != nullptr);'
)) {
    if (-not $source.Contains($token)) {
        throw "Missing fixed-widget board-scale contract token: $token"
    }
}

if ($source.Contains('panel->setLocalScale(settings.menuScale / physicalScale)')) {
    throw 'Scale compensation must remain restricted to FixedWidgetsContainer'
}

Write-Output 'fixed widget board-scale contract check: ok'