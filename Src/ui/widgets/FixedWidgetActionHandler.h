#pragma once

#include "vrui/VRUISettings.h"
#include "vrui/VRUIWidget.h"

namespace dragonboard::ui::widgets
{
    class FixedWidgetActionHandler
    {
    public:
        static void Execute(const vrui::FixedWidgetItem& data, vrui::EquipHand hand);
    };
}
