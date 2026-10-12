#include <OpenLoco/Date.h>
#include <OpenLoco/Entities/EntityManager.h>
#include <OpenLoco/Lines/Line.h>
#include <OpenLoco/Map/StationElement.h>
#include <OpenLoco/Map/TileManager.h>
#include <OpenLoco/Map/TrackElement.h>
#include <OpenLoco/Rules/Editor.h>
#include <OpenLoco/Rules/Platforms.h>
#include <OpenLoco/Rules/Rules.h>
#include <OpenLoco/Scenario/ScenarioManager.h>
#include <OpenLoco/Vehicles/OrderManager.h>
#include <OpenLoco/Vehicles/Vehicle2.h>
#include <OpenLoco/Vehicles/VehicleBody.h>
#include <OpenLoco/Vehicles/VehicleBogie.h>
#include <OpenLoco/Vehicles/VehicleHead.h>
#include <OpenLoco/World/CompanyManager.h>
#include <OpenLoco/World/Station.h>
#include <OpenLoco/World/StationManager.h>
#include <fstream>
#include <gtest/gtest.h>

using namespace OpenLoco::Rules;
namespace
{
    Program route() { return makeProgram({ 1, 9, 3, 17, 25 }, true); }
    Evaluate yes = [](const Condition&) { return CheckResult{ true, true, {} }; };
    Evaluate no = [](const Condition&) { return CheckResult{ true, false, {} }; };
    Block section(const Program& p, Kind kind, size_t first, size_t last)
    {
        Block b;
        b.kind = kind;
        b.first = p.rows[first].key;
        b.last = p.rows[last].key;
        return b;
    }
    Result add(Program& p, Block b)
    {
        Edit e;
        e.block = b;
        return edit(p, e);
    }
    struct GameRulesFixture : testing::Test
    {
        void SetUp() override
        {
            OpenLoco::EntityManager::reset();
            OpenLoco::Vehicles::OrderManager::reset();
            OpenLoco::Lines::clearLines();
            OpenLoco::CompanyManager::setControllingId(static_cast<OpenLoco::CompanyId>(0));
            OpenLoco::ScenarioManager::setScenarioTicks(0);
        }
        void TearDown() override
        {
            clear();
            OpenLoco::Lines::clearLines();
        }
        OpenLoco::Vehicles::VehicleHead* vehicle()
        {
            using namespace OpenLoco;
            using namespace OpenLoco::Vehicles;
            VehicleBase* components[4]{};
            const VehicleEntityType types[] = { VehicleEntityType::head, VehicleEntityType::vehicle_1, VehicleEntityType::vehicle_2, VehicleEntityType::tail };
            for (size_t i = 0; i < 4; ++i)
            {
                components[i] = reinterpret_cast<VehicleBase*>(EntityManager::createEntityVehicle());
                components[i]->baseType = EntityBaseType::vehicle;
                components[i]->subType = types[i];
                components[i]->mode = TransportMode::rail;
                components[i]->owner = static_cast<CompanyId>(0);
                components[i]->tileX = 32;
                components[i]->tileY = 32;
                if (i)
                {
                    components[i - 1]->nextCarId = components[i]->id;
                }
                components[i]->nextCarId = EntityId::null;
                components[i]->head = components[0]->id;
            }
            auto* head = reinterpret_cast<VehicleHead*>(components[0]);
            EntityManager::moveEntityToList(head, EntityManager::EntityListType::vehicleHead);
            head->status = Status::travelling;
            head->ordinalNumber = 1;
            OrderManager::allocateOrders(*head);
            for (uint16_t station : { 10, 20, 30 })
            {
                OrderStopAt order(static_cast<StationId>(station));
                OrderManager::insertOrder(head, head->sizeOfOrderTable - 1, &order);
            }
            head->currentOrder = 0;
            return head;
        }
    };
}
TEST(RulesTests, plainOrdersKeepTheirSequence)
{
    auto p = route();
    Runtime r;
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        for (const auto& row : p.rows)
        {
            EXPECT_EQ(next(p, r, 0, yes).order, row.key);
        }
    }
}
TEST(RulesTests, falseIfSkipsStopsAndWaypoint)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    Runtime r;
    EXPECT_EQ(next(p, r, 0, no).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, no).order, p.rows[4].key);
    Runtime other;
    for (const auto& row : p.rows)
    {
        EXPECT_EQ(next(p, other, 0, yes).order, row.key);
    }
}
TEST(RulesTests, conditionalSkipFallsThroughWhenFalse)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    auto skip = section(p, Kind::skip, 1, 3);
    skip.parent = p.blocks[0].id;
    ASSERT_TRUE(add(p, skip));
    Runtime r;
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[4].key);
    Runtime other;
    for (const auto& row : p.rows)
    {
        EXPECT_EQ(next(p, other, 0, no).order, row.key);
    }
}
TEST(RulesTests, fullVehicleGoesToNamedStop)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    auto go = section(p, Kind::goTo, 1, 3);
    go.parent = p.blocks[0].id;
    go.target = p.rows[4].key;
    ASSERT_TRUE(add(p, go));
    Runtime r;
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[4].key);
}
TEST(RulesTests, repeatRunsWholeSectionAndResetsNextVisit)
{
    auto p = route();
    auto b = section(p, Kind::repeat, 1, 3);
    b.number = 2;
    ASSERT_TRUE(add(p, b));
    Runtime a, bRuntime;
    const size_t expected[] = { 0, 1, 2, 3, 1, 2, 3, 4, 0, 1, 2, 3, 1, 2, 3, 4 };
    for (auto index : expected)
    {
        EXPECT_EQ(next(p, a, 0, yes).order, p.rows[index].key);
    }
    EXPECT_EQ(next(p, bRuntime, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, bRuntime, 0, yes).order, p.rows[1].key);
}
TEST(RulesTests, unavailableCheckTakesOtherwise)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 2)));
    auto otherwise = section(p, Kind::otherwise, 3, 3);
    otherwise.parent = p.blocks[0].id;
    ASSERT_TRUE(add(p, otherwise));
    Runtime r;
    auto unavailable = [](const Condition&) { return CheckResult{ false, true, "No timetable slot" }; };
    EXPECT_EQ(next(p, r, 0, unavailable).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, unavailable).order, p.rows[3].key);
    EXPECT_EQ(r.status, "No timetable slot");
}
TEST(RulesTests, fifthShapeAndMissingRepeatNumberAreAtomicRejections)
{
    auto p = route();
    auto b = section(p, Kind::repeat, 1, 3);
    b.number = 0;
    auto before = p;
    EXPECT_FALSE(add(p, b));
    EXPECT_EQ(before, p);
    b.number = 2;
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(add(p, b));
    }
    before = p;
    EXPECT_FALSE(add(p, b));
    EXPECT_EQ(before, p);
}
TEST(RulesTests, goToRejectsAnotherListAndEnd)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    auto go = section(p, Kind::goTo, 1, 3);
    go.parent = p.blocks[0].id;
    go.target = 900;
    auto before = p;
    EXPECT_FALSE(add(p, go));
    EXPECT_EQ(before, p);
    go.target = 0;
    EXPECT_FALSE(add(p, go));
}
TEST(RulesTests, interiorInsertionJoinsSectionExteriorInsertionDoesNot)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::group, 1, 3)));
    const auto first = p.blocks[0].first, last = p.blocks[0].last;
    Edit e;
    e.kind = EditKind::insertRow;
    e.first = 2;
    e.raw = 33;
    ASSERT_TRUE(edit(p, e));
    EXPECT_EQ(indexOf(p, last) - indexOf(p, first), 3U);
    e.first = 1;
    ASSERT_TRUE(edit(p, e));
    EXPECT_EQ(indexOf(p, first), 2U);
    EXPECT_EQ(indexOf(p, last) - indexOf(p, first), 3U);
}
TEST(RulesTests, deletingTargetDisablesGoToWithoutRetargeting)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    auto go = section(p, Kind::goTo, 1, 3);
    go.parent = p.blocks[0].id;
    go.target = p.rows[4].key;
    ASSERT_TRUE(add(p, go));
    Edit e;
    e.kind = EditKind::deleteRow;
    e.first = 4;
    ASSERT_TRUE(edit(p, e));
    EXPECT_FALSE(p.blocks[1].enabled);
    EXPECT_EQ(p.blocks[1].target, go.target);
    Runtime r;
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[1].key);
    EXPECT_EQ(r.status, "Go to destination is missing");
}
TEST(RulesTests, waitUsesSimulationTicksAndContinuesOnExpiry)
{
    auto p = route();
    auto b = section(p, Kind::waitUntil, 1, 1);
    b.number = 2;
    ASSERT_TRUE(add(p, b));
    Runtime r;
    EXPECT_EQ(next(p, r, 10, no).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 10, no).order, p.rows[1].key);
    EXPECT_EQ(next(p, r, 10, no).kind, RunKind::waiting);
    EXPECT_EQ(next(p, r, 89, no).kind, RunKind::waiting);
    EXPECT_EQ(next(p, r, 90, no).order, p.rows[2].key);
    EXPECT_EQ(r.status, "Time limit reached");
}
TEST(RulesTests, selfJumpYieldsAndResumesDeterministically)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 0)));
    auto go = section(p, Kind::goTo, 0, 0);
    go.parent = p.blocks[0].id;
    go.target = p.rows[0].key;
    ASSERT_TRUE(add(p, go));
    Runtime a, b;
    for (uint32_t tick = 0; tick < 3; ++tick)
    {
        EXPECT_EQ(next(p, a, tick, yes).kind, RunKind::yielded);
        EXPECT_EQ(next(p, b, tick, yes).kind, RunKind::yielded);
        EXPECT_EQ(a.pc, b.pc);
        EXPECT_EQ(a.steps, 256);
    }
    EXPECT_EQ(next(p, a, 3, no).order, p.rows[0].key);
}
TEST(RulesTests, crossingSectionsAndRoadPlatformChecksAreRefused)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::group, 0, 2)));
    EXPECT_FALSE(add(p, section(p, Kind::group, 1, 3)));
    p.rail = false;
    auto b = section(p, Kind::ifBlock, 3, 3);
    b.condition.check = Check::platformFree;
    b.condition.stop = p.rows[0].key;
    EXPECT_FALSE(add(p, b));
}
TEST(RulesTests, nestedRepeatsRestartTheInnerCount)
{
    auto p = route();
    auto outer = section(p, Kind::repeat, 1, 3);
    outer.number = 2;
    ASSERT_TRUE(add(p, outer));
    auto inner = section(p, Kind::repeat, 2, 2);
    inner.number = 2;
    ASSERT_TRUE(add(p, inner));
    Runtime r;
    const size_t expected[] = { 0, 1, 2, 2, 3, 1, 2, 2, 3, 4 };
    for (auto index : expected)
    {
        EXPECT_EQ(next(p, r, 0, yes).order, p.rows[index].key);
    }
}
TEST(RulesTests, skippingTheLastRowStillFinishesEachRepeatPass)
{
    auto p = route();
    auto repeat = section(p, Kind::repeat, 1, 3);
    repeat.number = 2;
    ASSERT_TRUE(add(p, repeat));
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 3, 3)));
    auto skip = section(p, Kind::skip, 3, 3);
    skip.parent = p.blocks[1].id;
    ASSERT_TRUE(add(p, skip));
    Runtime r;
    const size_t expected[] = { 0, 1, 2, 1, 2, 4 };
    for (auto index : expected)
    {
        EXPECT_EQ(next(p, r, 0, yes).order, p.rows[index].key);
    }
}
TEST(RulesTests, branchIsEvaluatedOnceWhenEnteringASection)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    Runtime r;
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[1].key);
    EXPECT_EQ(next(p, r, 1, no).order, p.rows[2].key);
    EXPECT_EQ(next(p, r, 1, no).order, p.rows[3].key);
}
TEST(RulesTests, cargoComparisonBoundariesAndJoinedChecks)
{
    auto p = route();
    auto b = section(p, Kind::ifBlock, 1, 3);
    b.condition.check = Check::cargoWaiting;
    b.condition.stop = p.rows[1].key;
    b.condition.compare = Compare::less;
    b.condition.value = 8;
    ASSERT_TRUE(add(p, b));
    for (int cargo : { 7, 8, 9 })
    {
        Runtime r;
        Evaluate evaluate = [cargo](const Condition& c) { return CheckResult{ true, cargo < c.value, {} }; };
        next(p, r, 0, evaluate);
        EXPECT_EQ(next(p, r, 0, evaluate).order, p.rows[cargo < 8 ? 1 : 4].key);
    }
    auto joined = p.blocks[0];
    joined.join = Join::orCheck;
    joined.extra.check = Check::full;
    Edit editCommand;
    editCommand.kind = EditKind::updateBlock;
    editCommand.block = joined;
    ASSERT_TRUE(edit(p, editCommand));
    Runtime r;
    Evaluate unavailable = [](const Condition& c) { return CheckResult{ c.check == Check::cargoWaiting, true, "Unavailable" }; };
    next(p, r, 0, unavailable);
    EXPECT_EQ(next(p, r, 0, unavailable).order, p.rows[4].key);
}
TEST(RulesTests, fullSectionMovePreservesKeysAndBrackets)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::group, 1, 3)));
    const auto group = p.blocks[0];
    Edit e;
    e.kind = EditKind::moveRows;
    e.first = 1;
    e.last = 3;
    e.destination = 0;
    ASSERT_TRUE(edit(p, e));
    EXPECT_EQ(indexOf(p, group.first), 0U);
    EXPECT_EQ(indexOf(p, group.last), 2U);
    EXPECT_EQ(p.blocks[0], group);
}
TEST(RulesTests, deletingTheOnlyMemberRemovesItsGroup)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::group, 1, 1)));
    Edit e;
    e.kind = EditKind::deleteRow;
    e.first = 1;
    ASSERT_TRUE(edit(p, e));
    EXPECT_TRUE(p.blocks.empty());
}
TEST(RulesTests, platformOccupancyIncludesWagonsButExcludesSelfAndReservations)
{
    using namespace OpenLoco;
    Signals::clearAll();
    Platform a{ 1, { { 32, 32, 16, 0, 0 }, { 64, 32, 16, 0, 0 } } };
    Platform b{ 2, { { 32, 64, 16, 0, 0 } } };
    EXPECT_TRUE(platformIsFree(a, 1));
    Signals::holdOccupied(2, a.pieces[1], false);
    EXPECT_FALSE(platformIsFree(a, 1));
    EXPECT_TRUE(platformIsFree(a, 2));
    EXPECT_TRUE(platformIsFree(b, 1));
    Signals::clearAll();
    std::vector<Signals::ReservedStep> path{ Signals::entranceOf({ a.pieces[0], 0 }) };
    ASSERT_TRUE(Signals::reservePath(2, path));
    EXPECT_TRUE(platformIsFree(a, 1));
    Signals::clearAll();
}
TEST(RulesTests, compactAndDefaultEditorControlsStaySeparate)
{
    for (int16_t width : { 360, 600, 1000 })
    {
        for (int16_t height : { 240, 360, 700 })
        {
            auto l = editorLayout(width, height);
            EXPECT_LT(l.canvasTop, l.canvasBottom);
            EXPECT_LT(l.canvasBottom, l.controlsTop);
            EXPECT_LT(l.controlsTop + 43, l.binTop);
            EXPECT_LT(l.binTop + 19, l.footerTop);
            EXPECT_LT(l.paletteWidth, width / 2);
        }
    }
}
TEST(RulesTests, editorPlacementAvoidsRelatedWindowsWhenTheyFit)
{
    EditorRect opener{ 0, 28, 420, 268 };
    auto p = placeEditor(opener, 1280, 800, { opener });
    EXPECT_EQ(p.x, 420);
    EXPECT_EQ(p.y, 28);
    EXPECT_EQ(p.width, 600);
    auto small = placeEditor(opener, 800, 600, { opener });
    EXPECT_EQ(small.x, 420);
    EXPECT_EQ(small.y, 28);
    EXPECT_EQ(small.width, 360);
    auto tiny = placeEditor(opener, 640, 480, { opener });
    EXPECT_EQ(tiny.width, 360);
    EXPECT_GE(tiny.x, 0);
    EXPECT_LE(tiny.x + tiny.width, 640);
}
TEST(RulesTests, sharedLineCompanionRoundTripsAndMissingFileClearsRules)
{
    using namespace OpenLoco;
    Lines::clearLines();
    clear();
    const auto line = Lines::createLine("Rules test", TransportMode::rail, 2);
    Lines::LineOrder a;
    a.station = 10;
    Lines::LineOrder b;
    b.station = 20;
    ASSERT_TRUE(Lines::addOrder(line.id, a).ok);
    ASSERT_TRUE(Lines::addOrder(line.id, b).ok);
    const Owner owner{ OwnerKind::line, line.id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::repeat, 0, 1);
    e.block.number = 2;
    ASSERT_TRUE(submit(owner, e));
    const auto saved = *find(owner);
    const auto path = std::filesystem::temp_directory_path() / "openloco_rules_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    clear();
    ASSERT_TRUE(loadCompanion(path));
    ASSERT_NE(find(owner), nullptr);
    EXPECT_EQ(*find(owner), saved);
    std::filesystem::remove(companionPath(path));
    ASSERT_TRUE(loadCompanion(path));
    EXPECT_EQ(find(owner), nullptr);
    Lines::clearLines();
    clear();
}
TEST(RulesTests, corruptedOrMismatchedCompanionNeverAttachesRules)
{
    using namespace OpenLoco;
    Lines::clearLines();
    clear();
    const auto line = Lines::createLine("Rules test", TransportMode::rail, 2);
    Lines::LineOrder order;
    order.station = 10;
    ASSERT_TRUE(Lines::addOrder(line.id, order).ok);
    const Owner owner{ OwnerKind::line, line.id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::group, 0, 0);
    ASSERT_TRUE(submit(owner, e));
    const auto path = std::filesystem::temp_directory_path() / "openloco_rules_bad_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    ASSERT_TRUE(Lines::addOrder(line.id, Lines::LineOrder{}).ok);
    ASSERT_TRUE(loadCompanion(path));
    EXPECT_EQ(find(owner), nullptr);
    {
        std::ofstream out(companionPath(path));
        out << "ORULES 1\n99999999\n";
    }
    EXPECT_FALSE(loadCompanion(path));
    EXPECT_EQ(find(owner), nullptr);
    std::filesystem::remove(companionPath(path));
    Lines::clearLines();
    clear();
}
TEST_F(GameRulesFixture, liveVehicleSkipsSectionAtTheRealOrderCursor)
{
    auto* head = vehicle();
    const auto id = OpenLoco::enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::ifBlock, 1, 1);
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    reached(id);
    EXPECT_TRUE(prepareVehicle(id));
    EXPECT_EQ(head->currentOrder, 4);
}
TEST_F(GameRulesFixture, privateRulesAndWaitRuntimeRoundTripWithoutPlayerSaves)
{
    auto* head = vehicle();
    const auto id = OpenLoco::enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::waitUntil, 0, 0);
    e.block.number = 2;
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    reached(id);
    EXPECT_FALSE(prepareVehicle(id));
    const auto path = std::filesystem::temp_directory_path() / "openloco_private_rules_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    clear();
    ASSERT_TRUE(loadCompanion(path));
    ASSERT_NE(find(owner), nullptr);
    OpenLoco::ScenarioManager::setScenarioTicks(79);
    EXPECT_FALSE(prepareVehicle(id));
    OpenLoco::ScenarioManager::setScenarioTicks(80);
    EXPECT_TRUE(prepareVehicle(id));
    EXPECT_EQ(head->currentOrder, 2);
    std::filesystem::remove(companionPath(path));
}
TEST_F(GameRulesFixture, assignmentDetachmentCloneAndDeleteKeepCorrectOwners)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner privateOwner{ OwnerKind::vehicle, id };
    auto p = snapshot(privateOwner);
    Edit e;
    e.block = section(p, Kind::repeat, 0, 1);
    e.block.number = 2;
    ASSERT_TRUE(submit(privateOwner, e));
    const auto line = Lines::createLine("Shared", TransportMode::rail, 2);
    Lines::LineOrder a;
    a.station = 10;
    Lines::LineOrder b;
    b.station = 20;
    ASSERT_TRUE(Lines::addOrder(line.id, a).ok);
    ASSERT_TRUE(Lines::addOrder(line.id, b).ok);
    ASSERT_TRUE(Lines::assignVehicle(line.id, id, TransportMode::rail).ok);
    EXPECT_EQ(find(privateOwner), nullptr);
    EXPECT_EQ(find({ OwnerKind::line, line.id }), nullptr);
    p = snapshot({ OwnerKind::line, line.id });
    e.block = section(p, Kind::repeat, 0, 1);
    e.block.number = 3;
    ASSERT_TRUE(submit({ OwnerKind::line, line.id }, e));
    ASSERT_TRUE(Lines::unassignVehicle(id).ok);
    ASSERT_NE(find(privateOwner), nullptr);
    EXPECT_EQ(find(privateOwner)->blocks[0].number, 3);
    auto* second = vehicle();
    cloned(id, enumValue(second->id));
    ASSERT_NE(find({ OwnerKind::vehicle, enumValue(second->id) }), nullptr);
    forget(privateOwner);
    EXPECT_EQ(find(privateOwner), nullptr);
    EXPECT_NE(find({ OwnerKind::vehicle, enumValue(second->id) }), nullptr);
}
TEST_F(GameRulesFixture, insertingDeletingAndMovingRealOrdersKeepRuleKeys)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::group, 1, 2);
    ASSERT_TRUE(submit(owner, e));
    const auto first = find(owner)->blocks[0].first, last = find(owner)->blocks[0].last;
    Vehicles::OrderStopAt newOrder(static_cast<StationId>(40));
    Vehicles::OrderManager::insertOrder(head, 4, &newOrder);
    EXPECT_EQ(indexOf(*find(owner), last) - indexOf(*find(owner), first), 2U);
    Vehicles::OrderManager::deleteOrder(head, 4);
    EXPECT_EQ(indexOf(*find(owner), last) - indexOf(*find(owner), first), 1U);
    e = {};
    e.kind = EditKind::moveRows;
    e.first = 1;
    e.last = 2;
    e.destination = 0;
    ASSERT_TRUE(submit(owner, e));
    EXPECT_EQ(indexOf(*find(owner), first), 0U);
    EXPECT_EQ(indexOf(*find(owner), last), 1U);
}
TEST_F(GameRulesFixture, ownerMismatchAndReusedVehicleIdentityAreRejected)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::group, 0, 1);
    head->owner = static_cast<CompanyId>(1);
    EXPECT_FALSE(submit(owner, e));
    EXPECT_EQ(find(owner), nullptr);
    head->owner = static_cast<CompanyId>(0);
    ASSERT_TRUE(submit(owner, e));
    const auto path = std::filesystem::temp_directory_path() / "openloco_reused_rules_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    head->ordinalNumber = 2;
    clear();
    EXPECT_TRUE(loadCompanion(path));
    EXPECT_EQ(find(owner), nullptr);
    std::filesystem::remove(companionPath(path));
}
TEST_F(GameRulesFixture, cargoInstructionsUseOneBranchChoiceThroughoutLoading)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    Vehicles::OrderUnloadAll unload(0);
    Vehicles::OrderManager::insertOrder(head, 2, &unload);
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::ifBlock, 1, 1);
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    reached(id);
    EXPECT_FALSE(cargoOrderEnabled(id, 2));
    EXPECT_FALSE(cargoOrderEnabled(id, 2));
    EXPECT_TRUE(prepareVehicle(id));
    EXPECT_EQ(head->currentOrder, 3);
}
TEST_F(GameRulesFixture, sharedVehiclesKeepIndependentRepeatsAndCursorAcrossLineEdit)
{
    using namespace OpenLoco;
    auto* a = vehicle();
    auto* b = vehicle();
    auto ai = enumValue(a->id), bi = enumValue(b->id);
    const auto line = Lines::createLine("Shared repeats", TransportMode::rail, 2);
    for (uint16_t station : { 10, 20, 30 })
    {
        Lines::LineOrder order;
        order.station = station;
        ASSERT_TRUE(Lines::addOrder(line.id, order).ok);
    }
    ASSERT_TRUE(Lines::assignVehicle(line.id, ai, TransportMode::rail).ok);
    ASSERT_TRUE(Lines::assignVehicle(line.id, bi, TransportMode::rail).ok);
    const Owner owner{ OwnerKind::line, line.id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::repeat, 1, 2);
    e.block.number = 2;
    ASSERT_TRUE(submit(owner, e));
    reached(ai);
    ASSERT_TRUE(prepareVehicle(ai));
    EXPECT_EQ(a->currentOrder, 2);
    reached(ai);
    ASSERT_TRUE(prepareVehicle(ai));
    EXPECT_EQ(a->currentOrder, 4);
    reached(ai);
    ASSERT_TRUE(prepareVehicle(ai));
    EXPECT_EQ(a->currentOrder, 2);
    EXPECT_EQ(b->currentOrder, 0);
    reached(bi);
    ASSERT_TRUE(prepareVehicle(bi));
    EXPECT_EQ(b->currentOrder, 2);
    Lines::LineOrder inserted;
    inserted.station = 40;
    ASSERT_TRUE(Lines::insertOrder(line.id, 0, inserted).ok);
    EXPECT_EQ(a->currentOrder, 4);
    EXPECT_EQ(b->currentOrder, 4);
}
TEST_F(GameRulesFixture, savingMidRepeatAndMidBudgetPreservesExecution)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::repeat, 1, 2);
    e.block.number = 2;
    ASSERT_TRUE(submit(owner, e));
    reached(id);
    ASSERT_TRUE(prepareVehicle(id));
    reached(id);
    ASSERT_TRUE(prepareVehicle(id));
    EXPECT_EQ(head->currentOrder, 4);
    const auto path = std::filesystem::temp_directory_path() / "openloco_running_rules_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    clear();
    ASSERT_TRUE(loadCompanion(path));
    reached(id);
    ASSERT_TRUE(prepareVehicle(id));
    EXPECT_EQ(head->currentOrder, 2);
    std::filesystem::remove(companionPath(path));
    e = {};
    e.kind = EditKind::removeBlock;
    e.block.id = find(owner)->blocks[0].id;
    ASSERT_TRUE(submit(owner, e));
    p = snapshot(owner);
    e = {};
    e.block = section(p, Kind::ifBlock, 2, 2);
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::equal;
    e.block.condition.value = OpenLoco::getCurrentYear();
    ASSERT_TRUE(submit(owner, e));
    p = snapshot(owner);
    e = {};
    e.block = section(p, Kind::goTo, 2, 2);
    e.block.parent = p.blocks[0].id;
    e.block.target = p.rows[2].key;
    ASSERT_TRUE(submit(owner, e));
    reached(id);
    EXPECT_FALSE(prepareVehicle(id));
    ASSERT_TRUE(saveCompanion(path));
    clear();
    ASSERT_TRUE(loadCompanion(path));
    ScenarioManager::setScenarioTicks(1);
    EXPECT_FALSE(prepareVehicle(id));
    EXPECT_NE(vehicleStatus(id).find("Choosing"), std::string::npos);
    std::filesystem::remove(companionPath(path));
}
TEST_F(GameRulesFixture, deletingASelectedRunUsesValidatedOrderDeletion)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::group, 1, 2);
    ASSERT_TRUE(submit(owner, e));
    e = {};
    e.kind = EditKind::deleteRows;
    e.first = 1;
    e.last = 2;
    ASSERT_TRUE(submit(owner, e));
    EXPECT_EQ(find(owner)->rows.size(), 1U);
    EXPECT_TRUE(find(owner)->blocks.empty());
    EXPECT_EQ(head->sizeOfOrderTable, 3);
}
TEST_F(GameRulesFixture, fullMeansEveryUsableCargoCompartmentIsFull)
{
    using namespace OpenLoco;
    using namespace OpenLoco::Vehicles;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    Vehicle train(*head);
    auto tail = train.veh2->nextCarId;
    auto* front = reinterpret_cast<VehicleBogie*>(EntityManager::createEntityVehicle());
    auto* back = reinterpret_cast<VehicleBogie*>(EntityManager::createEntityVehicle());
    auto* body = reinterpret_cast<VehicleBody*>(EntityManager::createEntityVehicle());
    for (auto* part : { static_cast<VehicleBase*>(front), static_cast<VehicleBase*>(back), static_cast<VehicleBase*>(body) })
    {
        part->baseType = EntityBaseType::vehicle;
        part->head = head->id;
        part->mode = TransportMode::rail;
    }
    front->subType = back->subType = VehicleEntityType::bogie;
    body->subType = VehicleEntityType::body_start;
    train.veh2->nextCarId = front->id;
    front->nextCarId = back->id;
    back->nextCarId = body->id;
    body->nextCarId = tail;
    front->secondaryCargo.type = 0;
    front->secondaryCargo.maxQty = 10;
    front->secondaryCargo.qty = 11;
    body->primaryCargo.type = 0;
    body->primaryCargo.maxQty = 10;
    body->primaryCargo.qty = 9;
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::ifBlock, 1, 1);
    e.block.condition.check = Check::full;
    ASSERT_TRUE(submit(owner, e));
    reached(id);
    ASSERT_TRUE(prepareVehicle(id));
    EXPECT_EQ(head->currentOrder, 4);
    head->currentOrder = 0;
    manuallySkipped(id);
    ASSERT_TRUE(prepareVehicle(id));
    front->secondaryCargo.qty = body->primaryCargo.qty = 10;
    reached(id);
    ASSERT_TRUE(prepareVehicle(id));
    EXPECT_EQ(head->currentOrder, 2);
}
TEST_F(GameRulesFixture, platformQueryJoinsStationTrackAndKeepsParallelPlatformsSeparate)
{
    using namespace OpenLoco;
    using namespace OpenLoco::World;
    TileManager::allocateMapElements();
    TileManager::initialise();
    auto* station = StationManager::get(static_cast<StationId>(10));
    station->name = 1;
    station->stationTileSize = 4;
    const Pos3 points[] = { { 320, 320, 16 }, { 352, 320, 16 }, { 320, 352, 16 }, { 352, 352, 16 } };
    for (size_t i = 0; i < 4; ++i)
    {
        const auto pos = points[i];
        station->stationTiles[i] = pos;
        auto* track = TileManager::insertElement(ElementType::track, Pos2{ pos.x, pos.y }, 4, 0)->as<TrackElement>();
        track->setTrackId(0);
        track->setRotation(0);
        track->setSequenceIndex(0);
        track->setHasStationElement(true);
        auto* element = TileManager::insertElement(ElementType::station, Pos2{ pos.x, pos.y }, 4, 0)->as<StationElement>();
        element->setStationId(static_cast<StationId>(10));
        element->setStationType(StationType::trainStation);
    }
    const auto platforms = railPlatforms(10);
    ASSERT_EQ(platforms.size(), 2U);
    EXPECT_EQ(platforms[0].pieces.size(), 2U);
    EXPECT_EQ(platforms[1].pieces.size(), 2U);
    Signals::clearAll();
    Signals::holdOccupied(2, platforms[0].pieces.back(), false);
    EXPECT_FALSE(platformIsFree(platforms[0], 1));
    EXPECT_TRUE(platformIsFree(platforms[1], 1));
    Signals::clearAll();
    station->name = StringIds::null;
}
TEST_F(GameRulesFixture, removingAStationUpdatesSharedAndPrivateOrderKeys)
{
    using namespace OpenLoco;
    auto* a = vehicle();
    auto* b = vehicle();
    auto ai = enumValue(a->id), bi = enumValue(b->id);
    const auto line = Lines::createLine("Station removal", TransportMode::rail, 2);
    for (uint16_t station : { 10, 20, 30 })
    {
        Lines::LineOrder order;
        order.station = station;
        ASSERT_TRUE(Lines::addOrder(line.id, order).ok);
    }
    ASSERT_TRUE(Lines::assignVehicle(line.id, ai, TransportMode::rail).ok);
    const Owner shared{ OwnerKind::line, line.id }, own{ OwnerKind::vehicle, bi };
    for (auto owner : { shared, own })
    {
        const auto p = snapshot(owner);
        Edit e;
        e.block = section(p, Kind::group, 0, 1);
        ASSERT_TRUE(submit(owner, e));
    }
    Vehicles::OrderManager::removeOrdersForStation(static_cast<StationId>(10));
    ASSERT_EQ(Lines::findLine(line.id)->orders.size(), 2U);
    EXPECT_EQ(a->sizeOfOrderTable, 5);
    EXPECT_EQ(b->sizeOfOrderTable, 5);
    for (auto owner : { shared, own })
    {
        ASSERT_NE(find(owner), nullptr);
        ASSERT_EQ(find(owner)->rows.size(), 2U);
        EXPECT_EQ(indexOf(*find(owner), find(owner)->blocks[0].first), 0U);
    }
}
TEST_F(GameRulesFixture, anUnplacedVehicleRunsTheFirstBlockWhenPlaced)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    head->tileX = -1;
    const auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::ifBlock, 0, 0);
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    head->tileX = 32;
    ASSERT_TRUE(prepareVehicle(id));
    EXPECT_EQ(head->currentOrder, 2);
}
TEST_F(GameRulesFixture, saveImmediatelyAfterDeletingTheCurrentOrderResetsPendingExecution)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    const auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::group, 0, 1);
    ASSERT_TRUE(submit(owner, e));
    Vehicles::OrderManager::deleteOrder(head, 0);
    const auto path = std::filesystem::temp_directory_path() / "openloco_deleted_current_rules_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    clear();
    ASSERT_TRUE(loadCompanion(path));
    ASSERT_NE(find(owner), nullptr);
    EXPECT_EQ(find(owner)->rows.size(), 2U);
    EXPECT_TRUE(prepareVehicle(id));
    std::filesystem::remove(companionPath(path));
}
TEST_F(GameRulesFixture, localServicesPassStationsSkippedByRulesAndSaveThatDecision)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    const auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::ifBlock, 1, 1);
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    reached(id);
    ASSERT_TRUE(prepareVehicle(id));
    EXPECT_FALSE(canStopAt(id, 20));
    EXPECT_TRUE(canStopAt(id, 30));
    EXPECT_TRUE(canStopAt(id, 40));
    const auto path = std::filesystem::temp_directory_path() / "openloco_skipped_stations_rules_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    clear();
    ASSERT_TRUE(loadCompanion(path));
    EXPECT_FALSE(canStopAt(id, 20));
    reached(id);
    EXPECT_TRUE(canStopAt(id, 20));
    std::filesystem::remove(companionPath(path));
}
TEST_F(GameRulesFixture, diversionKeepsTimetableAttachedToTheReachedStop)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::group, 0, 2);
    ASSERT_TRUE(submit(owner, e));
    reached(id);
    ASSERT_TRUE(prepareVehicle(id));
    EXPECT_EQ(head->currentOrder, 2);
    EXPECT_EQ(timetableOrder(id, head->currentOrder), 0);
    const auto path = std::filesystem::temp_directory_path() / "openloco_timetable_rules_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    clear();
    ASSERT_TRUE(loadCompanion(path));
    EXPECT_EQ(timetableOrder(id, head->currentOrder), 0);
    std::filesystem::remove(companionPath(path));
}

