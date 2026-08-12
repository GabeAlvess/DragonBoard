[CmdletBinding()]
param(
    [string]$SourceArchive = 'I:\Games\Skyrim VR FUS\mods\DragonBoardVR\DragonBoardVR.rar',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\..\artifacts\fomod'),
    [string]$Version = '',
    [string]$Author = 'GabeAlvz',
    [string]$Website = 'https://github.com/GabeAlvess/DragonBoard',
    [switch]$KeepStaging,
    [switch]$SkipSchemaValidation
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$physicalSizes = [ordered]@{
    Mini = 0.840000
    Pro = 1.050000
    ProMax = 1.312500
}

$floatingSizes = [ordered]@{
    Mini = 1.215000
    Pro = 1.350000
    ProMax = 1.485000
}

$floatingOffsets = [ordered]@{
    fMenuOffsetX = 0.000000
    fMenuOffsetY = -20.000000
    fMenuOffsetZ = -3.500000
}

$languages = [ordered]@{
    en = 'English'
    de = 'German'
    es = 'Spanish'
    fr = 'French'
    it = 'Italian'
    ja = 'Japanese'
    'pt-BR' = 'Brazilian Portuguese'
    ru = 'Russian'
    'zh-CN' = 'Simplified Chinese'
}

$physicalFiles = @(
    'DragonBoardVR.esp',
    'meshes\DragonBoardVR\dragonboard_physical.nif',
    'meshes\DragonBoardVR\dragonboard_vrik_proxy.nif',
    'meshes\DragonBoardVR\dragonboard_vrik_proxy_hidden.nif'
)

$floatingFiles = @(
    'SKSE\Plugins\SpellWheelVR_CustomConsoleCommand_DragonBoardVR.ini'
)

$mainIniPaths = @(
    'SKSE\Plugins\DragonBoardVR.ini',
    'SKSE\Plugins\DragonBoardVR\Defaults\DragonBoardVR.ini'
)

$layoutIniPaths = @(
    'SKSE\Plugins\DragonBoardVR_Layout.ini',
    'SKSE\Plugins\DragonBoardVR\Defaults\DragonBoardVR_Layout.ini'
)

$installerImages = @(
    'Floating DB.jpg',
    'Physical DB.jpg',
    'fDB Mini.jpg',
    'fDB Pro.jpg',
    'fDB Pro Max.jpg'
)

function Get-SevenZipPath {
    $candidates = @(
        'C:\Program Files\7-Zip\7z.exe',
        'C:\Program Files (x86)\7-Zip\7z.exe'
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $command = Get-Command 7z -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw '7-Zip was not found. Install 7-Zip or add 7z.exe to PATH.'
}

function Assert-ChildPath {
    param(
        [Parameter(Mandatory)] [string]$Parent,
        [Parameter(Mandatory)] [string]$Child
    )

    $parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    $childPath = [IO.Path]::GetFullPath($Child)
    if (-not $childPath.StartsWith($parentPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe path '$childPath': expected a child of '$parentPath'."
    }
}

function Reset-Directory {
    param(
        [Parameter(Mandatory)] [string]$Parent,
        [Parameter(Mandatory)] [string]$Path
    )

    Assert-ChildPath -Parent $Parent -Child $Path
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Copy-RelativePath {
    param(
        [Parameter(Mandatory)] [string]$SourceRoot,
        [Parameter(Mandatory)] [string]$RelativePath,
        [Parameter(Mandatory)] [string]$DestinationRoot
    )

    $source = Join-Path $SourceRoot $RelativePath
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Required source file is missing: $RelativePath"
    }

    $destination = Join-Path $DestinationRoot $RelativePath
    $destinationParent = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

function Remove-RelativePath {
    param(
        [Parameter(Mandatory)] [string]$Root,
        [Parameter(Mandatory)] [string]$RelativePath
    )

    $target = Join-Path $Root $RelativePath
    Assert-ChildPath -Parent $Root -Child $target
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Force
    }
}

function Set-IniValue {
    param(
        [Parameter(Mandatory)] [string]$Text,
        [Parameter(Mandatory)] [string]$Section,
        [Parameter(Mandatory)] [string]$Key,
        [Parameter(Mandatory)] [string]$Value
    )

    $lines = [Collections.Generic.List[string]]::new()
    foreach ($line in ($Text -split '\r?\n')) {
        $lines.Add($line)
    }

    $sectionStart = -1
    $sectionEnd = $lines.Count
    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ($lines[$index] -match '^\s*\[(?<name>[^]]+)\]\s*$') {
            if ($sectionStart -ge 0) {
                $sectionEnd = $index
                break
            }
            if ($Matches.name.Equals($Section, [StringComparison]::OrdinalIgnoreCase)) {
                $sectionStart = $index
            }
        }
    }

    if ($sectionStart -lt 0) {
        if ($lines.Count -gt 0 -and $lines[$lines.Count - 1] -ne '') {
            $lines.Add('')
        }
        $lines.Add("[$Section]")
        $lines.Add("$Key = $Value")
        return $lines -join "`r`n"
    }

    for ($index = $sectionStart + 1; $index -lt $sectionEnd; $index++) {
        if ($lines[$index] -match '^\s*(?<key>[^=;#]+?)\s*=') {
            if ($Matches.key.Trim().Equals($Key, [StringComparison]::OrdinalIgnoreCase)) {
                $lines[$index] = "$Key = $Value"
                return $lines -join "`r`n"
            }
        }
    }

    $lines.Insert($sectionEnd, "$Key = $Value")
    return $lines -join "`r`n"
}

function Write-Utf8BomFile {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$Content
    )

    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($true))
}

