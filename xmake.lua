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

add_requires('imgui', { configs = { dx11 = true } })
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
    add_packages('imgui', 'rmlui')
    add_syslinks('d3d11', 'd3dcompiler')

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
    add_files('External/ImGuiVRHelper/api/ImGuiVRHelperAPI.cpp')
    add_headerfiles('Src/**.h')
    add_installfiles('Assets/ui/rml/*.rml', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/ui'
    })
    add_installfiles('Assets/ui/rml/*.rcss', {
        prefixdir = 'SKSE/Plugins/DragonBoardVR/ui'
    })
    add_installfiles('Assets/meshes/DragonBoardVR/ImGuiScreen.nif', {
        prefixdir = 'meshes/DragonBoardVR'
    })
    add_installfiles('Assets/textures/ImGui0.dds', {
        prefixdir = 'textures'
    })

    add_includedirs(
        'Src',
        '$(projectdir)',
        '$(projectdir)/ClibUtil/include',
        '$(projectdir)/xbyak',
        '$(projectdir)/simpleini',
        '$(projectdir)/lib',
        '$(projectdir)/External/ImGuiVRHelper/api'
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
        local screenTexture = path.join(os.projectdir(), 'Assets', 'textures', 'ImGui0.dds')
        os.mkdir(path.join(installRoot, 'meshes', 'DragonBoardVR'))
        os.mkdir(path.join(installRoot, 'textures'))
        os.cp(screenNif, path.join(installRoot, 'meshes', 'DragonBoardVR', 'ImGuiScreen.nif'))
        os.cp(screenTexture, path.join(installRoot, 'textures', 'ImGui0.dds'))
        local rmlUiDir = path.join(os.projectdir(), 'Assets', 'ui', 'rml')
        local installedRmlUiDir = path.join(outputDir, 'DragonBoardVR', 'ui')
        os.mkdir(installedRmlUiDir)
        os.cp(path.join(rmlUiDir, '*.rml'), installedRmlUiDir)
        os.cp(path.join(rmlUiDir, '*.rcss'), installedRmlUiDir)
        cprint('${green}synced install_output:${clear} %s', installFile)
    end)

target('DragonBoardRmlPreview')
    set_kind('binary')
    set_default(false)
    set_rundir('$(projectdir)')

    add_packages('rmlui')
    add_links('rmlui_debugger')
    add_syslinks('d3d11', 'd3dcompiler', 'dxgi', 'user32', 'gdi32', 'shell32', 'comdlg32', 'comctl32')

    add_files(
        'Tools/RmlPreview/main.cpp',
        'Tools/RmlPreview/RmlVisualInspector.cpp',
        'Src/ui/rml/DragonBoardRmlRenderer.cpp'
    )
    add_includedirs('Src', 'Tools/RmlPreview')
    set_pcxxheader('Tools/RmlPreview/pch.h')
