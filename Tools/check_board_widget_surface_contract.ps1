$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content -LiteralPath (Join-Path $root 'Src/ui/rml/RmlPanelHost.cpp') -Raw
$header = Get-Content -LiteralPath (Join-Path $root 'Src/ui/rml/RmlPanelHost.h') -Raw
$rmlUiSource = Get-Content -LiteralPath (Join-Path $root 'Src/ui/rml/DragonBoardRmlUi.cpp') -Raw
$rmlUiHeader = Get-Content -LiteralPath (Join-Path $root 'Src/ui/rml/DragonBoardRmlUi.h') -Raw
$settingsSource = Get-Content -LiteralPath (Join-Path $root 'Src/ui/rml/BuiltinSettingsPanel.cpp') -Raw
$settingsRml = Get-Content -LiteralPath (Join-Path $root 'Assets/ui/rml/settings.rml') -Raw
$composition = Get-Content -LiteralPath (Join-Path $root 'Src/ui/menu/MenuComposition.cpp') -Raw
$layoutSource = Get-Content -LiteralPath (Join-Path $root 'Src/vrui/VRUILayoutManager.cpp') -Raw
$layoutHeader = Get-Content -LiteralPath (Join-Path $root 'Src/vrui/VRUILayoutManager.h') -Raw

foreach ($token in @(
    'kBoardWidgetLayoutContainer = "BoardWidgets"',
    'BoardWidgetDefaultTransform(float scale)',
    'transform.translate = { 0.0f, 0.0f, kSceneScreenSurfaceDepth };',
    'const std::string& layoutId = surface.id;',
    'ResetBoardWidgetTransform(SurfaceState& surface)',
    'removeElementAnywhere(surface.id);',
    'removeElementsWithPrefix(',
    'surface.id + ".BoardWidget"',
    'removeElementFromContainer(',
    'ApplyTabletSurfaceRotation(',
    'tabletTransform->rotate * fit',
    'kBoardWidgetContainerName = "FixedWidgetsContainer"',
    'ResolveBoardWidgetParentNode()',
    'ResolveTabletWorldTransform(',
    'MakeRelativeTransform(*tabletWorld, surface.node->world)',
    'saved->boardRelativeTransform',
    'StatusSceneSurface().boardWidget = true;',
    'surface->second.boardWidget = true;',
    'surface.node->local = BoardWidgetDefaultTransform(1.0f);',
    'settings.galleryPhotoPanelDefaultScale);',
    'widgetParentNode->AttachChild(surface.node.get());'
)) {
    if (-not $source.Contains($token)) {
        throw "Missing board-widget surface contract token: $token"
    }
}

if ($source -match 'BoardWidgetV[123]' -or
    $source.Contains('kBoardWidgetLayoutSuffix') -or
    $source.Contains('kPreviousBoardWidgetLayoutSuffix')) {
    throw 'Versioned board-widget persistence must not return'
}
foreach ($token in @(
    'removeElementFromContainer(',
    'removeElementsWithPrefix(',
    'element.id.starts_with(elementIdPrefix)'
)) {
    if (-not $layoutSource.Contains($token) -and
        -not $layoutHeader.Contains($token)) {
        throw "Missing layout cleanup contract token: $token"
    }
}
foreach ($token in @(
    'bool boardRelativeTransform = false;',
    'el.value("boardRelativeTransform", false)',
    'je["boardRelativeTransform"] = e.boardRelativeTransform'
)) {
    if (-not $layoutSource.Contains($token) -and
        -not $layoutHeader.Contains($token)) {
        throw "Missing board-relative persistence token: $token"
    }
}
if (-not $header.Contains('bool boardWidget = false;')) {
    throw 'SurfaceState must identify generic board widgets'
}
if (-not $header.Contains('RE::NiNode* tabletRootNode = nullptr;')) {
    throw 'SurfaceState must cache the tablet root used by its visual rotation'
}
if (-not $header.Contains('bool statusWidgetVisible = false;')) {
    throw 'SettingsDraft must carry Status widget visibility'
}
if (-not $header.Contains('_statusWidgetResetPending')) {
    throw 'Status widget reset must be queued to the game thread'
}