function Escape-Xml {
    param([Parameter(Mandatory)] [string]$Value)
    return [Security.SecurityElement]::Escape($Value)
}

function Add-PluginType {
    param(
        [Parameter(Mandatory)] [Text.StringBuilder]$Builder,
        [Parameter(Mandatory)] [string]$Type,
        [int]$Indent = 14
    )

    $spaces = ' ' * $Indent
    [void]$Builder.AppendLine("$spaces<typeDescriptor>")
    [void]$Builder.AppendLine(('{0}  <type name="{1}" />' -f $spaces, $Type))
    [void]$Builder.AppendLine("$spaces</typeDescriptor>")
}

function Add-FlagPlugin {
    param(
        [Parameter(Mandatory)] [Text.StringBuilder]$Builder,
        [Parameter(Mandatory)] [string]$Name,
        [Parameter(Mandatory)] [string]$Description,
        [Parameter(Mandatory)] [string]$Flag,
        [Parameter(Mandatory)] [string]$Value,
        [Parameter(Mandatory)] [string]$Type,
        [string]$ImagePath = ''
    )

    [void]$Builder.AppendLine(('            <plugin name="{0}">' -f (Escape-Xml $Name)))
    [void]$Builder.AppendLine("              <description>$(Escape-Xml $Description)</description>")
    if (-not [string]::IsNullOrWhiteSpace($ImagePath)) {
        [void]$Builder.AppendLine(('              <image path="{0}" />' -f (Escape-Xml $ImagePath)))
    }
    [void]$Builder.AppendLine('              <conditionFlags>')
    [void]$Builder.AppendLine(('                <flag name="{0}">{1}</flag>' -f (Escape-Xml $Flag), (Escape-Xml $Value)))
    [void]$Builder.AppendLine('              </conditionFlags>')
    Add-PluginType -Builder $Builder -Type $Type
    [void]$Builder.AppendLine('            </plugin>')
}

function Add-ConditionalFolderPattern {
    param(
        [Parameter(Mandatory)] [Text.StringBuilder]$Builder,
        [Parameter(Mandatory)] [Collections.IDictionary]$Flags,
        [Parameter(Mandatory)] [string]$Source,
        [Parameter(Mandatory)] [int]$Priority
    )

    [void]$Builder.AppendLine('      <pattern>')
    [void]$Builder.AppendLine('        <dependencies operator="And">')
    foreach ($entry in $Flags.GetEnumerator()) {
        [void]$Builder.AppendLine(('          <flagDependency flag="{0}" value="{1}" />' -f (Escape-Xml ([string]$entry.Key)), (Escape-Xml ([string]$entry.Value))))
    }
    [void]$Builder.AppendLine('        </dependencies>')
    [void]$Builder.AppendLine('        <files>')
    [void]$Builder.AppendLine(('          <folder source="{0}" destination="" priority="{1}" />' -f (Escape-Xml $Source), $Priority))
    [void]$Builder.AppendLine('        </files>')
    [void]$Builder.AppendLine('      </pattern>')
}

