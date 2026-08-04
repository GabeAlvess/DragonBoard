set_xmakever('3.0.1')
includes('lib/commonlibsse-ng')

set_project('DragonBoardVR')
set_version('1.0.0')
set_license('MIT')

set_languages('c++23')
set_warnings('all')
set_policy('package.requires_lock', true)
set_toolset('msvc', 'ninja')

add_rules('mode.debug', 'mode.releasedbg', 'mode.release')

add_requires('simpleini v4.25')
add_requires('rmlui 6.2', { configs = { shared = false, lua = false, svg = false, lottie = false } })
add_requires('nlohmann_json')

-- CommonLibSSE-NG enables SE, AE, and VR by default. Override those dependency
-- options here so a plain `xmake f` cannot accidentally produce a cross-runtime
-- ABI for this VR-only plugin.
option('skyrim_se')
    set_default(false)
option_end()

option('skyrim_ae')
    set_default(false)
option_end()

option('skyrim_vr')
    set_default(true)
option_end()

target('DragonBoardVR')
    set_kind('shared')
    set_arch('x64')
    add_deps('commonlibsse-ng')
    add_options('skyrim_se', 'skyrim_ae', 'skyrim_vr')
    add_packages('simpleini', 'rmlui', 'nlohmann_json')
    add_syslinks('d3d11', 'd3dcompiler', 'windowscodecs', 'ole32', 'bcrypt')

    on_config(function ()
        if has_config('skyrim_se') or has_config('skyrim_ae') or not has_config('skyrim_vr') then
            raise('DragonBoardVR is VR-only: skyrim_vr must be enabled and SE/AE must be disabled.')
        end
    end)

    add_files('Src/**.cpp')
    add_files('Resources/DragonBoardVR.rc')
    add_headerfiles('Src/**.h')
    add_installfiles('Assets/ui/rml/*.rml', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/ui'
    })
    add_installfiles('Assets/ui/rml/*.rcss', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/ui'
    })
    add_installfiles('Assets/ui/rml/assets/(**)', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/ui/assets'
    })
    add_installfiles('Assets/ui/translations/*.json', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/translations'
    })
    add_installfiles('Assets/ui/translations/README.txt', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/translations'
    })
    add_installfiles('Assets/tools/DragonBoardIniScanner.exe', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/tools'
    })
    add_installfiles('Src/papyrus/*.psc', {
        prefixdir = 'Scripts/Source'
    })
    add_installfiles('Assets/scripts/DragonBoardVR.pex', {
        prefixdir = 'Scripts'
    })
    add_installfiles('Assets/integrations/spellwheel/SKSE/Plugins/SpellWheelVR_CustomConsoleCommand_DragonBoardVR.ini', {
        prefixdir = 'SKSE/Plugins'
    })
    add_installfiles('Assets/meshes/DragonBoardVR/RmlUIScreen.nif', {
        prefixdir = 'meshes/DragonBoardVR'
    })
    add_installfiles('Assets/meshes/DragonBoardVR/dragonboard.nif', {
        prefixdir = 'meshes/DragonBoardVR'
    })
    add_installfiles('Assets/meshes/DragonBoardVR/dragonboard_physical.nif', {
        prefixdir = 'meshes/DragonBoardVR'
    })
    add_installfiles('Assets/meshes/DragonBoardVR/dragonboard_vrik_proxy.nif', {
        prefixdir = 'meshes/DragonBoardVR'
    })
    add_installfiles('Assets/meshes/DragonBoardVR/dragonboard_vrik_proxy_hidden.nif', {
        prefixdir = 'meshes/DragonBoardVR'
    })
    add_installfiles('Assets/DragonBoardVR.esp')
    add_installfiles('Assets/meshes/DragonBoardVR/iconplane.nif', {
        prefixdir = 'meshes/DragonBoardVR'
    })
    add_installfiles('Assets/meshes/DragonBoardVR/DBMarker*.nif', {
        prefixdir = 'meshes/DragonBoardVR'
    })
    add_installfiles('Assets/textures/RmlUI0.dds', {
        prefixdir = 'textures'
    })
    add_installfiles('Assets/textures/DragonBoardMat_*.dds', {
        prefixdir = 'textures'
    })
    add_installfiles('Assets/textures/DBMarker*.dds', {
        prefixdir = 'textures'
    })
    add_includedirs(
        'Src',
        '$(projectdir)'
    )

    set_pcxxheader('Src/pch.h')

    add_defines('ENABLE_SKYRIM_VR')

    after_build(function (target)
        local installRoot = path.join(os.projectdir(), 'install_output')
        local outputDir = path.join(installRoot, 'SKSE', 'Plugins')
        os.mkdir(outputDir)
        local targetFile = target:targetfile()
        local installFile = path.join(outputDir, path.filename(targetFile))
        os.cp(targetFile, installFile)
        local screenNif = path.join(os.projectdir(), 'Assets', 'meshes', 'DragonBoardVR', 'RmlUIScreen.nif')
        local spellWheelNif = path.join(os.projectdir(), 'Assets', 'meshes', 'DragonBoardVR', 'dragonboard.nif')
        local physicalBoardNif = path.join(os.projectdir(), 'Assets', 'meshes', 'DragonBoardVR', 'dragonboard_physical.nif')
        local vrikProxyNif = path.join(os.projectdir(), 'Assets', 'meshes', 'DragonBoardVR', 'dragonboard_vrik_proxy.nif')
        local vrikProxyHiddenNif = path.join(os.projectdir(), 'Assets', 'meshes', 'DragonBoardVR', 'dragonboard_vrik_proxy_hidden.nif')
        local physicalBoardPlugin = path.join(os.projectdir(), 'Assets', 'DragonBoardVR.esp')
        local iconPlaneNif = path.join(os.projectdir(), 'Assets', 'meshes', 'DragonBoardVR', 'iconplane.nif')
        local categoryMarkerNifs = path.join(
            os.projectdir(), 'Assets', 'meshes', 'DragonBoardVR', 'DBMarker*.nif')
        local screenTexture = path.join(os.projectdir(), 'Assets', 'textures', 'RmlUI0.dds')
        local spellWheelTextures = path.join(os.projectdir(), 'Assets', 'textures', 'DragonBoardMat_*.dds')
        local categoryMarkerTextures = path.join(os.projectdir(), 'Assets', 'textures', 'DBMarker*.dds')
        os.mkdir(path.join(installRoot, 'meshes', 'DragonBoardVR'))
        os.mkdir(path.join(installRoot, 'meshes', 'Magic'))
        os.mkdir(path.join(installRoot, 'textures'))
        os.cp(screenNif, path.join(installRoot, 'meshes', 'DragonBoardVR', 'RmlUIScreen.nif'))
        os.vrunv('powershell', {
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', path.join(os.projectdir(), 'Tools', 'GenerateStatusScreen.ps1'),
            '-Source', screenNif,
            '-Destination', path.join(installRoot, 'meshes', 'DragonBoardVR', 'StatusScreen.nif')
        })
        os.vrunv('powershell', {
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', path.join(os.projectdir(), 'Tools', 'GenerateStatusScreen.ps1'),
            '-Source', screenNif,
            '-Destination', path.join(
                installRoot, 'meshes', 'DragonBoardVR', 'KeyboardScreen.nif'),
            '-Replacement', 'RmlUI2.dds'
        })
        os.vrunv('powershell', {
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', path.join(os.projectdir(), 'Tools', 'GenerateStatusScreen.ps1'),
            '-Source', screenNif,
            '-Destination', path.join(
                installRoot, 'meshes', 'DragonBoardVR', 'TutorialScreen.nif'),
            '-Replacement', 'RmlUI3.dds'
        })
        os.vrunv('powershell', {
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', path.join(os.projectdir(), 'Tools', 'GenerateStatusScreen.ps1'),
            '-Source', screenNif,
            '-Destination', path.join(
                installRoot, 'meshes', 'DragonBoardVR', 'WidgetLabelScreen.nif'),
            '-Replacement', 'RmlUI4.dds'
        })
        os.cp(spellWheelNif, path.join(installRoot, 'meshes', 'DragonBoardVR', 'dragonboard.nif'))
        os.cp(physicalBoardNif, path.join(installRoot, 'meshes', 'DragonBoardVR', 'dragonboard_physical.nif'))
        os.cp(vrikProxyNif, path.join(installRoot, 'meshes', 'DragonBoardVR', 'dragonboard_vrik_proxy.nif'))
        os.cp(vrikProxyHiddenNif, path.join(installRoot, 'meshes', 'DragonBoardVR', 'dragonboard_vrik_proxy_hidden.nif'))
        os.cp(physicalBoardPlugin, path.join(installRoot, 'DragonBoardVR.esp'))
        os.cp(iconPlaneNif, path.join(installRoot, 'meshes', 'DragonBoardVR', 'iconplane.nif'))
        os.cp(categoryMarkerNifs, path.join(installRoot, 'meshes', 'DragonBoardVR'))
        os.cp(screenTexture, path.join(installRoot, 'textures', 'RmlUI0.dds'))
        os.cp(screenTexture, path.join(installRoot, 'textures', 'RmlUI1.dds'))
        os.cp(screenTexture, path.join(installRoot, 'textures', 'RmlUI2.dds'))
        os.cp(screenTexture, path.join(installRoot, 'textures', 'RmlUI3.dds'))
        os.cp(screenTexture, path.join(installRoot, 'textures', 'RmlUI4.dds'))
        os.cp(spellWheelTextures, path.join(installRoot, 'textures'))
        os.cp(categoryMarkerTextures, path.join(installRoot, 'textures'))
        local rmlUiDir = path.join(os.projectdir(), 'Assets', 'ui', 'rml')
        local installedRmlUiDir = path.join(outputDir, 'DragonBoardVR', 'ui')
        os.mkdir(installedRmlUiDir)
        local function copyMatchingFiles(pattern, destination)
            for _, sourceFile in ipairs(os.files(pattern)) do
                os.cp(sourceFile, path.join(destination, path.filename(sourceFile)))
            end
        end
        copyMatchingFiles(path.join(rmlUiDir, '*.rml'), installedRmlUiDir)
        copyMatchingFiles(path.join(rmlUiDir, '*.rcss'), installedRmlUiDir)
        local rmlUiAssetsDir = path.join(rmlUiDir, 'assets')
        local installedRmlUiAssetsDir = path.join(installedRmlUiDir, 'assets')
        if os.isdir(installedRmlUiAssetsDir) then
            os.rm(installedRmlUiAssetsDir)
        end
        os.mkdir(installedRmlUiAssetsDir)
        os.cp(path.join(rmlUiAssetsDir, '**'), installedRmlUiAssetsDir, {
            rootdir = rmlUiAssetsDir
        })
        local translationDir = path.join(os.projectdir(), 'Assets', 'ui', 'translations')
        local installedTranslationDir = path.join(outputDir, 'DragonBoardVR', 'translations')
        os.mkdir(installedTranslationDir)
        os.cp(path.join(translationDir, '*.json'), installedTranslationDir)
        os.cp(path.join(translationDir, 'README.txt'), installedTranslationDir)
        local iniScanner = path.join(
            os.projectdir(), 'Assets', 'tools', 'DragonBoardIniScanner.exe')
        local installedToolsDir = path.join(outputDir, 'DragonBoardVR', 'tools')
        if os.isfile(iniScanner) then
            os.mkdir(installedToolsDir)
            os.cp(iniScanner, path.join(installedToolsDir, 'DragonBoardIniScanner.exe'))
        else
            cprint('${yellow}INI scanner executable not found:${clear} %s', iniScanner)
        end
        local papyrusSourceDir = path.join(installRoot, 'Scripts', 'Source')
        os.mkdir(papyrusSourceDir)
        os.cp(path.join(os.projectdir(), 'Src', 'papyrus', '*.psc'), papyrusSourceDir)
        local papyrusPex = path.join(os.projectdir(), 'Assets', 'scripts', 'DragonBoardVR.pex')
        if os.isfile(papyrusPex) then
            local papyrusRuntimeDir = path.join(installRoot, 'Scripts')
            os.mkdir(papyrusRuntimeDir)
            os.cp(papyrusPex, path.join(papyrusRuntimeDir, 'DragonBoardVR.pex'))
        else
            cprint('${yellow}Papyrus runtime stub not found:${clear} %s', papyrusPex)
        end
        local spellWheelIni = path.join(
            os.projectdir(),
            'Assets',
            'integrations',
            'spellwheel',
            'SKSE',
            'Plugins',
            'SpellWheelVR_CustomConsoleCommand_DragonBoardVR.ini')
        os.cp(spellWheelIni, path.join(
            installRoot,
            'SKSE',
            'Plugins',
            'SpellWheelVR_CustomConsoleCommand_DragonBoardVR.ini'))
        local configNames = {
            'DragonBoardVR.ini',
            'DragonBoardVR_Layout.ini',
            'DragonBoardVR_State.ini',
            'DragonBoardVR_Layout.json'
        }
        local defaultsDir = path.join(installRoot, 'SKSE', 'Plugins', 'DragonBoardVR', 'Defaults')
        os.mkdir(defaultsDir)
        for _, configName in ipairs(configNames) do
            local configSource = path.join(os.projectdir(), 'Assets', 'config', configName)
            local configDestination = path.join(outputDir, configName)
            if not os.isfile(configDestination) then
                os.cp(configSource, configDestination)
                cprint('${green}seeded config:${clear} %s', configDestination)
            else
                cprint('${yellow}preserved existing config:${clear} %s', configDestination)
            end
            os.cp(configSource, path.join(defaultsDir, configName))
        end
        cprint('${green}synced install_output:${clear} %s', installFile)
    end)

