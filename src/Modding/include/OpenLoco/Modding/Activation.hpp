#pragma once

#include "Discovery.hpp"
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace OpenLoco::Modding
{
    enum class ModLifecycle
    {
        discovered,
        active,
        failed,
    };

    struct ActivationRecord
    {
        std::string id;
        ModLifecycle state = ModLifecycle::discovered;
        std::vector<std::string> messages;
        std::string error;
    };

    struct ActivationResult
    {
        std::vector<ActivationRecord> records;
        std::vector<std::string> errors;
    };

    struct HostCallbacks
    {
        std::function<void(std::string_view message)> onInfo;
        std::function<void(std::string_view message)> onError;
    };

    // Discovers mods, activates only ids listed in enabled.json, and runs log actions.
    // Does not change the simulation, read saves, or load native code.
    ActivationResult activateMods(const std::filesystem::path& directory, const HostCallbacks& callbacks = {});
}
