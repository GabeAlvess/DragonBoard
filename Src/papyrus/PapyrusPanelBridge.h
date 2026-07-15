#pragma once

namespace RE::BSScript
{
    class IVirtualMachine;
}

namespace dragonboard::papyrus
{
    bool RegisterPanelFunctions(RE::BSScript::IVirtualMachine* vm);
    void ResetPapyrusPanels();
}
