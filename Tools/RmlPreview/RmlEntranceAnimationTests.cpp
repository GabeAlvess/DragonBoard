#include "ui/rml/RmlEntranceAnimation.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using dragonboard::ui::rml::RmlEntranceAnimation;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    void RequireNear(float actual, float expected, const char* message)
    {
        if (std::abs(actual - expected) > 0.0001f) {
            throw std::runtime_error(message);
        }
    }
}

int main()
{
    RmlEntranceAnimation animation;
    Require(!animation.Configure(true, 0.25f, 0.10f), "default configuration changed the frame");

    animation.Start();
    Require(animation.IsActive(), "enabled animation did not start");
    RequireNear(animation.GetProgress(), 0.0f, "animation did not start fully hidden");
    RequireNear(animation.GetFeather(), 0.10f, "configured feather was not retained");

    Require(animation.Advance(0.125f), "half-duration advance did not change progress");
    RequireNear(animation.GetProgress(), 0.875f, "entrance did not use cubic ease-out");
    Require(animation.IsActive(), "animation ended before its duration");

    Require(animation.Advance(0.125f), "final advance did not change progress");
    RequireNear(animation.GetProgress(), 1.0f, "animation did not finish fully visible");
    Require(!animation.IsActive(), "animation remained active after completion");
    Require(!animation.Advance(0.1f), "completed animation kept changing");

    animation.Start();
    (void)animation.Advance(0.05f);
    Require(
        animation.Configure(false, 0.25f, 0.10f),
        "disabling an active animation did not invalidate the frame");
    RequireNear(animation.GetProgress(), 1.0f, "disabled animation did not become fully visible");
    Require(!animation.IsActive(), "disabled animation remained active");

    animation.Start();
    RequireNear(animation.GetProgress(), 1.0f, "disabled animation restarted");
    Require(!animation.IsActive(), "disabled animation reported active after restart");

    Require(!animation.Configure(true, 0.25f, 0.10f), "reenabling changed a settled frame");
    animation.Start();
    Require(!animation.Advance(-1.0f), "negative delta changed progress");
    RequireNear(animation.GetProgress(), 0.0f, "negative delta advanced the animation");
    Require(
        !animation.Advance(std::numeric_limits<float>::quiet_NaN()),
        "non-finite delta changed progress");
    RequireNear(animation.GetProgress(), 0.0f, "non-finite delta corrupted progress");

    Require(
        !animation.Configure(
            true,
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::quiet_NaN()),
        "invalid active configuration changed equivalent safe defaults");
    RequireNear(animation.GetFeather(), 0.10f, "invalid feather did not use its default");
    Require(animation.Advance(0.125f), "default duration did not advance");
    RequireNear(animation.GetProgress(), 0.875f, "invalid duration did not use its default");

    std::cout << "RmlUi entrance animation tests passed.\n";
    return 0;
}
