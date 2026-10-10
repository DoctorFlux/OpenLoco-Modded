#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OpenLoco::Modding
{
    // The manifest schema understood by this loader. This is not a save-game version.
    inline constexpr uint32_t kManifestSchemaVersion = 1;

    // Mods built against this API may load. See docs/MODDING_API.md for the compatibility rule.
    inline constexpr std::string_view kModApiVersion = "0.1.0";

    struct Version
    {
        uint32_t major{};
        uint32_t minor{};
        uint32_t patch{};
    };

    struct Manifest
    {
        std::string id;
        std::string name;
        std::string author;
        std::string version;
        std::string engineApiVersion;
        std::vector<std::string> dependencies;
        std::vector<std::string> optionalDependencies;
        std::vector<std::string> conflicts;
        std::vector<std::string> capabilities;
        std::vector<std::string> platforms;
        std::string description;
        std::string license;
        std::vector<std::string> entryPoints;
        uint32_t stateSchemaVersion{};
    };

    struct ParseFailure
    {
        std::string message;
    };

    std::optional<Version> parseVersion(std::string_view text);

    // major.minor must match. The running patch must be greater than or equal to the required patch.
    bool isApiCompatible(Version running, Version required);

    // Parses one manifest.json document. Unknown keys are ignored. Nested objects are rejected.
    std::optional<ParseFailure> parseManifest(std::string_view json, Manifest& out);
}