function Write-ModuleConfig {
    param(
        [Parameter(Mandatory)] [string]$Path
    )

    $builder = [Text.StringBuilder]::new()
    [void]$builder.AppendLine('<?xml version="1.0" encoding="utf-8"?>')
    [void]$builder.AppendLine('<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="https://raw.githubusercontent.com/Nexus-Mods/fomod-installer/master/src/InstallScripting/XmlScript/Schemas/XmlScript5.0.xsd">')
    [void]$builder.AppendLine('  <moduleName position="Left" colour="FFFFFF">DragonBoardVR</moduleName>')
    [void]$builder.AppendLine('  <installSteps order="Explicit">')

    [void]$builder.AppendLine('    <installStep name="Core DragonBoard">')
    [void]$builder.AppendLine('      <optionalFileGroups order="Explicit">')
    [void]$builder.AppendLine('        <group name="Core DragonBoard" type="SelectAll">')
    [void]$builder.AppendLine('          <plugins order="Explicit">')
    [void]$builder.AppendLine('            <plugin name="Core DragonBoard">')
    [void]$builder.AppendLine('              <description>DragonBoard is a personal tablet with an integrated map, access to your inventory, spells, journal, quick actions, and other features.</description>')
    [void]$builder.AppendLine('              <image path="fomod\images\Floating DB.jpg" />')
    [void]$builder.AppendLine('              <files>')
    [void]$builder.AppendLine('                <folder source="Core" destination="" priority="0" />')
    [void]$builder.AppendLine('              </files>')
    Add-PluginType -Builder $builder -Type 'Required'
    [void]$builder.AppendLine('            </plugin>')
    [void]$builder.AppendLine('          </plugins>')
    [void]$builder.AppendLine('        </group>')
    [void]$builder.AppendLine('      </optionalFileGroups>')
    [void]$builder.AppendLine('    </installStep>')

    [void]$builder.AppendLine('    <installStep name="Options">')
    [void]$builder.AppendLine('      <optionalFileGroups order="Explicit">')
    [void]$builder.AppendLine('        <group name="DragonBoard Modes" type="SelectAtLeastOne">')
    [void]$builder.AppendLine('          <plugins order="Explicit">')
    [void]$builder.AppendLine('            <plugin name="Physical DragonBoard (esp/esl)">')
    [void]$builder.AppendLine('              <description>Physical tablet meshes for interaction with VRIK and HIGGS.</description>')
    [void]$builder.AppendLine('              <image path="fomod\images\Physical DB.jpg" />')
    [void]$builder.AppendLine('              <files>')
    [void]$builder.AppendLine('                <folder source="Options\PhysicalDragonBoard" destination="" priority="20" />')
    [void]$builder.AppendLine('              </files>')
    [void]$builder.AppendLine('              <conditionFlags>')
    [void]$builder.AppendLine('                <flag name="PhysicalEnabled">true</flag>')
    [void]$builder.AppendLine('              </conditionFlags>')
    Add-PluginType -Builder $builder -Type 'Optional'
    [void]$builder.AppendLine('            </plugin>')
    [void]$builder.AppendLine('            <plugin name="Floating DragonBoard (No esp)">')
    [void]$builder.AppendLine('              <description>No physics or world item, just the floating tablet. No esp.</description>')
    [void]$builder.AppendLine('              <image path="fomod\images\Floating DB.jpg" />')
    [void]$builder.AppendLine('              <files>')
    [void]$builder.AppendLine('                <folder source="Options\FloatingDragonBoard" destination="" priority="20" />')
    [void]$builder.AppendLine('              </files>')
    [void]$builder.AppendLine('              <conditionFlags>')
    [void]$builder.AppendLine('                <flag name="FloatingEnabled">true</flag>')
    [void]$builder.AppendLine('              </conditionFlags>')
    Add-PluginType -Builder $builder -Type 'Optional'
    [void]$builder.AppendLine('            </plugin>')
    [void]$builder.AppendLine('          </plugins>')
    [void]$builder.AppendLine('        </group>')
    [void]$builder.AppendLine('      </optionalFileGroups>')
    [void]$builder.AppendLine('    </installStep>')

    [void]$builder.AppendLine('    <installStep name="Board Size">')
    [void]$builder.AppendLine('      <optionalFileGroups order="Explicit">')
    [void]$builder.AppendLine('        <group name="Physical Size" type="SelectExactlyOne">')
    [void]$builder.AppendLine('          <plugins order="Explicit">')
    Add-FlagPlugin -Builder $builder -Name 'DragonBoard Mini' -Description 'Uses the compact physical-board scale.' -Flag 'PhysicalSize' -Value 'Mini' -Type 'Optional' -ImagePath 'fomod\images\fDB Mini.jpg'
    Add-FlagPlugin -Builder $builder -Name 'DragonBoard Pro' -Description 'Uses the standard physical-board scale.' -Flag 'PhysicalSize' -Value 'Pro' -Type 'Recommended' -ImagePath 'fomod\images\fDB Pro.jpg'
    Add-FlagPlugin -Builder $builder -Name 'DragonBoard Pro Max' -Description 'Uses the largest physical-board scale.' -Flag 'PhysicalSize' -Value 'ProMax' -Type 'Optional' -ImagePath 'fomod\images\fDB Pro Max.jpg'
    [void]$builder.AppendLine('          </plugins>')
    [void]$builder.AppendLine('        </group>')
    [void]$builder.AppendLine('        <group name="Floating Size" type="SelectExactlyOne">')
    [void]$builder.AppendLine('          <plugins order="Explicit">')
    Add-FlagPlugin -Builder $builder -Name 'DragonBoard Mini' -Description 'Uses the compact floating-board scale.' -Flag 'FloatingSize' -Value 'Mini' -Type 'Optional' -ImagePath 'fomod\images\fDB Mini.jpg'
    Add-FlagPlugin -Builder $builder -Name 'DragonBoard Pro' -Description 'Uses the standard floating-board scale.' -Flag 'FloatingSize' -Value 'Pro' -Type 'Recommended' -ImagePath 'fomod\images\fDB Pro.jpg'
    Add-FlagPlugin -Builder $builder -Name 'DragonBoard Pro Max' -Description 'Uses the largest floating-board scale.' -Flag 'FloatingSize' -Value 'ProMax' -Type 'Optional' -ImagePath 'fomod\images\fDB Pro Max.jpg'
    [void]$builder.AppendLine('          </plugins>')
    [void]$builder.AppendLine('        </group>')
    [void]$builder.AppendLine('      </optionalFileGroups>')
    [void]$builder.AppendLine('    </installStep>')

    [void]$builder.AppendLine('    <installStep name="Language">')
    [void]$builder.AppendLine('      <optionalFileGroups order="Explicit">')
    [void]$builder.AppendLine('        <group name="Language" type="SelectExactlyOne">')
    [void]$builder.AppendLine('          <plugins order="Explicit">')
    foreach ($entry in $languages.GetEnumerator()) {
        $type = if ($entry.Key -eq 'en') { 'Recommended' } else { 'Optional' }
        Add-FlagPlugin -Builder $builder -Name $entry.Value -Description "Use $($entry.Value) as the initial DragonBoard language." -Flag 'Language' -Value $entry.Key -Type $type
    }
    [void]$builder.AppendLine('          </plugins>')
    [void]$builder.AppendLine('        </group>')
    [void]$builder.AppendLine('      </optionalFileGroups>')
    [void]$builder.AppendLine('    </installStep>')
    [void]$builder.AppendLine('  </installSteps>')

    [void]$builder.AppendLine('  <conditionalFileInstalls>')
    [void]$builder.AppendLine('    <patterns>')
    foreach ($physicalSize in $physicalSizes.Keys) {
        foreach ($language in $languages.Keys) {
            Add-ConditionalFolderPattern -Builder $builder -Flags ([ordered]@{
                PhysicalSize = $physicalSize
                Language = $language
            }) -Source "Presets\Main\Disabled\$physicalSize\$language" -Priority 50

            Add-ConditionalFolderPattern -Builder $builder -Flags ([ordered]@{
                PhysicalEnabled = 'true'
                PhysicalSize = $physicalSize
                Language = $language
            }) -Source "Presets\Main\Enabled\$physicalSize\$language" -Priority 60
        }
    }

    foreach ($floatingSize in $floatingSizes.Keys) {
        Add-ConditionalFolderPattern -Builder $builder -Flags ([ordered]@{
            FloatingSize = $floatingSize
        }) -Source "Presets\Layout\$floatingSize" -Priority 70
    }
    [void]$builder.AppendLine('    </patterns>')
    [void]$builder.AppendLine('  </conditionalFileInstalls>')
    [void]$builder.AppendLine('</config>')

    Write-Utf8BomFile -Path $Path -Content $builder.ToString()
}

