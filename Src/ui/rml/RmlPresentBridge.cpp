#include "pch.h"

#include "ui/rml/RmlPresentBridge.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <Windows.h>

#include <dxgi.h>
#include <RE/R/Renderer.h>

namespace dragonboard::ui::rml
{
    namespace
    {
        using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);

        PresentFn g_originalPresent = nullptr;
        RmlPresentBridge::PresentCallback g_presentCallback = nullptr;
        std::chrono::steady_clock::time_point g_lastPresent;
        bool g_lastPresentValid = false;

        HRESULT WINAPI RmlPresent(
            IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
        {
            const auto now = std::chrono::steady_clock::now();
            float deltaTime = 1.0f / 90.0f;
            if (g_lastPresentValid) {
                deltaTime = std::clamp(
                    std::chrono::duration<float>(now - g_lastPresent).count(),
                    1.0f / 240.0f,
                    0.1f);
            }
            g_lastPresent = now;
            g_lastPresentValid = true;

            try {
                if (g_presentCallback) g_presentCallback(deltaTime);
            } catch (const std::exception& e) {
                logger::error("DragonBoardVR: RmlUi Present exception: {}", e.what());
            }
            return g_originalPresent(swapChain, syncInterval, flags);
        }
    }

    bool RmlPresentBridge::Install(PresentCallback callback)
    {
        if (g_originalPresent) {
            if (callback) g_presentCallback = callback;
            return true;
        }
        if (!callback) return false;

        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer) return false;
        auto& rendererData = renderer->GetRuntimeData();
        auto* swapChain = reinterpret_cast<IDXGISwapChain*>(
            rendererData.renderWindows[0].swapChain);
        if (!swapChain) return false;

        auto** vtable = *reinterpret_cast<void***>(swapChain);
        if (!vtable || !vtable[8]) return false;

        DWORD oldProtect = 0;
        if (!VirtualProtect(&vtable[8], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
            return false;
        }
        g_presentCallback = callback;
        g_originalPresent = reinterpret_cast<PresentFn>(vtable[8]);
        vtable[8] = reinterpret_cast<void*>(&RmlPresent);
        DWORD ignored = 0;
        VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), &vtable[8], sizeof(void*));

        logger::info(
            "DragonBoardVR: RmlUi Present hook installed (next={}).",
            reinterpret_cast<void*>(g_originalPresent));
        return true;
    }

    bool RmlPresentBridge::IsInstalled()
    {
        return g_originalPresent != nullptr;
    }
}
