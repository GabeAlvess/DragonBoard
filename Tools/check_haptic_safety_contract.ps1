$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$hapticSource = Get-Content -LiteralPath (Join-Path $root 'Src\runtime\vr\HapticFeedback.cpp') -Raw
$buttonSource = Get-Content -LiteralPath (Join-Path $root 'Src\vrui\VRUIButton.cpp') -Raw

if ($hapticSource -notmatch 'openVR->TriggerHapticPulse') {
    throw 'Haptics do not use Skyrim''s transport-compatible OpenVR wrapper.'
}
if ($hapticSource -notmatch 'kMaximumEffectiveDurationSeconds = 0\.05f') {
    throw 'Haptic duration is not capped at 50 ms.'
}
if ($hapticSource -notmatch 'hapticsDisabled' -or
    $hapticSource -notmatch 'haptic call blocked') {
    throw 'Slow haptic calls do not disable later pulses.'
}
if ($buttonSource -match 'triggerHaptic\(true, 1\.0f, 0\.2f\)') {
    throw 'Grab/release restored the unsafe fixed haptic pulse.'
}
if ($buttonSource -notmatch 'hapticSettings\.hapticIntensity' -or
    $buttonSource -notmatch 'settings\.hapticIntensity \* 0\.35f') {
    throw 'Pin haptics do not use configured intensity.'
}

Write-Output 'Haptic safety contract passed.'
