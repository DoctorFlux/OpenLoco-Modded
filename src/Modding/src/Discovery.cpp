#include "OpenLoco/Modding/Discovery.hpp"

#include <fstream>

namespace OpenLoco::Modding
{
    namespace
    {
        constexpr std::uintmax_t kMaxManifestBytes = 1024 * 1024;

        std::optional<std::string> readText(const std::filesystem::path& path, std::string& error)
        {
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            if (ec)
            {
                error = "could not read " + path.string();
                return std::nullopt;
            }
            if (size > kMaxManifestBytes)
            {
                error = path.string() + " is larger than 1 MiB";
                return std::nullopt;
            }
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                error = "could not read " + path.string();
                return std::nullopt;
            }
            std::string text(static_cast<size_t>(size), '\0');
            stream.read(text.data(), static_cast<std::streamsize>(size));
            if (!stream)
            {
                error = "could not read " + path.string();
                return std::nullopt;
            }
            return text;
        }

        bool platformAllowed(const Manifest& manifest)
        {
            if (manifest.platforms.empty())
            {
                return true;
            }
            for (const auto& platform : manifest.platforms)
            {
                if (platform == kHostPlatform)
                {
                    return true;
                }
            }
            return false;
        }

        bool loadOne(const std::filesystem::path& root, LoadResult& result)
        {
            const auto manifestPath = root / "manifest.json";
            std::string error;
            const auto text = readText(manifestPath, error);
            if (!text)
            {
                result.errors.push_back(error);
                return false;
            }
            Manifest manifest;
            if (const auto failure = parseManifest(*text, manifest))
            {
                result.errors.push_back(manifestPath.string() + ": " + failure->message);
                return false;
            }
            if (!platformAllowed(manifest))
            {
                result.errors.push_back(manifest.id + " does not support platform \"" + std::string(kHostPlatform) + "\"");
                return false;
            }
            const auto running = parseVersion(kModApiVersion);
            const auto required = parseVersion(manifest.engineApiVersion);
            if (!running || !required || !isApiCompatible(*running, *required))
            {
                result.errors.push_back(manifest.id + " requires engine API " + manifest.engineApiVersion + " but this build provides " + std::string(kModApiVersion));
                return false;
            }
            result.mods.push_back(DiscoveredMod{ std::move(manifest), root });
            return true;
        }
    }

    LoadResult loadModDirectory(const std::filesystem::path& directory)
    {
        LoadResult result;
        std::error_code ec;
        if (!std::filesystem::is_directory(directory, ec))
        {
            result.errors.push_back(directory.string() + " is not a directory");
            return result;
        }

        const auto ownManifest = directory / "manifest.json";
        if (std::filesystem::is_regular_file(ownManifest))
        {
            loadOne(directory, result);
        }
        else
        {
            for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
            {
                if (ec)
                {
                    result.errors.push_back("could not list " + directory.string());
                    break;
                }
                std::error_code linkEc;
                if (std::filesystem::is_symlink(entry.symlink_status(linkEc)))
                {
                    result.errors.push_back("skipping symbolic link " + entry.path().string());
                    continue;
                }
                if (!entry.is_directory())
                {
                    continue;
                }
                loadOne(entry.path(), result);
            }
        }

        if (!result.errors.empty())
        {
            return result;
        }

        std::vector<Manifest> manifests;
        manifests.reserve(result.mods.size());
        for (const auto& mod : result.mods)
        {
            manifests.push_back(mod.manifest);
        }
        const auto resolved = resolveDependencies(manifests);
        result.loadOrder = resolved.loadOrder;
        result.errors = resolved.errors;
        if (!result.errors.empty())
        {
            result.loadOrder.clear();
        }
        return result;
    }
}
