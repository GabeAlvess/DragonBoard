#include "ui/rml/LocalizationManager.h"

#include <algorithm>
#include <iostream>

using dragonboard::ui::rml::LocalizationManager;

int main()
{
    const auto languages = LocalizationManager::SupportedLanguages();
    const auto dropIn = std::ranges::find_if(languages, [](const auto& language) {
        return language.code == "zz-Test";
    });
    if (dropIn == languages.end() || dropIn->nativeName != "Drop-in Test") {
        std::cerr << "drop-in catalog was not discovered" << std::endl;
        return 1;
    }
    if (LocalizationManager::NormalizeCode("zz") != "zz-Test" ||
        LocalizationManager::NormalizeCode("test-language") != "zz-Test" ||
        LocalizationManager::NativeName("zz-Test") != "Drop-in Test") {
        std::cerr << "drop-in metadata or aliases were not resolved" << std::endl;
        return 2;
    }

    LocalizationManager localization;
    if (!localization.Load("zz-Test") || localization.ActiveCode() != "zz-Test" ||
        localization.Translate("Drop-in source") != "Drop-in translated") {
        std::cerr << "drop-in translations were not loaded" << std::endl;
        return 3;
    }

    const auto next = LocalizationManager::NextCode("zz-Test");
    const auto previous = LocalizationManager::PreviousCode(next);
    if (previous != "zz-Test") {
        std::cerr << "drop-in language was not included in navigation" << std::endl;
        return 4;
    }

    std::cout << "LocalizationManager drop-in test passed" << std::endl;
    return 0;
}
