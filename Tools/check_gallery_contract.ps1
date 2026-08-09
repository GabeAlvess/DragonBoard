$ErrorActionPreference = 'Stop'
$rml = Get-Content -Raw "$PSScriptRoot\..\Assets\ui\rml\gallery.rml"
$cpp = Get-Content -Raw "$PSScriptRoot\..\Src\ui\rml\DragonBoardRmlUi.cpp"
$ids = @('gallery-favorite', 'gallery-delete')
foreach ($id in $ids) {
    if (-not $rml.Contains('id="' + $id + '"')) { throw "Missing RML id: $id" }
    if (-not $cpp.Contains('"' + $id + '"')) {
        throw "Missing C++ binding: $id"
    }
}
foreach ($removedId in @('gallery-name', 'gallery-rename', 'gallery-close')) {
    if ($rml.Contains('id="' + $removedId + '"')) {
        throw "Removed gallery control returned: $removedId"
    }
}
foreach ($removedText in @('PIN PHOTO', 'PIN AS PANEL', 'PIN TO MAP')) {
    if ($rml.Contains($removedText)) { throw "Removed gallery label returned: $removedText" }
}
foreach ($requiredText in @('>PIN</span>', '>MARKER ON MAP</span>')) {
    if (-not $rml.Contains($requiredText)) { throw "Missing gallery pin label: $requiredText" }
}
$css = Get-Content -Raw "$PSScriptRoot\..\Assets\ui\rml\gallery.rcss"
if (-not $css.Contains('width: 770px; height: 770px')) {
    throw 'Gallery preview is not square'
}
$pin = Get-Content -Raw "$PSScriptRoot\..\Assets\ui\rml\gallery_photo_pin.rcss"
if (-not $pin.Contains('width: 1024px; height: 1080px')) {
    throw 'Pinned photo surface does not reserve the larger bottom caption area'
}
foreach ($token in @(
    'background-color: #ffffff',
    'decorator: image("assets/Background/photoframe.jpg" fill)',
    'width: 976px; height: 976px',
    'font-size: 56px',
    '#photo-date-time'
)) {
    if (-not $pin.Contains($token)) { throw "Missing pinned photo frame token: $token" }
}
$pinRml = Get-Content -Raw "$PSScriptRoot\..\Assets\ui\rml\gallery_photo_pin.rml"
if ($pinRml.Contains('id="photo-name"') -or $pinRml.Contains('id="photo-date"')) {
    throw 'Pinned photo still displays its old name or full date'
}
if (-not $pinRml.Contains('id="photo-date-time"')) {
    throw 'Pinned photo day and time are missing'
}
$photoFrame = "$PSScriptRoot\..\Assets\ui\rml\assets\Background\photoframe.jpg"
if (-not (Test-Path -LiteralPath $photoFrame)) {
    throw 'Missing pinned photo white fallback texture'
}
foreach ($token in @(
    '.gallery-capture-actions { display: flex; flex-direction: column; width: 490px',
    '.gallery-primary-action { width: 490px; height: 104px',
    'background-color: rgba(12, 35, 27, 220)',
    '#gallery-timer { width: 490px; height: 52px'
)) {
    if (-not $css.Contains($token)) { throw "Missing gallery capture layout token: $token" }
}
foreach ($token in @(
    '.gallery-workspace { display: flex; width: 1920px; height: 820px',
    '.gallery-title { color: #f0f0f0; font-size: 68px',
    '.gallery-info-label { margin-top: 22px; color: #aeb0b0; font-size: 34px',
    '.gallery-meta { min-height: 68px; margin-top: 8px; overflow: hidden; color: #d0d1d1; font-size: 42px',
    '.gallery-details-spacer { flex-grow: 1; }'
)) {
    if (-not $css.Contains($token)) { throw "Missing gallery sidebar layout token: $token" }
}
if ($rml.Contains('<div class="gallery-list-header">')) {
    throw 'Gallery photo count must not create a separate list row'
}
if (-not $css.Contains('overflow-x: auto')) {
    throw 'Gallery browser is not horizontally scrollable'
}
if (-not $css.Contains('.gallery-frame { width: 1920px; height: 1080px; background-color: transparent; }')) {
    throw 'Gallery background is still covered by the frame'
}
if ($rml.Contains('gallery-preview" class="gallery-preview" src="assets/Background/Inventory.png')) {
    throw 'Gallery photo still uses the panel background as a placeholder'
}
if (-not $cpp.Contains('preview->RemoveAttribute("src")')) {
    throw 'Gallery preview does not clear its image when no photo is selected'
}
foreach ($token in @(
    '.gallery-details-sidebar', '.gallery-stage', '.gallery-actions-sidebar',
    '.gallery-card-favorite', '.gallery-card-delete'
)) {
    if (-not $css.Contains($token)) { throw "Missing gallery layout token: $token" }
}
foreach ($token in @(
    'gallery-card-favorite-', 'gallery-card-delete-',
    'assets/Icons/trashIcon.png'
)) {
    if (-not $cpp.Contains($token)) { throw "Missing gallery card binding: $token" }
}
if (-not $cpp.Contains('EscapeRml(photo.location)')) {
    throw 'Gallery cards do not display photo locations'
}
if ($cpp.Contains('gallery-card-name\">" + EscapeRml(photo.name)')) {
    throw 'Gallery cards still display photo names'
}
$hostCpp = Get-Content -Raw "$PSScriptRoot\..\Src\ui\rml\RmlPanelHost.cpp"
if (-not $hostCpp.Contains('std::stable_partition')) {
    throw 'Gallery photos are not ordered favorite-first'
}
$trashIcon = "$PSScriptRoot\..\Assets\ui\rml\assets\Icons\trashIcon.png"
if (-not (Test-Path -LiteralPath $trashIcon)) {
    throw 'Missing gallery trash icon'
}
$renderer = Get-Content -Raw "$PSScriptRoot\..\Src\ui\rml\DragonBoardRmlRenderer.cpp"
if ($renderer.Contains('IsGalleryPhotoTexture') -or
    $renderer.Contains('DXGI_FORMAT_R8G8B8A8_UNORM_SRGB')) {
    throw 'Gallery photos must use the regular UNORM RmlUi texture path'
}
if (-not $renderer.Contains('desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;')) {
    throw 'RmlUi texture upload is not using UNORM'
}$mapMarker = Get-Content -Raw "$PSScriptRoot\..\Src\vrui\VRUIMapMarker.cpp"
foreach ($token in @(
    'currentCellIsInterior',
    '!currentCellIsInterior &&'
)) {
    if (-not $mapMarker.Contains($token)) {
        throw "Gallery map markers do not remain visible indoors: $token"
    }
}
$capture = Get-Content -Raw "$PSScriptRoot\..\Src\ui\gallery\GalleryCaptureService.cpp"
foreach ($token in @(
    'ResolveMapLocationGameThread',
    'ResolveLocationNameGameThread',
    'GetTeleportLinkedDoor',
    'IsExteriorCell',
    'GalleryMapLocation',
    'kTamrielWorldspace = 0x0000003C',
    'locationName == "Скайрим"'
)) {
    if (-not $capture.Contains($token)) {
        throw "Missing interior gallery map resolver token: $token"
    }
}
if (-not $hostCpp.Contains('ResolveLocationNameGameThread(*player)')) {
    throw 'Status and gallery capture do not share the same location-name resolver'
}
$catalog = Get-Content -Raw "$PSScriptRoot\..\Src\ui\gallery\GalleryCatalog.cpp"
foreach ($token in @(
    'GetModuleHandleExW', 'GetFinalPathNameByHandleW',
    'FindMo2PluginPath', 'gallery storage resolved to'
)) {
    if (-not $catalog.Contains($token)) {
        throw "Missing physical gallery path token: $token"
    }
}
if ($catalog.Contains('return GameDirectory() / "Data" / "SKSE" / "Plugins" / "DragonBoardVR" / "gallery";')) {
    throw 'Gallery storage still writes directly through the MO2 virtual Data path'
}Write-Output 'gallery contract check: ok'
