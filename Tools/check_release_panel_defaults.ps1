$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$layout = Get-Content (Join-Path $root 'Assets/config/DragonBoardVR_Layout.ini') -Raw
$layoutJson = Get-Content (Join-Path $root 'Assets/config/DragonBoardVR_Layout.json') -Raw | ConvertFrom-Json
$settingsHeader = Get-Content (Join-Path $root 'Src/vrui/VRUISettings.h') -Raw
$panelHeader = Get-Content (Join-Path $root 'Src/ui/rml/RmlPanelHost.h') -Raw

if ($layout -notmatch '(?m)^bShowDevButton = false$') {
    throw 'Dev panel must be disabled in the default layout.'
}
if ($layout -notmatch '(?m)^fHomeRotX = 0\.000000$') {
    throw 'Home button X rotation must default to zero.'
}
if ($layout -notmatch '(?m)^fHomePosY = 0\.000000$') {
    throw 'Home button Y position must default to zero.'
}
$homeButton = @($layoutJson.containers.elements | Where-Object id -eq 'Btn_Home')
if ($homeButton.Count -ne 1 -or $homeButton[0].transform.rotation.x -ne 0) {
    throw 'Home button JSON rotation must default to zero.'
}
if ($homeButton[0].transform.position.y -ne 0) {
    throw 'Home button JSON Y position must default to zero.'
}
$homeMatrix = $homeButton[0].transform.matrix
$identity = @(@(1, 0, 0), @(0, 1, 0), @(0, 0, 1))
for ($row = 0; $row -lt 3; $row++) {
    for ($column = 0; $column -lt 3; $column++) {
        if ([double]$homeMatrix[$row][$column] -ne $identity[$row][$column]) {
            throw 'Home button JSON matrix must be identity.'
        }
    }
}
if ($layout -notmatch '(?ms)^\[StatusWidget\].*?^bVisible = false$') {
    throw 'Status widget must be disabled in the default layout.'
}
if ($settingsHeader -notmatch 'bool\s+showDevButton = false;' -or
    $settingsHeader -notmatch 'bool statusWidgetVisible = false;' -or
    $panelHeader -notmatch 'bool statusWidgetVisible = false;') {
    throw 'C++ fallback defaults must keep Dev and Status disabled.'
}

Write-Host 'Release panel defaults passed.'
