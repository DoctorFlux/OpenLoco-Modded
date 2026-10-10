#include <OpenLoco/Modding/Discovery.hpp>
#include <fstream>
#include <gtest/gtest.h>

using namespace OpenLoco::Modding;

namespace
{
    void writeManifest(const std::filesystem::path& directory, std::string_view id, std::string_view dependencies = "[]")
    {
        std::filesystem::create_directories(directory);
        std::ofstream stream(directory / "manifest.json", std::ios::binary);
        stream << "{\n"
               << "  \"id\": \"" << id << "\",\n"
               << "  \"name\": \"" << id << "\",\n"
               << "  \"version\": \"0.1.0\",\n"
               << "  \"engineApiVersion\": \"0.1.0\",\n"
               << "  \"dependencies\": " << dependencies << ",\n"
               << "  \"stateSchemaVersion\": 1\n"
               << "}\n";
    }
}

TEST(DiscoveryTests, loadsDirectoryOfModsInOrder)
{
    const auto root = std::filesystem::temp_directory_path() / "openloco-modding-discovery-order";
    std::filesystem::remove_all(root);
    writeManifest(root / "lines", "openloco.sample.lines", "[\"openloco.sample.logic\"]");
    writeManifest(root / "logic", "openloco.sample.logic");

    const auto result = loadModDirectory(root);
    std::filesystem::remove_all(root);

    ASSERT_TRUE(result.errors.empty()) << result.errors.front();
    ASSERT_EQ(result.mods.size(), 2U);
    ASSERT_EQ(result.loadOrder.size(), 2U);
    EXPECT_EQ(result.loadOrder[0], "openloco.sample.logic");
    EXPECT_EQ(result.loadOrder[1], "openloco.sample.lines");
}

TEST(DiscoveryTests, loadsSingleModRoot)
{
    const auto root = std::filesystem::temp_directory_path() / "openloco-modding-discovery-single";
    std::filesystem::remove_all(root);
    writeManifest(root, "openloco.sample.hello");

    const auto result = loadModDirectory(root);
    std::filesystem::remove_all(root);

    ASSERT_TRUE(result.errors.empty()) << result.errors.front();
    ASSERT_EQ(result.mods.size(), 1U);
    EXPECT_EQ(result.mods[0].manifest.id, "openloco.sample.hello");
}

TEST(DiscoveryTests, reportsInvalidManifest)
{
    const auto root = std::filesystem::temp_directory_path() / "openloco-modding-discovery-invalid";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "broken");
    std::ofstream stream(root / "broken" / "manifest.json", std::ios::binary);
    stream << "{}\n";
    stream.close();

    const auto result = loadModDirectory(root);
    std::filesystem::remove_all(root);

    EXPECT_TRUE(result.mods.empty());
    EXPECT_TRUE(result.loadOrder.empty());
    ASSERT_FALSE(result.errors.empty());
}

TEST(DiscoveryTests, rejectsOversizedManifest)
{
    const auto root = std::filesystem::temp_directory_path() / "openloco-modding-discovery-oversize";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream stream(root / "manifest.json", std::ios::binary);
    stream << std::string(1024 * 1024 + 1, ' ');
    stream.close();

    const auto result = loadModDirectory(root);
    std::filesystem::remove_all(root);

    EXPECT_TRUE(result.mods.empty());
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].find("1 MiB"), std::string::npos);
}

TEST(DiscoveryTests, rejectsIncompatibleApi)
{
    const auto root = std::filesystem::temp_directory_path() / "openloco-modding-discovery-api";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream stream(root / "manifest.json", std::ios::binary);
    stream << R"({"id":"openloco.sample.future","name":"Future","version":"0.1.0","engineApiVersion":"9.0.0","stateSchemaVersion":1})";
    stream.close();

    const auto result = loadModDirectory(root);
    std::filesystem::remove_all(root);

    EXPECT_TRUE(result.mods.empty());
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].find("9.0.0"), std::string::npos);
}