foreach ($token in @(
    '"FixedWidgetsContainer",',
    'persistentPanel->addElement(fixedWidgetsContainer);'
)) {
    if (-not $composition.Contains($token)) {
        throw "Menu composition must create the board-widget container: $token"
    }
}

foreach ($token in @(
    '"general", "visuals", "items", "widgets", "tutorials"',
    'BindClick(_settingsDocument, "toggle-status-widget");',
    'ConsumeStatusWidgetToggleRequested()',
    'SetStatusWidgetEnabled(bool enabled)',
    '_statusWidgetToggleRequested = true;'
)) {
    if (-not $rmlUiSource.Contains($token)) {
        throw "Missing Status settings wiring: $token"
    }
}
if (-not $rmlUiHeader.Contains('ConsumeStatusWidgetToggleRequested();') -or
    -not $rmlUiHeader.Contains('SetStatusWidgetEnabled(bool enabled);')) {
    throw 'Status settings API is incomplete'
}
foreach ($token in @(
    '_draft.statusWidgetVisible = settings.statusWidgetVisible;',
    'settings.statusWidgetVisible = _draft.statusWidgetVisible;'
)) {
    if (-not $settingsSource.Contains($token)) {
        throw "Settings draft must persist Status visibility: $token"
    }
}
foreach ($token in @(
    'id="tab-widgets"',
    'id="tab-tutorials"',
    'id="page-widgets"',
    'id="page-tutorials"',
    'id="toggle-status-widget"',
    'id="toggle-show-tutorials"'
)) {
    if (-not $settingsRml.Contains($token)) {
        throw "Missing Settings RML contract token: $token"
    }
}

if ($source.Contains('panel->addElement(container);')) {
    throw 'RmlPanelHost must not mutate the persistent-panel hierarchy during updates'
}
if ($source.Contains('ComposeTransform(backgroundNode->world, surface.node->local)')) {
    throw 'Board widgets must never convert through world-space during attachment'
}

$statusStart = $source.IndexOf('bool RmlPanelHost::UpdateStatusSceneSurfaceGameThread')
$statusEnd = $source.IndexOf('bool RmlPanelHost::UpdateWelcomeSceneSurfaceGameThread', $statusStart)
$statusSource = $source.Substring($statusStart, $statusEnd - $statusStart)
if ($statusSource.Contains('backgroundNode->AttachChild(surface.node.get())')) {
    throw 'Status surface must not attach directly to the board background'
}

$galleryStart = $source.IndexOf('void RmlPanelHost::UpdateGalleryPhotoSurfacesGameThread')
$galleryEnd = $source.IndexOf('void RmlPanelHost::UpdatePinnedWidgetVisibilityGameThread', $galleryStart)
$gallerySource = $source.Substring($galleryStart, $galleryEnd - $galleryStart)
if ($gallerySource.Contains('backgroundNode->AttachChild(surface.node.get())')) {
    throw 'Gallery photo surfaces must not attach directly to the board background'
}

$persistStart = $source.IndexOf('void RmlPanelHost::PersistSurfaceTransform')
$persistEnd = $source.IndexOf('void RmlPanelHost::ResetBoardWidgetTransform', $persistStart)
$persistSource = $source.Substring($persistStart, $persistEnd - $persistStart)
foreach ($token in @(
    'if (vrui::VRUILayoutManager::getElement(layoutContainer, layoutId))',
    'VRUILayoutManager::updateElementTransform(',
    'VRUILayoutManager::registerDefaultLayout('
)) {
    if (-not $persistSource.Contains($token)) {
        throw "Surface persistence must upsert missing layout elements: $token"
    }
}

Write-Output 'board widget surface contract check: ok'
