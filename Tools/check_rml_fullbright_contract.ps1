$utils = Get-Content -Raw "$PSScriptRoot\..\Src\ui\rml\RmlSceneSurfaceUtils.cpp"
$panelHostSource = Get-Content -Raw "$PSScriptRoot\..\Src\ui\rml\RmlPanelHost.cpp"
$screenNifPath = "$PSScriptRoot\..\Assets\meshes\DragonBoardVR\RmlUIScreen.nif"
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
if ($callCount -ne 5) { throw "Expected 5 RML fullbright surfaces, found $callCount" }
$screenNif = [System.Text.Encoding]::ASCII.GetString(
    [System.IO.File]::ReadAllBytes($screenNifPath))
foreach ($token in @('BSEffectShaderProperty', 'textures\RmlUI0.dds')) {
    if (-not $screenNif.Contains($token)) { throw "Missing RML screen NIF token: $token" }
}
if ($screenNif.Contains('BSLightingShaderProperty')) {
    throw 'RML screen NIF must not use the game-lit BSLightingShaderProperty'
}
Write-Output 'rml fullbright contract check: ok'