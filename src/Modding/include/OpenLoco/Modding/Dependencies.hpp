#pragma once

#include "Manifest.hpp"
#include <string>
#include <vector>

namespace OpenLoco::Modding
{
    struct ResolveResult
    {
        // Mod ids in dependency-first order. Empty when errors is not empty.
        std::vector<std::string> loadOrder;
        std::vector<std::string> errors;
    };

    // Every manifest in the list is treated as requested for activation.
    ResolveResult resolveDependencies(const std::vector<Manifest>& manifests);
}
