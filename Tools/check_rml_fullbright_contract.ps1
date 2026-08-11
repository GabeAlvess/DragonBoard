$utils = Get-Content -Raw "$PSScriptRoot\..\Src\ui\rml\RmlSceneSurfaceUtils.cpp"
$panelHostSource = Get-Content -Raw "$PSScriptRoot\..\Src\ui\rml\RmlPanelHost.cpp"
$screenNifPath = "$PSScriptRoot\..\Assets\meshes\DragonBoardVR\RmlUIScreen.nif"
$galleryNifPath = "$PSScriptRoot\..\Assets\meshes\DragonBoardVR\GalleryPhotoSurface.nif"
foreach ($token in @(
    'kOwnEmit', 'kExternalEmittance', 'kReceiveShadows', 'kCastShadows',
    'emissiveMult = 1.0f', 'specularColorScale = 0.0f',
    'BSEffectShaderProperty', 'sourceTexture', 'IsolateRmlSurfaceMaterial'
)) {
    if (-not $utils.Contains($token)) { throw "Missing fullbright token: $token" }
}
$bindingCount = ([regex]::Matches($panelHostSource, 'IsolateRmlSurfaceMaterial')).Count
if ($bindingCount -ne 5) { throw "Expected 5 isolated RML surface bindings, found $bindingCount" }
$callCount = ([regex]::Matches($panelHostSource, 'ConfigureRmlSurfaceFullbright')).Count
if ($callCount -ne 4) { throw "Expected 4 RML fullbright surfaces, found $callCount" }
$screenNif = [System.Text.Encoding]::ASCII.GetString(
    [System.IO.File]::ReadAllBytes($screenNifPath))
foreach ($token in @('BSEffectShaderProperty', 'textures\RmlUI0.dds')) {
    if (-not $screenNif.Contains($token)) { throw "Missing RML screen NIF token: $token" }
}
if ($screenNif.Contains('BSLightingShaderProperty')) {
    throw 'RML screen NIF must not use the game-lit BSLightingShaderProperty'
}
$galleryNif = [System.Text.Encoding]::ASCII.GetString(
    [System.IO.File]::ReadAllBytes($galleryNifPath))
foreach ($token in @('BSLightingShaderProperty', 'textures\RmlUI0.dds')) {
    if (-not $galleryNif.Contains($token)) { throw "Missing gallery photo NIF token: $token" }
}
if ($galleryNif.Contains('NiAlphaProperty')) {
    throw 'Gallery photo NIF must stay opaque so it receives the board shadow pass'
}
foreach ($token in @(
    'DragonBoardVR\\GalleryPhotoSurface.nif',
    'IsolateRmlSurfaceMaterial(*geometry, false)',
    'ConfigureRmlSurfaceShadowReceiver'
)) {
    if (-not $panelHostSource.Contains($token)) { throw "Missing gallery shadow receiver token: $token" }
}
Write-Output 'rml fullbright contract check: ok'
