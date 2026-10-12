#include <OpenLoco/Rules/Rules.h>
#include <gtest/gtest.h>

using namespace OpenLoco::Rules;
namespace
{
    Program route() { return makeProgram({ 1, 9, 3, 17, 25 }, true); }
    Evaluate yes = [](const Condition&) { return CheckResult{ true, true, {} }; };
    Evaluate no = [](const Condition&) { return CheckResult{ true, false, {} }; };
    Block section(const Program& p, Kind kind, size_t first, size_t last)
    {
        Block b; b.kind = kind; b.first = p.rows[first].key; b.last = p.rows[last].key;
        return b;
    }
    Result add(Program& p, Block b) { Edit e; e.block = b; return edit(p, e); }
}
TEST(RulesTests, plainOrdersKeepTheirSequence)
{
    auto p = route(); Runtime r;
    for (int cycle = 0; cycle < 2; ++cycle)
        for (const auto& row : p.rows) EXPECT_EQ(next(p, r, 0, yes).order, row.key);
}
TEST(RulesTests, falseIfSkipsStopsAndWaypoint)
{
    auto p = route(); ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    Runtime r;
    EXPECT_EQ(next(p, r, 0, no).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, no).order, p.rows[4].key);
    Runtime other;
    for (const auto& row : p.rows) EXPECT_EQ(next(p, other, 0, yes).order, row.key);
}
TEST(RulesTests, conditionalSkipFallsThroughWhenFalse)
{
    auto p = route(); ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    auto skip = section(p, Kind::skip, 1, 3); skip.parent = p.blocks[0].id;
    ASSERT_TRUE(add(p, skip));
    Runtime r;
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[4].key);
    Runtime other;
    for (const auto& row : p.rows) EXPECT_EQ(next(p, other, 0, no).order, row.key);
}
TEST(RulesTests, fullVehicleGoesToNamedStop)
{
    auto p = route(); ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    auto go = section(p, Kind::goTo, 1, 3); go.parent = p.blocks[0].id; go.target = p.rows[4].key;
    ASSERT_TRUE(add(p, go)); Runtime r;
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[4].key);
}
TEST(RulesTests, repeatRunsWholeSectionAndResetsNextVisit)
{
    auto p = route(); auto b = section(p, Kind::repeat, 1, 3); b.number = 2;
    ASSERT_TRUE(add(p, b)); Runtime a, bRuntime;
    const size_t expected[] = { 0, 1, 2, 3, 1, 2, 3, 4, 0, 1, 2, 3, 1, 2, 3, 4 };
    for (auto index : expected) EXPECT_EQ(next(p, a, 0, yes).order, p.rows[index].key);
    EXPECT_EQ(next(p, bRuntime, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, bRuntime, 0, yes).order, p.rows[1].key);
}
TEST(RulesTests, unavailableCheckTakesOtherwise)
{
    auto p = route(); ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 2)));
    auto otherwise = section(p, Kind::otherwise, 3, 3); otherwise.parent = p.blocks[0].id;
    ASSERT_TRUE(add(p, otherwise)); Runtime r;
    auto unavailable = [](const Condition&) { return CheckResult{ false, true, "No timetable slot" }; };
    EXPECT_EQ(next(p, r, 0, unavailable).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, unavailable).order, p.rows[3].key);
    EXPECT_EQ(r.status, "No timetable slot");
}
TEST(RulesTests, fifthShapeAndMissingRepeatNumberAreAtomicRejections)
{
    auto p = route(); auto b = section(p, Kind::repeat, 1, 3); b.number = 0;
    auto before = p; EXPECT_FALSE(add(p, b)); EXPECT_EQ(before, p);
    b.number = 2;
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(add(p, b));
    before = p; EXPECT_FALSE(add(p, b)); EXPECT_EQ(before, p);
}
TEST(RulesTests, goToRejectsAnotherListAndEnd)
{
    auto p = route(); ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    auto go = section(p, Kind::goTo, 1, 3); go.parent = p.blocks[0].id; go.target = 900;
    auto before = p; EXPECT_FALSE(add(p, go)); EXPECT_EQ(before, p);
    go.target = 0; EXPECT_FALSE(add(p, go));
}
TEST(RulesTests, interiorInsertionJoinsSectionExteriorInsertionDoesNot)
{
    auto p = route(); ASSERT_TRUE(add(p, section(p, Kind::group, 1, 3)));
    const auto first = p.blocks[0].first, last = p.blocks[0].last;
    Edit e; e.kind = EditKind::insertRow; e.first = 2; e.raw = 33;
    ASSERT_TRUE(edit(p, e)); EXPECT_EQ(indexOf(p, last) - indexOf(p, first), 3U);
    e.first = 1; ASSERT_TRUE(edit(p, e)); EXPECT_EQ(indexOf(p, first), 2U);
    EXPECT_EQ(indexOf(p, last) - indexOf(p, first), 3U);
}
TEST(RulesTests, deletingTargetDisablesGoToWithoutRetargeting)
{
    auto p = route(); ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    auto go = section(p, Kind::goTo, 1, 3); go.parent = p.blocks[0].id; go.target = p.rows[4].key;
    ASSERT_TRUE(add(p, go)); Edit e; e.kind = EditKind::deleteRow; e.first = 4;
    ASSERT_TRUE(edit(p, e)); EXPECT_FALSE(p.blocks[1].enabled); EXPECT_EQ(p.blocks[1].target, go.target);
    Runtime r; EXPECT_EQ(next(p, r, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[1].key);
    EXPECT_EQ(r.status, "Go to destination is missing");
}
TEST(RulesTests, waitUsesSimulationTicksAndContinuesOnExpiry)
{
    auto p = route(); auto b = section(p, Kind::waitUntil, 1, 1); b.number = 2;
    ASSERT_TRUE(add(p, b)); Runtime r;
    EXPECT_EQ(next(p, r, 10, no).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 10, no).order, p.rows[1].key);
    EXPECT_EQ(next(p, r, 10, no).kind, RunKind::waiting);
    EXPECT_EQ(next(p, r, 89, no).kind, RunKind::waiting);
    EXPECT_EQ(next(p, r, 90, no).order, p.rows[2].key);
    EXPECT_EQ(r.status, "Time limit reached");
}
TEST(RulesTests, selfJumpYieldsAndResumesDeterministically)
{
    auto p = route(); ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 0)));
    auto go = section(p, Kind::goTo, 0, 0); go.parent = p.blocks[0].id; go.target = p.rows[0].key;
    ASSERT_TRUE(add(p, go)); Runtime a, b;
    for (uint32_t tick = 0; tick < 3; ++tick)
    {
        EXPECT_EQ(next(p, a, tick, yes).kind, RunKind::yielded);
        EXPECT_EQ(next(p, b, tick, yes).kind, RunKind::yielded);
        EXPECT_EQ(a.pc, b.pc); EXPECT_EQ(a.steps, 256);
    }
    EXPECT_EQ(next(p, a, 3, no).order, p.rows[0].key);
}
TEST(RulesTests, crossingSectionsAndRoadPlatformChecksAreRefused)
{
    auto p = route(); ASSERT_TRUE(add(p, section(p, Kind::group, 0, 2)));
    EXPECT_FALSE(add(p, section(p, Kind::group, 1, 3)));
    p.rail = false; auto b = section(p, Kind::ifBlock, 3, 3);
    b.condition.check = Check::platformFree; b.condition.stop = p.rows[0].key;
    EXPECT_FALSE(add(p, b));
}
