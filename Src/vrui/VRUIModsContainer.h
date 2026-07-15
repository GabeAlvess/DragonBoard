#pragma once
#include "VRUIDynamicContainer.h"
#include <string>

namespace vrui {

    class VRUIModsContainer : public VRUIDynamicContainer {
    public:
        explicit VRUIModsContainer(const std::string& name);
        virtual ~VRUIModsContainer() = default;

        void refresh() override;
    };

}
