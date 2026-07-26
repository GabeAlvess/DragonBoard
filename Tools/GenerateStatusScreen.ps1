param(
    [Parameter(Mandatory = $true)]
    [string]$Source,

    [Parameter(Mandatory = $true)]
    [string]$Destination,

    [string]$Replacement = 'ImGui1.dds'
)

$sourceBytes = [System.IO.File]::ReadAllBytes($Source)
$searchBytes = [System.Text.Encoding]::ASCII.GetBytes('ImGui0.dds')
$replacementBytes = [System.Text.Encoding]::ASCII.GetBytes($Replacement)

if ($searchBytes.Length -ne $replacementBytes.Length) {
    throw 'Status texture replacement must preserve the NIF string length.'
}

$matchOffset = -1
$matchCount = 0
for ($offset = 0; $offset -le $sourceBytes.Length - $searchBytes.Length; $offset++) {
    $matches = $true
    for ($index = 0; $index -lt $searchBytes.Length; $index++) {
        if ($sourceBytes[$offset + $index] -ne $searchBytes[$index]) {
            $matches = $false
            break
        }
    }
    if ($matches) {
        $matchOffset = $offset
        $matchCount++
    }
}

if ($matchCount -ne 1) {
    throw "Expected exactly one ImGui0.dds reference in '$Source', found $matchCount."
}

[System.Array]::Copy(
    $replacementBytes,
    0,
    $sourceBytes,
    $matchOffset,
    $replacementBytes.Length)

$destinationDirectory = [System.IO.Path]::GetDirectoryName($Destination)
[System.IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
[System.IO.File]::WriteAllBytes($Destination, $sourceBytes)
