#pragma once

namespace dragonboard::ui::menu
{
    [[nodiscard]] bool IsCreated();
    void Create();
    void Recreate();
    void SetDeveloperButtonVisible(bool visible);
}