TEST(RulesTests, otherwiseCanUseTheDiversionSectionWhenTheCheckFails)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    auto go = section(p, Kind::goTo, 1, 3);
    go.parent = p.blocks[0].id;
    go.target = p.rows[4].key;
    ASSERT_TRUE(add(p, go));
    auto fallback = section(p, Kind::otherwise, 1, 3);
    fallback.parent = p.blocks[0].id;
    ASSERT_TRUE(add(p, fallback));
    Runtime diverted, normal;
    EXPECT_EQ(next(p, diverted, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, diverted, 0, yes).order, p.rows[4].key);
    for (const auto& row : p.rows)
    {
        EXPECT_EQ(next(p, normal, 0, no).order, row.key);
    }
}

TEST_F(GameRulesFixture, companionNumbersDoNotDependOnTheGameLocale)
{
    struct GroupedNumbers : std::numpunct<char>
    {
        char do_thousands_sep() const override { return ','; }
        std::string do_grouping() const override { return "\3"; }
    };
    struct RestoreLocale
    {
        std::locale previous = std::locale();
        ~RestoreLocale() { std::locale::global(previous); }
    } restore;
    std::locale::global(std::locale(std::locale::classic(), new GroupedNumbers));
    auto* head = vehicle();
    auto p = snapshot(ownerForVehicle(OpenLoco::enumValue(head->id)));
    auto b = section(p, Kind::ifBlock, 0, 0);
    b.condition.check = Check::year;
    b.condition.value = 2000;
    Edit e;
    e.block = b;
    ASSERT_TRUE(submit(ownerForVehicle(OpenLoco::enumValue(head->id)), e));
    const auto path = std::filesystem::temp_directory_path() / "openloco-rules-locale.SV5";
    ASSERT_TRUE(saveCompanion(path));
    std::ifstream file(companionPath(path));
    const std::string contents((std::istreambuf_iterator<char>(file)), {});
    EXPECT_EQ(contents.find("2,000"), std::string::npos);
    std::locale::global(std::locale::classic());
    clear();
    EXPECT_TRUE(loadCompanion(path));
    ASSERT_NE(find(ownerForVehicle(OpenLoco::enumValue(head->id))), nullptr);
    EXPECT_EQ(find(ownerForVehicle(OpenLoco::enumValue(head->id)))->blocks[0].condition.value, 2000);
    file.close();
    std::filesystem::remove(companionPath(path));
}
