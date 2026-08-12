$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content (Join-Path $root 'Src/ui/pointer/PointerVisualController.cpp') -Raw

$smoothing = $source.IndexOf('smoothedPosition.x +=')
$suppression = $source.IndexOf('if (manager._pointerVisual.IsReticleSuppressed())')
$showReticle = $source.IndexOf('reticle->SetAppCulled(false);')

if ($smoothing -lt 0 -or $suppression -lt 0 -or $showReticle -lt 0) {
    throw 'Pointer suppression tracking tokens are missing.'
}
if ($suppression -lt $smoothing) {
    throw 'Suppressed reticle must keep tracking the board hit before returning.'
}
if ($suppression -gt $showReticle) {
    throw 'Suppression must be checked before showing the reticle.'
}

Write-Host 'Suppressed pointer tracking contract passed.'
