#include <OpenLoco/Modding/Manifest.hpp>
#include <gtest/gtest.h>

using namespace OpenLoco::Modding;

namespace
{
    constexpr std::string_view kValid = R"({
        "id": "openloco.sample.hello",
        "name": "Hello World",
        "author": "OpenLoco",
        "version": "0.1.0",
        "engineApiVersion": "0.1.0",
        "dependencies": ["openloco.core.logic"],
        "optionalDependencies": [],
        "conflicts": ["openloco.sample.other"],
        "capabilities": ["logic"],
        "platforms": ["windows", "linux", "macos"],
        "description": "A sample.\nSecond line.",
        "license": "MIT",
        "entryPoints": ["scripts/main"],
        "stateSchemaVersion": 1,
        "futureField": "ignored"
    })";
}

TEST(ManifestTests, parsesValidManifest)
{
    Manifest manifest;
    const auto failure = parseManifest(kValid, manifest);
    ASSERT_FALSE(failure.has_value());
    EXPECT_EQ(manifest.id, "openloco.sample.hello");
    EXPECT_EQ(manifest.name, "Hello World");
    EXPECT_EQ(manifest.dependencies.size(), 1U);
    EXPECT_EQ(manifest.dependencies[0], "openloco.core.logic");
    EXPECT_EQ(manifest.description, "A sample.\nSecond line.");
    EXPECT_EQ(manifest.stateSchemaVersion, 1U);
}

TEST(ManifestTests, rejectsMissingId)
{
    Manifest manifest;
    const auto failure = parseManifest(R"({"name":"X","version":"0.1.0","engineApiVersion":"0.1.0","stateSchemaVersion":1})", manifest);
    ASSERT_TRUE(failure.has_value());
    EXPECT_NE(failure->message.find("id"), std::string::npos);
}

TEST(ManifestTests, rejectsBadVersion)
{
    Manifest manifest;
    const auto failure = parseManifest(R"({"id":"openloco.sample.hello","name":"X","version":"1.0","engineApiVersion":"0.1.0","stateSchemaVersion":1})", manifest);
    ASSERT_TRUE(failure.has_value());
    EXPECT_NE(failure->message.find("version"), std::string::npos);
}

TEST(ManifestTests, rejectsDuplicateKey)
{
    Manifest manifest;
    const auto failure = parseManifest(R"({"id":"openloco.sample.hello","id":"openloco.sample.other","name":"X","version":"0.1.0","engineApiVersion":"0.1.0","stateSchemaVersion":1})", manifest);
    ASSERT_TRUE(failure.has_value());
    EXPECT_NE(failure->message.find("duplicate"), std::string::npos);
}

TEST(ManifestTests, rejectsSelfDependency)
{
    Manifest manifest;
    const auto failure = parseManifest(R"({"id":"openloco.sample.hello","name":"X","version":"0.1.0","engineApiVersion":"0.1.0","dependencies":["openloco.sample.hello"],"stateSchemaVersion":1})", manifest);
    ASSERT_TRUE(failure.has_value());
    EXPECT_NE(failure->message.find("itself"), std::string::npos);
}

TEST(ManifestTests, decodesUnicodeEscape)
{
    Manifest manifest;
    const auto failure = parseManifest(R"({"id":"openloco.sample.hello","name":"Caf\u00e9","version":"0.1.0","engineApiVersion":"0.1.0","stateSchemaVersion":1})", manifest);
    ASSERT_FALSE(failure.has_value());
    EXPECT_EQ(manifest.name, "Caf\xC3\xA9");
}

TEST(ManifestTests, apiCompatibility)
{
    const auto running = parseVersion(kModApiVersion);
    ASSERT_TRUE(running.has_value());
    EXPECT_TRUE(isApiCompatible(*running, *parseVersion("0.1.0")));
    EXPECT_FALSE(isApiCompatible(*running, *parseVersion("0.1.1")));
    EXPECT_FALSE(isApiCompatible(*running, *parseVersion("0.2.0")));
    EXPECT_FALSE(isApiCompatible(*running, *parseVersion("1.0.0")));
    EXPECT_TRUE(isApiCompatible(Version{ 0, 1, 2 }, *parseVersion("0.1.0")));
}
