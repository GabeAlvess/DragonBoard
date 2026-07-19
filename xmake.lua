set_xmakever('3.0.1')
includes('lib/commonlibsse-ng')

set_project('DragonBoardVR')
set_version('1.0.0')
set_license('GPL-3.0')

set_languages('c++23')
set_warnings('all')
set_policy('package.requires_lock', true)
set_toolset('msvc', 'ninja')

add_rules('mode.debug', 'mode.releasedbg', 'mode.release')

add_requires('rmlui 6.2', { configs = { shared = false, lua = false, svg = false, lottie = false } })

-- CommonLibSSE-NG enables SE, AE, and VR by default. Override those dependency
-- options here so a plain `xmake f` cannot accidentally produce a cross-runtime
-- ABI for this VR-only plugin.
option('skyrim_se')
    set_default(false)
    set_showmenu(false)
option_end()

option('skyrim_ae')
    set_default(false)
    set_showmenu(false)
option_end()

option('skyrim_vr')
    set_default(true)
    set_showmenu(false)
option_end()

target('DragonBoardVR')
    add_deps('commonlibsse-ng')
    add_packages('rmlui')
    add_syslinks('d3d11', 'd3dcompiler', 'windowscodecs', 'ole32')

    on_config(function ()
        if has_config('skyrim_se') or has_config('skyrim_ae') or not has_config('skyrim_vr') then
            raise('DragonBoardVR is VR-only: skyrim_vr must be enabled and SE/AE must be disabled.')
        end
    end)

    add_rules('commonlibsse-ng.plugin', {
        name        = 'DragonBoardVR',
        author      = 'GabeAlvz',
        description = 'VR Menu Framework - Interactive menus on non-dominant hand with raycast activation.',
        runtime     = 'vr'
    })

    add_files('Src/**.cpp')
    add_headerfiles('Src/**.h')
    add_installfiles('Assets/ui/rml/*.rml', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/ui'
    })
    add_installfiles('Assets/ui/rml/*.rcss', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/ui'
    })
    add_installfiles('Assets/ui/rml/assets/*.png', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/ui/assets'
    })
    add_installfiles('Assets/ui/rml/assets/*.jpeg', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/ui/assets'
    })
    add_installfiles('Assets/ui/rml/assets/*.ttf', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/ui/assets'
    })
    add_installfiles('Src/papyrus/*.psc', {
        prefixdir = 'Scripts/Source'
    })
    add_installfiles('Assets/scripts/DragonBoardVR.pex', {
        prefixdir = 'Scripts'
    })
    add_installfiles('Assets/meshes/DragonBoardVR/ImGuiScreen.nif', {
        prefixdir = 'meshes/DragonBoardVR'
    })
    add_installfiles('Assets/meshes/DragonBoardVR/dragonboard.nif', {
        prefixdir = 'meshes/DragonBoardVR'
    })
    add_installfiles('Assets/textures/ImGui0.dds', {
        prefixdir = 'textures'
    })
    add_installfiles('Assets/textures/DragonBoardMat_*.dds', {
        prefixdir = 'textures'
    })
    add_includedirs(
        'Src',
        '$(projectdir)',
        '$(projectdir)/ClibUtil/include',
        '$(projectdir)/xbyak',
        '$(projectdir)/simpleini',
        '$(projectdir)/lib'
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
        local screenNif = path.join(os.projectdir(), 'Assets', 'meshes', 'DragonBoardVR', 'ImGuiScreen.nif')
        local spellWheelNif = path.join(os.projectdir(), 'Assets', 'meshes', 'DragonBoardVR', 'dragonboard.nif')
        local screenTexture = path.join(os.projectdir(), 'Assets', 'textures', 'ImGui0.dds')
        local spellWheelTextures = path.join(os.projectdir(), 'Assets', 'textures', 'DragonBoardMat_*.dds')
        os.mkdir(path.join(installRoot, 'meshes', 'DragonBoardVR'))
        os.mkdir(path.join(installRoot, 'textures'))
        os.cp(screenNif, path.join(installRoot, 'meshes', 'DragonBoardVR', 'ImGuiScreen.nif'))
        os.cp(spellWheelNif, path.join(installRoot, 'meshes', 'DragonBoardVR', 'dragonboard.nif'))
        os.cp(screenTexture, path.join(installRoot, 'textures', 'ImGui0.dds'))
        os.cp(spellWheelTextures, path.join(installRoot, 'textures'))
        local rmlUiDir = path.join(os.projectdir(), 'Assets', 'ui', 'rml')
        local installedRmlUiDir = path.join(outputDir, 'DragonBoardVR', 'ui')
        os.mkdir(installedRmlUiDir)
        os.cp(path.join(rmlUiDir, '*.rml'), installedRmlUiDir)
        os.cp(path.join(rmlUiDir, '*.rcss'), installedRmlUiDir)
        local installedRmlUiAssetsDir = path.join(installedRmlUiDir, 'assets')
        os.mkdir(installedRmlUiAssetsDir)
        os.cp(path.join(rmlUiDir, 'assets', '*.png'), installedRmlUiAssetsDir)
        os.cp(path.join(rmlUiDir, 'assets', '*.jpeg'), installedRmlUiAssetsDir)
        os.cp(path.join(rmlUiDir, 'assets', '*.ttf'), installedRmlUiAssetsDir)
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
        cprint('${green}synced install_output:${clear} %s', installFile)
    end)

target('DragonBoardRmlPreview')
    set_kind('binary')
    set_default(false)
    set_rundir('$(projectdir)')

    add_packages('rmlui')
    add_links('rmlui_debugger')
    add_syslinks('d3d11', 'd3dcompiler', 'dxgi', 'user32', 'gdi32', 'shell32', 'comdlg32', 'comctl32', 'windowscodecs', 'ole32')

    add_files(
        'Tools/RmlPreview/main.cpp',
        'Tools/RmlPreview/RmlSourceEditor.cpp',
        'Tools/RmlPreview/RmlVisualInspector.cpp',
        'Src/ui/rml/DragonBoardRmlRenderer.cpp'
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
