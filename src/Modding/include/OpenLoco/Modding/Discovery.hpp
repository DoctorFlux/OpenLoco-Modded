#pragma once

#include "Dependencies.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace OpenLoco::Modding
{
    inline constexpr std::string_view kHostPlatform =
#if defined(_WIN32)
        "windows"
#elif defined(__APPLE__)
        "macos"
#else
        "linux"
#endif
        ;

    struct DiscoveredMod
    {
        Manifest manifest;
        std::filesystem::path root;
    };

    struct LoadResult
    {
        std::vector<DiscoveredMod> mods;
        std::vector<std::string> loadOrder;
        std::vector<std::string> errors;
    };

    // Reads an unpacked mod directory, or a directory of unpacked mods.
    // Does not open archives, run scripts, or change game state.
    LoadResult loadModDirectory(const std::filesystem::path& directory);
}