function Validate-ModuleConfig {
    param([Parameter(Mandatory)] [string]$Path)

    [xml](Get-Content -LiteralPath $Path -Raw) | Out-Null
    if ($SkipSchemaValidation) {
        return
    }

    $schemaUrl = 'https://raw.githubusercontent.com/Nexus-Mods/fomod-installer/master/src/InstallScripting/XmlScript/Schemas/XmlScript5.0.xsd'
    $schemaPath = Join-Path $workingRoot 'XmlScript5.0.xsd'
    Invoke-WebRequest -UseBasicParsing -Uri $schemaUrl -OutFile $schemaPath

    $schemas = [Xml.Schema.XmlSchemaSet]::new()
    [void]$schemas.Add($null, $schemaPath)
    $settings = [Xml.XmlReaderSettings]::new()
    $settings.Schemas = $schemas
    $settings.ValidationType = [Xml.ValidationType]::Schema
    $errors = [Collections.Generic.List[string]]::new()
    $handler = [Xml.Schema.ValidationEventHandler]{
        param($sender, $eventArgs)
        $errors.Add($eventArgs.Message)
    }
    $settings.add_ValidationEventHandler($handler)
    $reader = [Xml.XmlReader]::Create($Path, $settings)
    try {
        while ($reader.Read()) {}
    } finally {
        $reader.Dispose()
    }

    if ($errors.Count -gt 0) {
        throw "ModuleConfig.xml failed schema validation:`n$($errors -join "`n")"
    }
}

$sourceArchivePath = [IO.Path]::GetFullPath($SourceArchive)
if (-not (Test-Path -LiteralPath $sourceArchivePath)) {
    throw "Source archive was not found: $sourceArchivePath"
}

$sevenZip = Get-SevenZipPath
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$assetRoot = Join-Path $projectRoot 'Assets'
foreach ($imageName in $installerImages) {
    $imagePath = Join-Path $assetRoot $imageName
    if (-not (Test-Path -LiteralPath $imagePath)) {
        throw "Missing FOMOD image: $imagePath"
    }
}

$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$workingRoot = Join-Path $outputRoot 'working'
$sourceRoot = Join-Path $workingRoot 'source'
$stageRoot = Join-Path $workingRoot 'DragonBoardVR-FOMOD-stage'
Reset-Directory -Parent $outputRoot -Path $workingRoot
New-Item -ItemType Directory -Path $sourceRoot -Force | Out-Null
New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null

Write-Host "Extracting $sourceArchivePath"
& $sevenZip x -y "-o$sourceRoot" $sourceArchivePath | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "7-Zip failed to extract the source archive with exit code $LASTEXITCODE."
}

$unexpectedBackups = Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Where-Object { $_.Name -match '\.bak$|pre-vrik' }
if ($unexpectedBackups) {
    throw "The source archive still contains backup files: $($unexpectedBackups.FullName -join ', ')"
}

$devCommandsPath = Join-Path $sourceRoot 'SKSE\Plugins\DragonBoardVR_DevCommands.ini'
if ((Test-Path -LiteralPath $devCommandsPath) -and
    (Get-Content -LiteralPath $devCommandsPath -Raw) -match 'player\.additem\s+91000800') {
    throw 'The source archive still contains the load-order-specific DragonBoard additem command.'
}

$dllPath = Join-Path $sourceRoot 'SKSE\Plugins\DragonBoardVR.dll'
if (-not (Test-Path -LiteralPath $dllPath)) {
    throw 'The source archive does not contain SKSE\Plugins\DragonBoardVR.dll.'
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $detectedVersion = (Get-Item -LiteralPath $dllPath).VersionInfo.ProductVersion
    if ($detectedVersion -match '(?<version>\d+\.\d+\.\d+)') {
        $Version = $Matches.version
    } else {
        throw "Could not detect a release version from DragonBoardVR.dll ('$detectedVersion')."
    }
}

foreach ($language in $languages.Keys) {
    $translationPath = Join-Path $sourceRoot "SKSE\Plugins\DragonBoardVR\translations\$language.json"
    if (-not (Test-Path -LiteralPath $translationPath)) {
        throw "Missing translation required by the FOMOD language page: $language.json"
    }
}

$coreRoot = Join-Path $stageRoot 'Core'
New-Item -ItemType Directory -Path $coreRoot -Force | Out-Null
Copy-Item -Path (Join-Path $sourceRoot '*') -Destination $coreRoot -Recurse -Force

foreach ($relativePath in ($physicalFiles + $floatingFiles + $mainIniPaths + $layoutIniPaths)) {
    Remove-RelativePath -Root $coreRoot -RelativePath $relativePath
}

$physicalRoot = Join-Path $stageRoot 'Options\PhysicalDragonBoard'
foreach ($relativePath in $physicalFiles) {
    Copy-RelativePath -SourceRoot $sourceRoot -RelativePath $relativePath -DestinationRoot $physicalRoot
}

$floatingRoot = Join-Path $stageRoot 'Options\FloatingDragonBoard'
foreach ($relativePath in $floatingFiles) {
    Copy-RelativePath -SourceRoot $sourceRoot -RelativePath $relativePath -DestinationRoot $floatingRoot
}

$utf8 = [Text.UTF8Encoding]::new($true)
foreach ($physicalState in @('Disabled', 'Enabled')) {
    $enabledValue = if ($physicalState -eq 'Enabled') { 'true' } else { 'false' }
    foreach ($physicalSize in $physicalSizes.GetEnumerator()) {
        $scaleValue = ([double]$physicalSize.Value).ToString('0.000000', [Globalization.CultureInfo]::InvariantCulture)
        foreach ($language in $languages.Keys) {
            $presetRoot = Join-Path $stageRoot "Presets\Main\$physicalState\$($physicalSize.Key)\$language"
            foreach ($relativePath in $mainIniPaths) {
                $sourcePath = Join-Path $sourceRoot $relativePath
                $content = [IO.File]::ReadAllText($sourcePath, $utf8)
                $content = Set-IniValue -Text $content -Section 'PhysicalBoard' -Key 'bEnabled' -Value $enabledValue
                $content = Set-IniValue -Text $content -Section 'PhysicalBoard' -Key 'fScale' -Value $scaleValue
                $content = Set-IniValue -Text $content -Section 'Interface' -Key 'sLanguage' -Value $language
                Write-Utf8BomFile -Path (Join-Path $presetRoot $relativePath) -Content $content
            }
        }
    }
}

foreach ($floatingSize in $floatingSizes.GetEnumerator()) {
    $scaleValue = ([double]$floatingSize.Value).ToString('0.000000', [Globalization.CultureInfo]::InvariantCulture)
    $presetRoot = Join-Path $stageRoot "Presets\Layout\$($floatingSize.Key)"
    foreach ($relativePath in $layoutIniPaths) {
        $sourcePath = Join-Path $sourceRoot $relativePath
        $content = [IO.File]::ReadAllText($sourcePath, $utf8)
        $content = Set-IniValue -Text $content -Section 'Visual' -Key 'fMenuScale' -Value $scaleValue
        $content = Set-IniValue -Text $content -Section 'FixedButtons' -Key 'bShowDevButton' -Value 'false'
        $content = Set-IniValue -Text $content -Section 'FixedButtons' -Key 'fHomePosY' -Value '0.000000'
        $content = Set-IniValue -Text $content -Section 'FixedButtons' -Key 'fHomeRotX' -Value '0.000000'
        $content = Set-IniValue -Text $content -Section 'StatusWidget' -Key 'bVisible' -Value 'false'
        foreach ($offset in $floatingOffsets.GetEnumerator()) {
            $offsetValue = ([double]$offset.Value).ToString('0.000000', [Globalization.CultureInfo]::InvariantCulture)
            $content = Set-IniValue -Text $content -Section 'Visual' -Key $offset.Key -Value $offsetValue
        }
        Write-Utf8BomFile -Path (Join-Path $presetRoot $relativePath) -Content $content
    }
}

$fomodRoot = Join-Path $stageRoot 'fomod'
New-Item -ItemType Directory -Path $fomodRoot -Force | Out-Null
$fomodImageRoot = Join-Path $fomodRoot 'images'
New-Item -ItemType Directory -Path $fomodImageRoot -Force | Out-Null
foreach ($imageName in $installerImages) {
    Copy-Item -LiteralPath (Join-Path $assetRoot $imageName) -Destination (Join-Path $fomodImageRoot $imageName) -Force
}
$infoXml = @"
<?xml version="1.0" encoding="utf-8"?>
<fomod>
  <Name>DragonBoardVR</Name>
  <Author>$(Escape-Xml $Author)</Author>
  <Version>$(Escape-Xml $Version)</Version>
  <Website>$(Escape-Xml $Website)</Website>
  <Description>Native Skyrim VR interface with optional Physical DragonBoard and Floating DragonBoard installation modes.</Description>
</fomod>
"@
Write-Utf8BomFile -Path (Join-Path $fomodRoot 'info.xml') -Content $infoXml
$moduleConfigPath = Join-Path $fomodRoot 'ModuleConfig.xml'
Write-ModuleConfig -Path $moduleConfigPath
Validate-ModuleConfig -Path $moduleConfigPath

foreach ($relativePath in $physicalFiles) {
    if (Test-Path -LiteralPath (Join-Path $coreRoot $relativePath)) {
        throw "Physical-only file leaked into Core: $relativePath"
    }
}
foreach ($relativePath in $floatingFiles) {
    if (Test-Path -LiteralPath (Join-Path $coreRoot $relativePath)) {
        throw "Floating-only file leaked into Core: $relativePath"
    }
}

$nestedArchiveExtensions = @('.zip', '.7z', '.rar', '.tar', '.gz', '.bz2', '.xz')
$nestedArchives = Get-ChildItem -LiteralPath $stageRoot -File -Recurse | Where-Object {
    $_.Extension.ToLowerInvariant() -in $nestedArchiveExtensions
}
if ($nestedArchives) {
    $archiveList = ($nestedArchives.FullName -join [Environment]::NewLine)
    throw "FOMOD staging contains nested archives:`n$archiveList"
}

$archiveName = "DragonBoardVR-$Version-FOMOD.zip"
$archivePath = Join-Path $outputRoot $archiveName
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

Write-Host "Creating $archivePath"
Push-Location $stageRoot
try {
    & $sevenZip a -tzip -mx=9 $archivePath '.\*' | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "7-Zip failed to create the FOMOD archive with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}

& $sevenZip t $archivePath | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "7-Zip archive validation failed with exit code $LASTEXITCODE."
}

$sourceFileCount = (Get-ChildItem -LiteralPath $sourceRoot -Recurse -File).Count
$coreFileCount = (Get-ChildItem -LiteralPath $coreRoot -Recurse -File).Count
$archiveSize = (Get-Item -LiteralPath $archivePath).Length
Write-Host "Source files: $sourceFileCount"
Write-Host "Core files: $coreFileCount"
Write-Host "FOMOD archive: $archivePath ($archiveSize bytes)"

if (-not $KeepStaging) {
    Assert-ChildPath -Parent $outputRoot -Child $workingRoot
    Remove-Item -LiteralPath $workingRoot -Recurse -Force
}

[pscustomobject]@{
    Archive = $archivePath
    Version = $Version
    SourceFiles = $sourceFileCount
    CoreFiles = $coreFileCount
    PhysicalFiles = $physicalFiles.Count
    FloatingFiles = $floatingFiles.Count
    Languages = $languages.Count
}
