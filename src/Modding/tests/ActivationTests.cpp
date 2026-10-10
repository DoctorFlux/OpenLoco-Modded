#include <OpenLoco/Modding/Activation.hpp>
#include <fstream>
#include <gtest/gtest.h>

using namespace OpenLoco::Modding;

namespace
{
    void writeFile(const std::filesystem::path& path, std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << text;
    }

    std::filesystem::path makeRoot(const char* name)
    {
        const auto root = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        return root;
    }
}

TEST(ActivationTests, disabledModDoesNotRun)
{
    const auto root = makeRoot("openloco-modding-activation-disabled");
    writeFile(root / "hello" / "manifest.json", R"({
        "id": "openloco.sample.hello",
        "name": "Hello",
        "version": "0.1.0",
        "engineApiVersion": "0.1.0",
        "entryPoints": ["scripts/activate.json"],
        "stateSchemaVersion": 1
    })");
    writeFile(root / "hello" / "scripts" / "activate.json", R"({"log":["should not run"]})");

    std::vector<std::string> info;
    const auto result = activateMods(root, HostCallbacks{ [&](std::string_view message) { info.emplace_back(message); }, {} });
    std::filesystem::remove_all(root);

    ASSERT_TRUE(result.errors.empty()) << result.errors.front();
    ASSERT_EQ(result.records.size(), 1U);
    EXPECT_EQ(result.records[0].state, ModLifecycle::discovered);
    EXPECT_TRUE(info[0].find("not enabled") != std::string::npos);
}

TEST(ActivationTests, enabledModLogsMessage)
{
    const auto root = makeRoot("openloco-modding-activation-enabled");
    writeFile(root / "enabled.json", R"({"enabled":["openloco.sample.hello"]})");
    writeFile(root / "hello" / "manifest.json", R"({
        "id": "openloco.sample.hello",
        "name": "Hello",
        "version": "0.1.0",
        "engineApiVersion": "0.1.0",
        "entryPoints": ["scripts/activate.json"],
        "stateSchemaVersion": 1
    })");
    writeFile(root / "hello" / "scripts" / "activate.json", R"({"log":["Hello World"]})");

    std::vector<std::string> info;
    const auto result = activateMods(root, HostCallbacks{ [&](std::string_view message) { info.emplace_back(message); }, {} });
    std::filesystem::remove_all(root);

    ASSERT_TRUE(result.errors.empty()) << result.errors.front();
    ASSERT_EQ(result.records.size(), 1U);
    EXPECT_EQ(result.records[0].state, ModLifecycle::active);
    ASSERT_FALSE(info.empty());
    EXPECT_NE(info[0].find("Hello World"), std::string::npos);
}

TEST(ActivationTests, rejectsEntryPointEscape)
{
    const auto root = makeRoot("openloco-modding-activation-escape");
    writeFile(root / "enabled.json", R"({"enabled":["openloco.sample.hello"]})");
    writeFile(root / "hello" / "manifest.json", R"({
        "id": "openloco.sample.hello",
        "name": "Hello",
        "version": "0.1.0",
        "engineApiVersion": "0.1.0",
        "entryPoints": ["../outside.json"],
        "stateSchemaVersion": 1
    })");

    const auto result = activateMods(root);
    std::filesystem::remove_all(root);

    ASSERT_EQ(result.records.size(), 1U);
    EXPECT_EQ(result.records[0].state, ModLifecycle::failed);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].find(".."), std::string::npos);
}

TEST(ActivationTests, missingEnabledModIsAnError)
{
    const auto root = makeRoot("openloco-modding-activation-missing");
    writeFile(root / "enabled.json", R"({"enabled":["openloco.sample.missing"]})");
    writeFile(root / "hello" / "manifest.json", R"({
        "id": "openloco.sample.hello",
        "name": "Hello",
        "version": "0.1.0",
        "engineApiVersion": "0.1.0",
        "stateSchemaVersion": 1
    })");

    const auto result = activateMods(root);
    std::filesystem::remove_all(root);

    EXPECT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors.back().find("missing"), std::string::npos);
}