target('DragonBoardRmlPreview')
    set_kind('binary')
    set_default(false)
    set_rundir('$(projectdir)')

    add_packages('rmlui', 'nlohmann_json')
    add_links('rmlui_debugger')
    add_syslinks('d3d11', 'd3dcompiler', 'dxgi', 'user32', 'gdi32', 'shell32', 'comdlg32', 'comctl32', 'windowscodecs', 'ole32')

    add_files(
        'Tools/RmlPreview/main.cpp',
        'Tools/RmlPreview/RmlSourceEditor.cpp',
        'Tools/RmlPreview/RmlVisualInspector.cpp',
        'Src/ui/rml/LocalizationManager.cpp',
        'Src/ui/rml/D3D11StateGuard.cpp',
        'Src/ui/rml/DragonBoardRmlRenderer.cpp',
        'Src/ui/rml/RmlPerformanceMetrics.cpp',
        'Src/ui/rml/RmlVirtualList.cpp'
    )
    add_includedirs('Src', 'Tools/RmlPreview')
    set_pcxxheader('Tools/RmlPreview/pch.h')

target('RmlVirtualListTests')
    set_kind('binary')
    set_default(false)
    add_files(
        'Tools/RmlPreview/RmlVirtualListTests.cpp',
        'Src/ui/rml/RmlVirtualList.cpp'
    )
    add_includedirs('Src')

target('RmlEntranceAnimationTests')
    set_kind('binary')
    set_default(false)
    add_files(
        'Tools/RmlPreview/RmlEntranceAnimationTests.cpp',
        'Src/ui/rml/RmlEntranceAnimation.cpp'
    )
    add_includedirs('Src')

target('LocalizationManagerTests')
    set_kind('binary')
    set_default(false)
    add_deps('commonlibsse-ng')
    add_files(
        'Tools/RmlPreview/LocalizationManagerTests.cpp',
        'Src/ui/rml/LocalizationManager.cpp'
    )
    add_includedirs(
        'Src',
        '$(projectdir)'
    )
    set_pcxxheader('Src/pch.h')
    add_defines('ENABLE_SKYRIM_VR')

target('IniEditorTests')
    set_kind('binary')
    set_default(false)
    add_files(
        'Tools/IniScanner/IniEditorTests.cpp',
        'Src/ui/mods/IniCatalog.cpp',
        'Src/ui/mods/IniFileWriter.cpp'
    )
    add_includedirs('Src')
    add_syslinks('bcrypt')
