#pragma once

#include <RE/N/NiNode.h>
#include <RE/N/NiSmartPointer.h>
#include <RE/N/NiTransform.h>

namespace RE
{
    struct VR_NODE_DATA;
}

namespace vrui
{
    class JournalMenuProbe
    {
    public:
        static JournalMenuProbe& GetSingleton();

        // Arms one reversible probe using the DragonBoard's current world position.
        // The probe is consumed only by the next Journal Menu opened from the board.
        void ArmFromDragonBoard();
        void Update();
        void Reset();

    private:
        void LogVrNodes(RE::VR_NODE_DATA& nodes) const;
        void ApplyTranslationOnly(RE::NiNode& uiNode);
        void Restore();

        bool _armed = false;
        bool _journalWasOpen = false;
        bool _nodesLogged = false;
        bool _targetAvailable = false;
        bool _overwriteLogged = false;
        std::uint32_t _framesWaitingForJournal = 0;
        RE::NiTransform _targetWorld;
        RE::NiTransform _originalLocal;
        RE::NiTransform _lastAppliedLocal;
        RE::NiPointer<RE::NiNode> _activeUiNode;
    };
}
