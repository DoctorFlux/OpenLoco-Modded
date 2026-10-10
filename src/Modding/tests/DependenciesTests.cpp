#include <OpenLoco/Modding/Dependencies.hpp>
#include <gtest/gtest.h>

using namespace OpenLoco::Modding;

namespace
{
    Manifest makeMod(std::string id, std::vector<std::string> dependencies = {}, std::vector<std::string> optional = {}, std::vector<std::string> conflicts = {})
    {
        Manifest manifest;
        manifest.id = std::move(id);
        manifest.name = manifest.id;
        manifest.version = "0.1.0";
        manifest.engineApiVersion = "0.1.0";
        manifest.stateSchemaVersion = 1;
        manifest.dependencies = std::move(dependencies);
        manifest.optionalDependencies = std::move(optional);
        manifest.conflicts = std::move(conflicts);
        return manifest;
    }
}

TEST(DependenciesTests, ordersDependenciesFirst)
{
    const auto result = resolveDependencies({
        makeMod("openloco.sample.lines", { "openloco.sample.logic" }),
        makeMod("openloco.sample.logic"),
    });
    ASSERT_TRUE(result.errors.empty());
    ASSERT_EQ(result.loadOrder.size(), 2U);
    EXPECT_EQ(result.loadOrder[0], "openloco.sample.logic");
    EXPECT_EQ(result.loadOrder[1], "openloco.sample.lines");
}

TEST(DependenciesTests, reportsMissingDependency)
{
    const auto result = resolveDependencies({
        makeMod("openloco.sample.lines", { "openloco.sample.logic" }),
    });
    ASSERT_TRUE(result.loadOrder.empty());
    ASSERT_EQ(result.errors.size(), 1U);
    EXPECT_NE(result.errors[0].find("missing"), std::string::npos);
}

TEST(DependenciesTests, reportsCycle)
{
    const auto result = resolveDependencies({
        makeMod("openloco.sample.a", { "openloco.sample.b" }),
        makeMod("openloco.sample.b", { "openloco.sample.a" }),
    });
    EXPECT_TRUE(result.loadOrder.empty());
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].find("cycle"), std::string::npos);
}

TEST(DependenciesTests, reportsConflict)
{
    const auto result = resolveDependencies({
        makeMod("openloco.sample.a", {}, {}, { "openloco.sample.b" }),
        makeMod("openloco.sample.b"),
    });
    EXPECT_TRUE(result.loadOrder.empty());
    ASSERT_FALSE(result.errors.empty());
    EXPECT_NE(result.errors[0].find("conflicts"), std::string::npos);
}

TEST(DependenciesTests, optionalDependencyIsOrderedWhenPresent)
{
    const auto result = resolveDependencies({
        makeMod("openloco.sample.lines", {}, { "openloco.sample.logic" }),
        makeMod("openloco.sample.logic"),
    });
    ASSERT_TRUE(result.errors.empty());
    EXPECT_EQ(result.loadOrder[0], "openloco.sample.logic");
    EXPECT_EQ(result.loadOrder[1], "openloco.sample.lines");
}

TEST(DependenciesTests, missingOptionalDependencyIsAllowed)
{
    const auto result = resolveDependencies({
        makeMod("openloco.sample.lines", {}, { "openloco.sample.logic" }),
    });
    ASSERT_TRUE(result.errors.empty());
    ASSERT_EQ(result.loadOrder.size(), 1U);
    EXPECT_EQ(result.loadOrder[0], "openloco.sample.lines");
}
