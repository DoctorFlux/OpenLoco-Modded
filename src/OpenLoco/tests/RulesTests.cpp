#include <OpenLoco/Date.h>
#include <OpenLoco/Entities/EntityManager.h>
#include <OpenLoco/Graphics/RenderTarget.h>
#include <OpenLoco/Graphics/SoftwareDrawingContext.h>
#include <OpenLoco/Lines/Line.h>
#include <OpenLoco/Map/MapSelection.h>
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
TEST(RulesTests, fifthShapeAndInvalidRepeatCountAreAtomicRejections)
{
    auto p = route();
    auto b = section(p, Kind::repeat, 1, 3);
    b.number = 21;
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

TEST(RulesTests, growingAnIfAlsoGrowsItsActionsAndSameSectionOtherwise)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 1)));
    auto action = section(p, Kind::skip, 1, 1);
    action.parent = p.blocks[0].id;
    ASSERT_TRUE(add(p, action));
    auto fallback = section(p, Kind::otherwise, 1, 1);
    fallback.parent = p.blocks[0].id;
    ASSERT_TRUE(add(p, fallback));
    Edit resize;
    resize.kind = EditKind::updateBlock;
    resize.block = p.blocks[0];
    resize.block.last = p.rows[3].key;
    ASSERT_TRUE(edit(p, resize));
    for (const auto& block : p.blocks)
    {
        EXPECT_EQ(block.last, p.rows[3].key);
    }
    Runtime runner;
    EXPECT_EQ(next(p, runner, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, runner, 0, yes).order, p.rows[4].key);
}
TEST(RulesTests, resizeRejectsCrossingBracketsWithoutChangingAnything)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::group, 0, 1)));
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 2, 4)));
    const auto before = p;
    Edit resize;
    resize.kind = EditKind::updateBlock;
    resize.block = p.blocks[0];
    resize.block.last = p.rows[2].key;
    EXPECT_FALSE(edit(p, resize));
    EXPECT_EQ(p, before);
}
TEST(RulesTests, executionFeedbackUsesLatchedBranchesAndCurrentRows)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    Runtime runtime;
    EXPECT_EQ(blockFeedback(p, &runtime, p.blocks[0].id), Feedback::idle);
    next(p, runtime, 0, yes);
    next(p, runtime, 0, yes);
    EXPECT_EQ(blockFeedback(p, &runtime, p.blocks[0].id), Feedback::passed);
    EXPECT_TRUE(blockIsCurrent(p, &runtime, p.blocks[0].id));
    const auto row = orderFeedback(p, &runtime, 1);
    EXPECT_TRUE(row.current);
    EXPECT_EQ(row.state, Feedback::passed);
    EXPECT_NE(row.sentence.find("If"), std::string::npos);
    EXPECT_NE(row.sentence.find("[2-4]"), std::string::npos);
    Runtime failed;
    next(p, failed, 0, no);
    next(p, failed, 0, no);
    EXPECT_EQ(blockFeedback(p, &failed, p.blocks[0].id), Feedback::failed);
    EXPECT_FALSE(blockIsCurrent(p, &failed, p.blocks[0].id));
    ++p.revision;
    EXPECT_EQ(blockFeedback(p, &runtime, p.blocks[0].id), Feedback::idle);
    EXPECT_FALSE(orderFeedback(p, &runtime, 1).current);
}
TEST(RulesTests, waitFeedbackPersistsAndTurnsGreenWhenItPasses)
{
    auto p = route();
    auto wait = section(p, Kind::waitUntil, 0, 0);
    wait.number = 2;
    ASSERT_TRUE(add(p, wait));
    Runtime runtime;
    next(p, runtime, 0, no);
    EXPECT_EQ(next(p, runtime, 0, no).kind, RunKind::waiting);
    EXPECT_EQ(blockFeedback(p, &runtime, p.blocks[0].id), Feedback::failed);
    EXPECT_TRUE(validRuntime(p, runtime));
    EXPECT_EQ(next(p, runtime, 1, yes).kind, RunKind::order);
    EXPECT_EQ(blockFeedback(p, &runtime, p.blocks[0].id), Feedback::passed);
    EXPECT_TRUE(validRuntime(p, runtime));
}
TEST(RulesTests, normalOrderDraggingProducesAnEditAndCancelsStaleOrOutsideDrops)
{
    auto p = route();
    const Owner owner{ OwnerKind::vehicle, 24 };
    OrderDrag drag;
    drag.begin(owner, p, 2, 10, 20);
    drag.motion(10, 40);
    const auto move = drag.finish(owner, p, p.rows.size());
    ASSERT_TRUE(move);
    auto original = p.rows[2].key;
    ASSERT_TRUE(edit(p, *move));
    EXPECT_EQ(p.rows.back().key, original);
    drag.begin(owner, p, 0, 0, 0);
    drag.motion(0, 20);
    EXPECT_FALSE(drag.finish(owner, p, {}));
    drag.begin(owner, p, 0, 0, 0);
    drag.motion(0, 20);
    ++p.revision;
    EXPECT_FALSE(drag.finish(owner, p, 2));
    drag.begin(owner, p, 0, 0, 0);
    drag.motion(0, 20);
    EXPECT_FALSE(drag.finish({ OwnerKind::line, 3 }, p, 2));
    drag.begin(owner, p, 0, 0, 0);
    drag.motion(0, 20);
    drag.cancel();
    EXPECT_FALSE(drag.finish(owner, p, 2));
}
TEST(RulesTests, ordinaryRowGeometryOnlyExpandsWhenRulesExist)
{
    auto p = route();
    EXPECT_EQ(orderRowHeight(&p), 10);
    ASSERT_TRUE(add(p, section(p, Kind::group, 0, 3)));
    EXPECT_EQ(orderRowHeight(&p), 20);
    const auto layout = editorLayout(360, 240);
    const auto width = (360 - 10) / 6;
    EXPECT_GT(width - 3, 50);
    EXPECT_LT(layout.canvasBottom, layout.controlsTop);
    EXPECT_LT(layout.controlsTop + 43, layout.binTop);
}

TEST(RulesTests, elseIfChecksOnlyUntilOneBranchIsTaken)
{
    auto p = route();
    auto first = section(p, Kind::ifBlock, 0, 0);
    first.condition.value = 1;
    ASSERT_TRUE(add(p, first));
    auto middle = section(p, Kind::elseIf, 1, 2);
    middle.parent = p.blocks[0].id;
    middle.condition.value = 2;
    ASSERT_TRUE(add(p, middle));
    auto last = section(p, Kind::otherwise, 3, 3);
    last.parent = p.blocks[1].id;
    ASSERT_TRUE(add(p, last));
    for (int taken : { 1, 2, 3 })
    {
        Runtime runtime;
        int checks = 0;
        auto evaluate = [&](const Condition& c) { ++checks; return CheckResult{true, c.value == taken, {}}; };
        std::vector<Key> visited;
        do
        {
            visited.push_back(next(p, runtime, 0, evaluate).order);
        } while (visited.back() != p.rows[4].key);
        const std::vector<Key> expected = taken == 1 ? std::vector<Key>{ p.rows[0].key, p.rows[4].key } : taken == 2 ? std::vector<Key>{ p.rows[1].key, p.rows[2].key, p.rows[4].key }
                                                                                                                     : std::vector<Key>{ p.rows[3].key, p.rows[4].key };
        EXPECT_EQ(visited, expected);
        EXPECT_EQ(checks, taken == 1 ? 1 : 2);
    }
}
TEST(RulesTests, elseChainRejectsForksAndBadParentsAtomically)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 0)));
    auto middle = section(p, Kind::elseIf, 1, 1);
    middle.parent = p.blocks[0].id;
    ASSERT_TRUE(add(p, middle));
    const auto before = p;
    auto branch = section(p, Kind::otherwise, 2, 2);
    branch.parent = p.blocks[0].id;
    EXPECT_FALSE(add(p, branch));
    EXPECT_EQ(p, before);
    branch.parent = p.rows[0].key;
    EXPECT_FALSE(add(p, branch));
    EXPECT_EQ(p, before);
}
TEST(RulesTests, infiniteRepeatRunsUntilConditionalBreakAndStartsFreshOnReturn)
{
    auto p = route();
    auto repeat = section(p, Kind::repeat, 1, 3);
    repeat.number = 0;
    ASSERT_TRUE(add(p, repeat));
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 3, 3)));
    auto stop = section(p, Kind::breakLoop, 3, 3);
    stop.parent = p.blocks[1].id;
    stop.target = p.blocks[0].id;
    ASSERT_TRUE(add(p, stop));
    Runtime runtime;
    for (int index : { 0, 1, 2, 3, 1, 2 })
    {
        EXPECT_EQ(next(p, runtime, 0, no).order, p.rows[index].key);
    }
    EXPECT_EQ(runtime.repeats[p.blocks[0].id], 0);
    EXPECT_TRUE(validRuntime(p, runtime));
    EXPECT_EQ(next(p, runtime, 0, yes).order, p.rows[4].key);
    EXPECT_TRUE(runtime.repeats.empty());
    EXPECT_EQ(next(p, runtime, 1, no).order, p.rows[0].key);
    EXPECT_EQ(next(p, runtime, 1, no).order, p.rows[1].key);
    EXPECT_EQ(runtime.repeats[p.blocks[0].id], 0);
}
TEST(RulesTests, infiniteDecisionLoopIsBoundedAndResumesToBreak)
{
    auto p = route();
    auto repeat = section(p, Kind::repeat, 1, 3);
    repeat.number = 0;
    ASSERT_TRUE(add(p, repeat));
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    auto stop = section(p, Kind::breakLoop, 1, 3);
    stop.parent = p.blocks[2].id;
    stop.target = p.blocks[0].id;
    ASSERT_TRUE(add(p, stop));
    Runtime runtime;
    EXPECT_EQ(next(p, runtime, 0, no).order, p.rows[0].key);
    EXPECT_EQ(next(p, runtime, 0, no).kind, RunKind::yielded);
    EXPECT_EQ(runtime.steps, 256);
    EXPECT_TRUE(validRuntime(p, runtime));
    EXPECT_EQ(next(p, runtime, 1, yes).order, p.rows[4].key);
    EXPECT_EQ(runtime.status.find("Choosing"), std::string::npos);
}
TEST(RulesTests, breakRejectsNonLoopTargetsAndLoopRemovalRemovesBreak)
{
    auto p = route();
    auto repeat = section(p, Kind::repeat, 0, 3);
    repeat.number = 0;
    ASSERT_TRUE(add(p, repeat));
    auto stop = section(p, Kind::breakLoop, 2, 2);
    stop.target = p.rows[0].key;
    const auto before = p;
    EXPECT_FALSE(add(p, stop));
    EXPECT_EQ(p, before);
    stop.target = p.blocks[0].id;
    ASSERT_TRUE(add(p, stop));
    Edit removal;
    removal.kind = EditKind::removeBlock;
    removal.block.id = p.blocks[0].id;
    ASSERT_TRUE(edit(p, removal));
    EXPECT_TRUE(p.blocks.empty());
}
TEST(RulesTests, programLayoutContainsRealNestedBracketsAndConsistentHitTests)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::group, 0, 3)));
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 2)));
    for (int width : { 138, 190, 360, 580 })
    {
        const auto layout = programLayout(p, width);
        ASSERT_EQ(layout.orderTops.size(), p.rows.size());
        int16_t previousEnd = 0;
        for (const auto& item : layout.items)
        {
            EXPECT_GE(item.top, previousEnd);
            previousEnd = item.top + item.height;
            const auto* hit = programHit(layout, item.top + 1);
            ASSERT_NE(hit, nullptr);
            EXPECT_EQ(hit->row, item.row);
            EXPECT_EQ(hit->block, item.block);
            EXPECT_EQ(programRowAt(layout, item.top + 1), item.row);
            if (item.block && !item.closing)
            {
                EXPECT_GT(item.bottom, item.top + item.height);
            }
            if (item.closing)
            {
                EXPECT_EQ(programDrop(p, layout, item.top + 1).row, item.row + 1);
                EXPECT_EQ(programDrop(p, layout, item.top + 1).into, item.block);
            }
        }
        EXPECT_GT(layout.height, 10 * p.rows.size());
        EXPECT_EQ(programRowAt(layout, layout.height + 10), p.rows.size());
        EXPECT_LT(layout.orderTops[1] - layout.orderTops[0], layout.height);
    }
}
TEST_F(GameRulesFixture, currentProgramAndNextStopRemainVisibleDuringWait)
{
    auto* head = vehicle();
    const auto id = OpenLoco::enumValue(head->id);
    auto p = snapshot(ownerForVehicle(id));
    Edit e;
    e.block = section(p, Kind::waitUntil, 0, 0);
    e.block.number = 3;
    ASSERT_TRUE(submit(ownerForVehicle(id), e));
    ASSERT_TRUE(prepareVehicle(id));
    reached(id);
    EXPECT_FALSE(prepareVehicle(id));
    EXPECT_NE(vehicleStatus(id).find("Wait until"), std::string::npos);
    EXPECT_NE(vehicleNextStop(id).find("after decision"), std::string::npos);
    EXPECT_EQ(executingBlock(*find(ownerForVehicle(id)), *runtimeForVehicle(id)), find(ownerForVehicle(id))->blocks[0].id);
}
TEST_F(GameRulesFixture, saveResumesInfiniteRepeatAndElseIfDefinitions)
{
    auto* head = vehicle();
    const auto id = OpenLoco::enumValue(head->id);
    auto p = snapshot(ownerForVehicle(id));
    Edit e;
    e.block = section(p, Kind::repeat, 1, 2);
    e.block.number = 0;
    ASSERT_TRUE(submit(ownerForVehicle(id), e));
    ASSERT_TRUE(prepareVehicle(id));
    reached(id);
    ASSERT_TRUE(prepareVehicle(id));
    const auto path = std::filesystem::temp_directory_path() / "openloco-infinite-rules.SV5";
    ASSERT_TRUE(saveCompanion(path));
    clear();
    ASSERT_TRUE(loadCompanion(path));
    ASSERT_NE(runtimeForVehicle(id), nullptr);
    ASSERT_FALSE(runtimeForVehicle(id)->repeats.empty());
    EXPECT_EQ(runtimeForVehicle(id)->repeats.begin()->second, 0);
    EXPECT_NE(vehicleStatus(id).find("forever"), std::string::npos);
    p = snapshot(ownerForVehicle(id));
    e = {};
    e.block = section(p, Kind::ifBlock, 0, 0);
    ASSERT_TRUE(submit(ownerForVehicle(id), e));
    p = snapshot(ownerForVehicle(id));
    e.block = section(p, Kind::elseIf, 1, 1);
    e.block.parent = p.blocks.back().id;
    ASSERT_TRUE(submit(ownerForVehicle(id), e));
    ASSERT_TRUE(saveCompanion(path));
    clear();
    ASSERT_TRUE(loadCompanion(path));
    EXPECT_EQ(find(ownerForVehicle(id))->blocks.back().kind, Kind::elseIf);
    std::filesystem::remove(companionPath(path));
}
TEST_F(GameRulesFixture, structuralEditsRebuildTheWhiteCircleMapLabels)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    for (uint16_t station : { 10, 20, 30 })
    {
        auto* record = StationManager::get(static_cast<StationId>(station));
        record->x = station * 32;
        record->y = 32;
    }
    Vehicles::OrderManager::generateNumDisplayFrames(head);
    ASSERT_EQ(Vehicles::OrderManager::displayFrames().size(), 3);
    const auto original = Vehicles::OrderManager::displayFrames().front().frame.left[0];
    Edit move;
    move.kind = EditKind::moveRows;
    move.first = move.last = 2;
    move.destination = 0;
    ASSERT_TRUE(submit(ownerForVehicle(enumValue(head->id)), move));
    EXPECT_NE(Vehicles::OrderManager::displayFrames().front().frame.left[0], original);
    World::resetMapSelectionFlags();
}

TEST(RulesTests, maximumBlockCountFitsNativeScrollOffsets)
{
    auto p = route();
    for (size_t i = 0; i < 256; ++i)
    {
        auto group = section(p, Kind::group, 0, 4);
        group.name = std::string(80, 'W');
        ASSERT_TRUE(add(p, group));
    }
    const auto layout = programLayout(p, 138);
    EXPECT_GT(layout.height, 0);
    EXPECT_LT(layout.height, 32767);
    EXPECT_EQ(programRowAt(layout, layout.orderTops.back()), 4);
    EXPECT_EQ(layout.items[256].depth, 8);
}
TEST_F(GameRulesFixture, sharedOrderDraggingRefreshesTheAssignedVehiclesMapNumbers)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    for (uint16_t station : { 10, 20, 30 })
    {
        auto* record = StationManager::get(static_cast<StationId>(station));
        record->x = station * 32;
        record->y = 32;
    }
    const auto line = Lines::createLine("Numbers", TransportMode::rail, 2);
    for (uint16_t station : { 10, 20, 30 })
    {
        Lines::LineOrder row;
        row.station = station;
        ASSERT_TRUE(Lines::addOrder(line.id, row).ok);
    }
    ASSERT_TRUE(Lines::assignVehicle(line.id, enumValue(head->id), TransportMode::rail).ok);
    Vehicles::OrderManager::generateNumDisplayFrames(head);
    const auto original = Vehicles::OrderManager::displayFrames().front().frame.left[0];
    Edit move;
    move.kind = EditKind::moveRows;
    move.first = move.last = 2;
    move.destination = 0;
    ASSERT_TRUE(submit({ OwnerKind::line, line.id }, move));
    EXPECT_NE(Vehicles::OrderManager::displayFrames().front().frame.left[0], original);
    World::resetMapSelectionFlags();
}

TEST_F(GameRulesFixture, movingAWaypointRefreshesItsWorldNumberAndPosition)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    for (uint16_t station : { 10, 20, 30 })
    {
        auto* record = StationManager::get(static_cast<StationId>(station));
        record->x = station * 32;
        record->y = 32;
    }
    Vehicles::OrderRouteWaypoint waypoint({ 16, 20 }, 0, 0, 0);
    Vehicles::OrderManager::insertOrder(head, head->sizeOfOrderTable - 1, &waypoint);
    Vehicles::OrderManager::generateNumDisplayFrames(head);
    ASSERT_EQ(Vehicles::OrderManager::displayFrames().size(), 4);
    const auto oldLeft = Vehicles::OrderManager::displayFrames().front().frame.left[0];
    Edit move;
    move.kind = EditKind::moveRows;
    move.first = move.last = 3;
    move.destination = 0;
    ASSERT_TRUE(submit(ownerForVehicle(enumValue(head->id)), move));
    EXPECT_EQ(Vehicles::OrderManager::orders()[head->orderTableOffset].getType(), Vehicles::OrderType::RouteWaypoint);
    EXPECT_NE(Vehicles::OrderManager::displayFrames().front().frame.left[0], oldLeft);
    EXPECT_NE(vehicleNextStop(enumValue(head->id)).find("stop 2"), std::string::npos);
    World::resetMapSelectionFlags();
}

TEST(RulesTests, adjacentStopCanJoinIfWithoutMovingItsPhysicalOrder)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 0)));
    const auto rows = p.rows;
    Edit move;
    move.kind = EditKind::moveRows;
    move.first = move.last = move.destination = 1;
    move.into = p.blocks[0].id;
    ASSERT_TRUE(edit(p, move));
    EXPECT_EQ(p.rows, rows);
    EXPECT_EQ(p.blocks[0].last, p.rows[1].key);
    Runtime run;
    EXPECT_EQ(next(p, run, 0, no).order, p.rows[2].key);
    EXPECT_EQ(next(p, run, 1, yes).order, p.rows[3].key);
}
TEST(RulesTests, explicitDropAppendsStopsAndWaypointsToEveryContainerKind)
{
    for (auto kind : { Kind::ifBlock, Kind::otherwise, Kind::elseIf, Kind::repeat, Kind::waitUntil, Kind::group })
    {
        auto p = route();
        if (kind == Kind::otherwise || kind == Kind::elseIf)
        {
            ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 0)));
        }
        auto block = section(p, kind, 1, 1);
        if (kind == Kind::otherwise || kind == Kind::elseIf)
        {
            block.parent = p.blocks[0].id;
        }
        ASSERT_TRUE(add(p, block));
        const auto target = p.blocks.back().id;
        Edit move;
        move.kind = EditKind::moveRows;
        move.first = move.last = move.destination = 2; // Adjacent waypoint used to remain outside.
        move.into = target;
        ASSERT_TRUE(edit(p, move));
        EXPECT_EQ(p.blocks.back().last, p.rows[2].key);
        move.first = move.last = 4;
        move.destination = 3;
        ASSERT_TRUE(edit(p, move));
        EXPECT_EQ(p.blocks.back().last, p.rows[3].key);
        EXPECT_EQ(p.rows[2].raw & 7, 3);
        EXPECT_EQ(p.rows[3].raw & 7, 1);
    }
}
TEST(RulesTests, explicitDropGrowsNestedAncestorsAndAttachedActionsAtomically)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::repeat, 0, 1)));
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 1)));
    auto skip = section(p, Kind::skip, 1, 1);
    skip.parent = p.blocks[1].id;
    ASSERT_TRUE(add(p, skip));
    Edit move;
    move.kind = EditKind::moveRows;
    move.first = move.last = move.destination = 2;
    move.into = p.blocks[1].id;
    ASSERT_TRUE(edit(p, move));
    for (const auto& block : p.blocks)
    {
        EXPECT_EQ(block.last, p.rows[2].key);
    }
    EXPECT_TRUE(validate(p));
}
TEST(RulesTests, dropIntoRejectsBadReferencesCrossingBracketsAndSelfContainment)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 0)));
    ASSERT_TRUE(add(p, section(p, Kind::group, 1, 2)));
    const auto before = p;
    Edit move;
    move.kind = EditKind::moveRows;
    move.first = move.last = move.destination = 1;
    move.into = p.blocks[0].id;
    EXPECT_FALSE(edit(p, move)); // Would cross the Group.
    EXPECT_EQ(p, before);
    move.into = 9000;
    EXPECT_FALSE(edit(p, move));
    EXPECT_EQ(p, before);
    move.into = p.blocks[0].id;
    move.first = move.last = move.destination = 0;
    EXPECT_FALSE(edit(p, move));
    EXPECT_EQ(p, before);
}
TEST(RulesTests, explicitInsertionAtEitherEdgeJoinsWhileOrdinaryInsertionStaysOutside)
{
    for (size_t edge : { size_t{ 1 }, size_t{ 2 } })
    {
        auto p = route();
        ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 1)));
        Edit insert;
        insert.kind = EditKind::insertRow;
        insert.first = edge;
        insert.raw = 3;
        auto plain = p;
        ASSERT_TRUE(edit(plain, insert));
        EXPECT_NE(plain.blocks[0].first, plain.rows[edge].key);
        EXPECT_NE(plain.blocks[0].last, plain.rows[edge].key);
        insert.into = p.blocks[0].id;
        ASSERT_TRUE(edit(p, insert));
        EXPECT_LE(indexOf(p, p.blocks[0].first), edge);
        EXPECT_GE(indexOf(p, p.blocks[0].last), edge);
    }
}
TEST(RulesTests, paletteActionsUseEnclosingBranchWhenDroppedOnAnyInteriorOrder)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::repeat, 0, 3)));
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 1, 3)));
    for (auto kind : { Kind::skip, Kind::goTo, Kind::breakLoop })
    {
        const auto drop = blockDrop(p, kind, 2, 2);
        ASSERT_TRUE(drop);
        EXPECT_EQ(drop->block.parent, p.blocks[1].id);
        EXPECT_EQ(drop->block.first, p.rows[1].key);
        EXPECT_EQ(drop->block.last, p.rows[3].key);
        if (kind == Kind::breakLoop)
        {
            EXPECT_EQ(drop->block.target, p.blocks[0].id);
        }
    }
    const auto child = blockDrop(p, Kind::ifBlock, 2, 2);
    ASSERT_TRUE(child);
    auto nested = p;
    ASSERT_TRUE(edit(nested, *child));
    EXPECT_EQ(nested.blocks.back().first, p.rows[2].key);
    EXPECT_EQ(nested.blocks.back().last, p.rows[2].key);
}
TEST(RulesTests, droppingElseOnItsHeaderChoosesFollowingSection)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 1)));
    for (auto kind : { Kind::otherwise, Kind::elseIf })
    {
        const auto drop = blockDrop(p, kind, 0, 1, p.blocks[0].id);
        ASSERT_TRUE(drop);
        EXPECT_EQ(drop->block.parent, p.blocks[0].id);
        EXPECT_EQ(drop->block.first, p.rows[2].key);
    }
}
TEST(RulesTests, everyNestedGripAndAppendSlotIsReachableAtNarrowAndWideSizes)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::repeat, 0, 2)));
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 2)));
    ASSERT_TRUE(add(p, section(p, Kind::group, 1, 2)));
    for (int16_t width : { int16_t{ 138 }, int16_t{ 200 }, int16_t{ 460 } })
    {
        const auto layout = programLayout(p, width, 20);
        std::set<Key> closing;
        for (const auto& visual : layout.items)
        {
            if (visual.closing)
            {
                EXPECT_EQ(programDrop(p, layout, visual.top + 3).into, visual.block);
                closing.insert(visual.block);
            }
            else if (!visual.block)
            {
                const auto grip = 142 + visualIndent(visual, width) + 8;
                EXPECT_TRUE(orderGripHit(visual, grip, 138, width));
                EXPECT_FALSE(orderGripHit(visual, grip + 20, 138, width));
            }
        }
        EXPECT_EQ(closing.size(), p.blocks.size());
        EXPECT_LE(visualIndent(layout.items[4], width), width - 92);
    }
}
TEST(RulesTests, normalOrderDragCarriesTheExplicitAppendMembership)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 0)));
    const Owner owner{ OwnerKind::line, 9 };
    OrderDrag drag;
    drag.begin(owner, p, 1, 0, 0);
    drag.motion(20, 20);
    const auto drop = drag.finish(owner, p, 1, p.blocks[0].id);
    ASSERT_TRUE(drop);
    EXPECT_EQ(drop->into, p.blocks[0].id);
    ASSERT_TRUE(edit(p, *drop));
    EXPECT_EQ(p.blocks[0].last, p.rows[1].key);
}
TEST(RulesTests, stackSilhouetteHasRoundedCornersNotchAndBottomConnector)
{
    using namespace OpenLoco;
    std::vector<uint8_t> pixels(120 * 80, 255);
    Gfx::SoftwareDrawingContext ctx;
    ctx.pushRenderTarget({ pixels.data(), 0, 0, 120, 80, 0 });
    drawStackBlock(ctx, 10, 10, 90, 24, Kind::ifBlock);
    auto at = [&](int x, int y) { return pixels[y * 120 + x]; };
    EXPECT_EQ(at(10, 10), 255); // Rounded outside corner.
    EXPECT_NE(at(14, 10), 255);
    EXPECT_EQ(at(35, 10), 255); // Top snap notch.
    EXPECT_NE(at(35, 14), 255);
    EXPECT_NE(at(35, 35), 255); // Bottom connector.
    EXPECT_EQ(at(14, 35), 255);
    EXPECT_EQ(at(35, 37), 255);
    ctx.reset();
}
TEST_F(GameRulesFixture, addingAWaypointInsidePrivateIfPreservesReachedStopAndRoundTrips)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const auto owner = ownerForVehicle(id);
    auto p = snapshot(owner);
    Edit editCommand;
    editCommand.block = section(p, Kind::ifBlock, 0, 0);
    ASSERT_TRUE(submit(owner, editCommand));
    const auto key = find(owner)->blocks[0].id;
    Vehicles::OrderRouteWaypoint waypoint({ 16, 20 }, 0, 0, 0);
    Vehicles::OrderManager::insertOrder(head, head->sizeOfOrderTable - 1, &waypoint);
    const auto reached = head->currentOrder;
    editCommand = {};
    editCommand.kind = EditKind::moveRows;
    editCommand.first = editCommand.last = 3;
    editCommand.destination = 1;
    editCommand.into = key;
    ASSERT_TRUE(submit(owner, editCommand));
    EXPECT_EQ(head->currentOrder, reached);
    EXPECT_EQ(indexOf(*find(owner), find(owner)->blocks[0].last), 1);
    const auto path = std::filesystem::temp_directory_path() / "openloco-inside-rules.SV5";
    ASSERT_TRUE(saveCompanion(path));
    const auto expected = *find(owner);
    clear();
    ASSERT_TRUE(loadCompanion(path));
    EXPECT_EQ(*find(owner), expected);
    std::filesystem::remove(companionPath(path));
}
TEST_F(GameRulesFixture, sharedIfAcceptsSeveralStopsThroughTheSameValidatedCommand)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto line = Lines::createLine("Inside", TransportMode::rail, 2);
    for (uint16_t station : { 10, 20, 30 })
    {
        Lines::LineOrder order;
        order.station = station;
        ASSERT_TRUE(Lines::addOrder(line.id, order).ok);
    }
    ASSERT_TRUE(Lines::assignVehicle(line.id, enumValue(head->id), TransportMode::rail).ok);
    const Owner owner{ OwnerKind::line, line.id };
    auto p = snapshot(owner);
    Edit editCommand;
    editCommand.block = section(p, Kind::ifBlock, 0, 0);
    ASSERT_TRUE(submit(owner, editCommand));
    const auto key = find(owner)->blocks[0].id;
    editCommand = {};
    editCommand.kind = EditKind::moveRows;
    editCommand.first = editCommand.last = editCommand.destination = 1;
    editCommand.into = key;
    ASSERT_TRUE(submit(owner, editCommand));
    editCommand.first = editCommand.last = editCommand.destination = 2;
    ASSERT_TRUE(submit(owner, editCommand));
    EXPECT_EQ(find(owner)->blocks[0].last, find(owner)->rows[2].key);
    EXPECT_EQ(Lines::findLine(line.id)->orders.size(), 3);
}

TEST(RulesTests, expandingOneBranchKeepsItsWholePairedElseIfChainAndActionsAttached)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 0)));
    auto skip = section(p, Kind::skip, 0, 0);
    skip.parent = p.blocks[0].id;
    ASSERT_TRUE(add(p, skip));
    auto middle = section(p, Kind::elseIf, 0, 0);
    middle.parent = p.blocks[0].id;
    ASSERT_TRUE(add(p, middle));
    skip.parent = p.blocks.back().id;
    ASSERT_TRUE(add(p, skip));
    auto last = section(p, Kind::otherwise, 0, 0);
    last.parent = p.blocks[2].id;
    ASSERT_TRUE(add(p, last));
    Edit insert;
    insert.kind = EditKind::insertRow;
    insert.first = 1;
    insert.raw = 3;
    insert.into = p.blocks[0].id;
    ASSERT_TRUE(edit(p, insert));
    for (const auto& b : p.blocks)
    {
        EXPECT_EQ(b.last, p.rows[1].key);
    }
    EXPECT_TRUE(validate(p));
}

TEST(RulesTests, cShapeRasterLeavesAnOpenMouthAndIncludesFullClosingArm)
{
    using namespace OpenLoco;
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 2)));
    const auto layout = programLayout(p, 200, 20);
    std::vector<uint8_t> pixels(200 * layout.height, 255);
    Gfx::SoftwareDrawingContext ctx;
    ctx.pushRenderTarget({ pixels.data(), 0, 0, 200, layout.height, 0 });
    drawProgramBlocks(ctx, p, nullptr, layout, 200, 0, 0, false); // Geometry uses no original font assets.
    auto at = [&](int x, int y) { return pixels[y * 200 + x]; };
    const auto body = layout.orderTops[1] + 7;
    EXPECT_NE(at(5, body), 255);  // Thick C spine.
    EXPECT_EQ(at(13, body), 255); // Open cavity before the inserted statement.
    EXPECT_NE(at(30, body), 255); // The order's puzzle body.
    const auto end = std::find_if(layout.items.begin(), layout.items.end(), [](const ProgramVisual& v) { return v.closing; });
    ASSERT_NE(end, layout.items.end());
    EXPECT_NE(at(150, end->top + 18), 255); // Full-width closing arm.
    ctx.reset();
}

TEST(RulesTests, aWholeSelectedSectionCanSnapInsideAnotherBlock)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 0)));
    ASSERT_TRUE(add(p, section(p, Kind::group, 2, 3)));
    const auto group = p.blocks[1].id;
    Edit move;
    move.kind = EditKind::moveRows;
    move.first = 2;
    move.last = 3;
    move.destination = 1;
    move.into = p.blocks[0].id;
    move.block.id = group;
    ASSERT_TRUE(edit(p, move));
    EXPECT_EQ(indexOf(p, p.blocks[0].last), 2);
    EXPECT_EQ(indexOf(p, p.blocks[1].first), 1);
    EXPECT_EQ(indexOf(p, p.blocks[1].last), 2);
    Runtime run;
    EXPECT_EQ(next(p, run, 0, no).order, p.rows[3].key);
}

TEST(RulesTests, paletteDropsUseTheInteriorSideOfTheClosingSlot)
{
    auto p = route();
    ASSERT_TRUE(add(p, section(p, Kind::ifBlock, 0, 2)));
    const auto layout = programLayout(p, 300, 20);
    const auto close = std::find_if(layout.items.begin(), layout.items.end(), [](const ProgramVisual& v) { return v.closing; });
    ASSERT_NE(close, layout.items.end());
    for (auto kind : { Kind::ifBlock, Kind::repeat, Kind::waitUntil, Kind::group })
    {
        const auto drop = paletteDrop(p, layout, kind, close->top + 1);
        ASSERT_TRUE(drop);
        EXPECT_EQ(drop->block.first, p.rows[2].key);
        EXPECT_EQ(drop->block.last, p.rows[2].key);
    }
    const auto skip = paletteDrop(p, layout, Kind::skip, close->top + 1);
    ASSERT_TRUE(skip);
    EXPECT_EQ(skip->block.parent, p.blocks[0].id);
    const auto otherwise = paletteDrop(p, layout, Kind::otherwise, close->top + 1);
    ASSERT_TRUE(otherwise);
    EXPECT_EQ(otherwise->block.first, p.rows[3].key);
    const auto selected = paletteDrop(p, layout, Kind::repeat, close->top + 1, std::pair<size_t, size_t>{ 1, 2 });
    ASSERT_TRUE(selected);
    EXPECT_EQ(selected->block.first, p.rows[1].key);
    EXPECT_EQ(selected->block.last, p.rows[2].key);
    EXPECT_FALSE(paletteDrop(p, layout, Kind::group, layout.height + 5));
}

TEST(RulesTests, arrivalIfVisitsItsAnchorBeforeEvaluatingAndSkipsOnlyUnvisitedRows)
{
    auto p = route();
    auto b = section(p, Kind::ifBlock, 1, 3);
    b.when = Timing::onArrival;
    ASSERT_TRUE(add(p, b));
    Runtime r;
    int checks = 0;
    Evaluate check = [&](const Condition&) { ++checks; return CheckResult{ true, false, {} }; };
    EXPECT_EQ(next(p, r, 0, check).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, check).order, p.rows[1].key);
    EXPECT_EQ(checks, 0);
    EXPECT_TRUE(requiresArrivalStop(p, r));
    EXPECT_EQ(executingBlock(p, r), p.blocks[0].id);
    EXPECT_EQ(next(p, r, 1, check).order, p.rows[1].key);
    EXPECT_EQ(checks, 0);
    markArrived(p, r, p.rows[1].key);
    EXPECT_EQ(next(p, r, 2, check).order, p.rows[4].key);
    EXPECT_EQ(checks, 1);
    EXPECT_FALSE(r.skipped.contains(p.rows[1].key));
    EXPECT_TRUE(r.skipped.contains(p.rows[2].key));
    EXPECT_TRUE(r.skipped.contains(p.rows[3].key));
    EXPECT_TRUE(validRuntime(p, r));
}

TEST(RulesTests, beforeTravelIfChoosesAnotherStopWithoutVisitingTheSkippedAnchor)
{
    auto p = route();
    auto b = section(p, Kind::ifBlock, 1, 3);
    b.when = Timing::beforeTravel;
    ASSERT_TRUE(add(p, b));
    Runtime r;
    EXPECT_EQ(next(p, r, 0, no).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, no).order, p.rows[4].key);
    EXPECT_FALSE(requiresArrivalStop(p, r));
    EXPECT_EQ(r.awaitingArrival, 0U);
}

TEST(RulesTests, arrivalActionsInheritTheirIfTimingAndKeepTheReachedStop)
{
    for (auto kind : { Kind::skip, Kind::goTo })
    {
        auto p = route();
        auto b = section(p, Kind::ifBlock, 1, 3);
        b.when = Timing::onArrival;
        ASSERT_TRUE(add(p, b));
        auto action = section(p, kind, 1, 3);
        action.parent = p.blocks[0].id;
        action.target = p.rows[4].key;
        ASSERT_TRUE(add(p, action));
        EXPECT_EQ(timingOf(p, p.blocks[1]), Timing::onArrival);
        Runtime r;
        EXPECT_EQ(next(p, r, 0, yes).order, p.rows[0].key);
        EXPECT_EQ(next(p, r, 0, yes).order, p.rows[1].key);
        markArrived(p, r, p.rows[1].key);
        EXPECT_EQ(next(p, r, 0, yes).order, p.rows[4].key);
        EXPECT_FALSE(r.skipped.contains(p.rows[1].key));
    }
}

TEST(RulesTests, arrivalElseChainDoesNotVisitClausesAfterATakenBranch)
{
    auto p = route();
    auto first = section(p, Kind::ifBlock, 0, 0);
    first.when = Timing::onArrival;
    ASSERT_TRUE(add(p, first));
    auto second = section(p, Kind::elseIf, 1, 2);
    second.parent = p.blocks[0].id;
    second.when = Timing::onArrival;
    ASSERT_TRUE(add(p, second));
    auto last = section(p, Kind::otherwise, 3, 3);
    last.parent = p.blocks[1].id;
    last.when = Timing::onArrival;
    ASSERT_TRUE(add(p, last));
    Runtime r;
    int checks = 0;
    Evaluate check = [&](const Condition&) { ++checks; return CheckResult{ true, true, {} }; };
    EXPECT_EQ(next(p, r, 0, check).order, p.rows[0].key);
    EXPECT_EQ(checks, 0);
    markArrived(p, r, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, check).order, p.rows[4].key);
    EXPECT_EQ(checks, 1);
    EXPECT_FALSE(r.branches.at(p.blocks[1].id));
    EXPECT_FALSE(r.branches.at(p.blocks[2].id));
    EXPECT_TRUE(validRuntime(p, r));
}

TEST(RulesTests, arrivalElseIfAndElseReachTheirOwnAnchorOnlyWhenNeeded)
{
    auto p = route();
    auto b = section(p, Kind::ifBlock, 0, 0);
    b.when = Timing::onArrival;
    ASSERT_TRUE(add(p, b));
    b = section(p, Kind::elseIf, 1, 2);
    b.parent = p.blocks[0].id;
    b.when = Timing::onArrival;
    ASSERT_TRUE(add(p, b));
    b = section(p, Kind::otherwise, 3, 3);
    b.parent = p.blocks[1].id;
    b.when = Timing::onArrival;
    ASSERT_TRUE(add(p, b));
    Runtime r;
    EXPECT_EQ(next(p, r, 0, no).order, p.rows[0].key);
    markArrived(p, r, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, no).order, p.rows[1].key);
    markArrived(p, r, p.rows[1].key);
    EXPECT_EQ(next(p, r, 0, no).order, p.rows[3].key);
    markArrived(p, r, p.rows[3].key);
    EXPECT_EQ(next(p, r, 0, no).order, p.rows[4].key);
    EXPECT_TRUE(r.branches.at(p.blocks[2].id));
}

TEST(RulesTests, arrivalRepeatCountsTheFirstPhysicalVisitAndRevisitsOnLaterPasses)
{
    auto p = route();
    auto b = section(p, Kind::repeat, 1, 2);
    b.when = Timing::onArrival;
    b.number = 2;
    ASSERT_TRUE(add(p, b));
    Runtime r;
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[1].key);
    EXPECT_TRUE(r.repeats.empty());
    markArrived(p, r, p.rows[1].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[2].key);
    EXPECT_EQ(r.repeats.at(p.blocks[0].id), 2);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[1].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[2].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[3].key);
    EXPECT_TRUE(r.repeats.empty());
}

TEST(RulesTests, waitTimingPlacesTheHoldBeforeOrAfterItsRouteSection)
{
    for (auto when : { Timing::beforeTravel, Timing::onArrival })
    {
        auto p = route();
        auto b = section(p, Kind::waitUntil, 1, 2);
        b.when = when;
        b.number = 2;
        ASSERT_TRUE(add(p, b));
        Runtime r;
        EXPECT_EQ(next(p, r, 10, no).order, p.rows[0].key);
        if (when == Timing::onArrival)
        {
            EXPECT_EQ(next(p, r, 10, no).order, p.rows[1].key);
            EXPECT_EQ(next(p, r, 10, no).order, p.rows[2].key);
            EXPECT_TRUE(requiresArrivalStop(p, r));
            markArrived(p, r, p.rows[2].key);
        }
        EXPECT_EQ(next(p, r, 10, no).kind, RunKind::waiting);
        EXPECT_EQ(next(p, r, 89, no).kind, RunKind::waiting);
        EXPECT_EQ(next(p, r, 90, no).order, p.rows[when == Timing::beforeTravel ? 1 : 3].key);
    }
}

TEST(RulesTests, stationArrivalDoesNotEvaluateTheNextTripsChecksBeforeCargoHandling)
{
    auto p = makeProgram({ 1, 4, 9, 17 }, true);
    auto b = section(p, Kind::ifBlock, 2, 2);
    b.when = Timing::beforeTravel;
    ASSERT_TRUE(add(p, b));
    Runtime r;
    int checks = 0;
    Evaluate check = [&](const Condition&) { ++checks; return CheckResult{ true, false, {} }; };
    EXPECT_EQ(next(p, r, 0, check).order, p.rows[0].key);
    markArrived(p, r, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, check, p.rows[0].key).order, p.rows[1].key);
    EXPECT_EQ(next(p, r, 0, check, p.rows[0].key).kind, RunKind::departure);
    EXPECT_EQ(checks, 0);
    EXPECT_EQ(next(p, r, 1, check).order, p.rows[3].key);
    EXPECT_EQ(checks, 1);
}

TEST(RulesTests, arrivalCargoInstructionsUseTheAlreadyReachedStationAnchor)
{
    auto p = makeProgram({ 1, 4, 9 }, true);
    auto b = section(p, Kind::ifBlock, 0, 1);
    b.when = Timing::onArrival;
    ASSERT_TRUE(add(p, b));
    auto cargo = section(p, Kind::ifBlock, 1, 1);
    cargo.when = Timing::onArrival;
    ASSERT_TRUE(add(p, cargo));
    Runtime r;
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[0].key);
    markArrived(p, r, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, yes, p.rows[0].key).order, p.rows[1].key);
    EXPECT_EQ(r.arrived, p.rows[0].key);
    EXPECT_TRUE(r.branches.at(p.blocks[1].id));
    EXPECT_EQ(next(p, r, 0, yes, p.rows[0].key).kind, RunKind::departure);
    EXPECT_EQ(next(p, r, 1, yes).order, p.rows[2].key);
}

TEST(RulesTests, arrivalTimingValidationAndReadableSentencesAreConsistent)
{
    auto p = route();
    auto b = section(p, Kind::ifBlock, 0, 1);
    b.when = static_cast<Timing>(3);
    auto before = p;
    EXPECT_FALSE(add(p, b));
    EXPECT_EQ(p, before);
    b.when = Timing::onArrival;
    ASSERT_TRUE(add(p, b));
    EXPECT_NE(blockSentence(p, p.blocks[0]).find("[on arrival]"), std::string::npos);
    b = section(p, Kind::waitUntil, 2, 3);
    ASSERT_TRUE(add(p, b));
    EXPECT_EQ(timingOf(p, p.blocks[1]), Timing::onArrival);
    EXPECT_EQ(arrivalAnchor(p, p.blocks[1]), p.rows[3].key);
    b = section(p, Kind::group, 0, 1);
    b.when = Timing::onArrival;
    EXPECT_FALSE(add(p, b));
    auto onlyCargo = makeProgram({ 4, 5 }, true);
    EXPECT_FALSE(add(onlyCargo, section(onlyCargo, Kind::waitUntil, 0, 1)));
}

TEST_F(GameRulesFixture, waypointWaitDoesNotArmOrChangeSpeedUntilPhysicalArrival)
{
    using namespace OpenLoco;
    using namespace OpenLoco::Literals;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    Vehicles::OrderRouteWaypoint waypoint({ 2, 1 }, 0, 0, 0);
    Vehicles::OrderManager::insertOrder(head, 0, &waypoint);
    head->currentOrder = 0;
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::waitUntil, 0, 0);
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    e.block.number = 2;
    ASSERT_TRUE(submit(owner, e));
    ASSERT_TRUE(prepareVehicle(id));
    Vehicles::Vehicle train(*head);
    train.veh2->currentSpeed = 40.0_mph;
    train.veh2->routingHandle = head->routingHandle;
    train.veh2->subPosition = head->subPosition + 1;
    ASSERT_TRUE(planArrival(id));
    EXPECT_EQ(train.veh2->currentSpeed, 40.0_mph);
    EXPECT_EQ(head->currentOrder, 0);
    EXPECT_EQ(runtimeForVehicle(id)->waiting, 0U);
    EXPECT_FALSE(finishApproach(id));
    ScenarioManager::setScenarioTicks(100);
    train.veh2->currentSpeed = 0.0_mph;
    EXPECT_FALSE(finishApproach(id));
    EXPECT_EQ(runtimeForVehicle(id)->waiting, 0U);
    train.veh2->subPosition = head->subPosition;
    EXPECT_FALSE(finishApproach(id));
    ASSERT_NE(runtimeForVehicle(id)->waiting, 0U);
    EXPECT_EQ(runtimeForVehicle(id)->waitStart, 100U);
    ScenarioManager::setScenarioTicks(179);
    EXPECT_FALSE(finishApproach(id));
    ScenarioManager::setScenarioTicks(180);
    EXPECT_TRUE(finishApproach(id));
    EXPECT_EQ(head->currentOrder, 6);
    EXPECT_FALSE(isApproaching(id));
}

TEST_F(GameRulesFixture, arrivalConditionIsLatchedBeforeStationCargoChangesIt)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    Vehicles::OrderUnloadAll unload(0);
    Vehicles::OrderManager::insertOrder(head, 2, &unload);
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::ifBlock, 0, 1);
    e.block.when = Timing::onArrival;
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    ASSERT_TRUE(prepareVehicle(id));
    EXPECT_TRUE(runtimeForVehicle(id)->branches.empty());
    reached(id);
    head->status = Vehicles::Status::unloading;
    head->currentOrder = 2;
    processStationArrival(id);
    EXPECT_FALSE(runtimeForVehicle(id)->branches.at(find(owner)->blocks[0].id));
    EXPECT_FALSE(cargoOrderEnabled(id, 2));
    EXPECT_EQ(head->currentOrder, 2);
    head->status = Vehicles::Status::loading;
    processStationArrival(id);
    EXPECT_FALSE(cargoOrderEnabled(id, 2));
    EXPECT_FALSE(holdAtStation(id));
    EXPECT_EQ(head->currentOrder, 3);
}

TEST_F(GameRulesFixture, departureChecksObserveCargoAfterLoadingRatherThanOnArrival)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::ifBlock, 1, 1);
    e.block.when = Timing::beforeTravel;
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    reached(id);
    head->status = Vehicles::Status::unloading;
    head->currentOrder = 2;
    processStationArrival(id);
    EXPECT_TRUE(runtimeForVehicle(id)->branches.empty());
    EXPECT_EQ(head->currentOrder, 2);
    EXPECT_FALSE(holdAtStation(id));
    EXPECT_FALSE(runtimeForVehicle(id)->branches.at(find(owner)->blocks[0].id));
    EXPECT_EQ(head->currentOrder, 4);
}

TEST_F(GameRulesFixture, approachingArrivalCheckAndItsTimingRoundTrip)
{
    using namespace OpenLoco;
    using namespace OpenLoco::Literals;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::ifBlock, 1, 1);
    e.block.when = Timing::onArrival;
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    reached(id);
    ASSERT_TRUE(prepareVehicle(id));
    Vehicles::Vehicle train(*head);
    train.veh2->currentSpeed = 25.0_mph;
    ASSERT_TRUE(planArrival(id));
    const auto path = std::filesystem::temp_directory_path() / "openloco_arrival_phase_test.sv5";
    const auto saved = *find(owner);
    ASSERT_TRUE(saveCompanion(path));
    clear();
    ASSERT_TRUE(loadCompanion(path));
    ASSERT_NE(find(owner), nullptr);
    EXPECT_EQ(*find(owner), saved);
    EXPECT_TRUE(isApproaching(id));
    EXPECT_NE(runtimeForVehicle(id)->awaitingArrival, 0U);
    EXPECT_FALSE(finishApproach(id));
    EXPECT_EQ(train.veh2->currentSpeed, 25.0_mph);
    train.veh2->currentSpeed = 0.0_mph;
    train.veh2->routingHandle = head->routingHandle;
    train.veh2->subPosition = head->subPosition;
    EXPECT_TRUE(finishApproach(id));
    EXPECT_EQ(head->currentOrder, 4);
    std::filesystem::remove(companionPath(path));
}

TEST_F(GameRulesFixture, legacyCompanionsStillLoadAndUnknownPhaseDataNeverAttaches)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::repeat, 0, 1);
    e.block.number = 2;
    ASSERT_TRUE(submit(owner, e));
    const auto path = std::filesystem::temp_directory_path() / "openloco_phase_legacy_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    std::ifstream in(companionPath(path));
    std::string text((std::istreambuf_iterator<char>(in)), {});
    in.close();
    const auto extension = text.find("PHASES 1");
    ASSERT_NE(extension, std::string::npos);
    text.resize(extension);
    {
        std::ofstream out(companionPath(path));
        out << text;
    }
    clear();
    ASSERT_TRUE(loadCompanion(path));
    ASSERT_NE(find(owner), nullptr);
    EXPECT_EQ(find(owner)->blocks[0].when, Timing::automatic);
    {
        std::ofstream out(companionPath(path));
        out << text << "PHASES 1 1\n0 " << id << ' ' << find(owner)->blocks[0].id << " 9\n0\n";
    }
    EXPECT_FALSE(loadCompanion(path));
    EXPECT_EQ(find(owner), nullptr);
    std::filesystem::remove(companionPath(path));
}

TEST_F(GameRulesFixture, beforeTravelWaitBrakesAtThePreviousBoundaryAndRestartsItsClockThere)
{
    using namespace OpenLoco;
    using namespace OpenLoco::Literals;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::waitUntil, 1, 1);
    e.block.when = Timing::beforeTravel;
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    Vehicles::Vehicle train(*head);
    train.veh2->currentSpeed = 40.0_mph;
    passed(id);
    ASSERT_TRUE(prepareVehicle(id));
    EXPECT_TRUE(isApproaching(id));
    EXPECT_EQ(head->currentOrder, 0);
    EXPECT_EQ(train.veh2->currentSpeed, 40.0_mph);
    EXPECT_EQ(head->status, Vehicles::Status::approaching);
    ScenarioManager::setScenarioTicks(100);
    train.veh2->currentSpeed = 0.0_mph;
    train.veh2->routingHandle = head->routingHandle;
    train.veh2->subPosition = head->subPosition;
    EXPECT_FALSE(finishApproach(id));
    EXPECT_EQ(runtimeForVehicle(id)->waitStart, 100U);
    ScenarioManager::setScenarioTicks(139);
    EXPECT_FALSE(finishApproach(id));
    ScenarioManager::setScenarioTicks(140);
    EXPECT_TRUE(finishApproach(id));
    EXPECT_EQ(head->currentOrder, 2);
}

TEST_F(GameRulesFixture, stationWaitKeepsCargoHandlingAndTimeoutWhileDepartureCursorStaysSafe)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    Vehicles::OrderUnloadAll unload(0);
    Vehicles::OrderManager::insertOrder(head, 2, &unload);
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::waitUntil, 0, 1);
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    reached(id);
    head->status = Vehicles::Status::unloading;
    head->currentOrder = 2;
    processStationArrival(id);
    EXPECT_NE(runtimeForVehicle(id)->waiting, 0U);
    EXPECT_TRUE(prepareVehicle(id));
    EXPECT_TRUE(cargoOrderEnabled(id, 2));
    EXPECT_EQ(head->currentOrder, 2);
    EXPECT_TRUE(holdAtStation(id));
    ScenarioManager::setScenarioTicks(40);
    processStationArrival(id);
    EXPECT_EQ(runtimeForVehicle(id)->waiting, 0U);
    EXPECT_EQ(head->currentOrder, 2);
    EXPECT_FALSE(holdAtStation(id));
    EXPECT_EQ(head->currentOrder, 3);
    EXPECT_NE(vehicleStatus(id).find("Time limit reached"), std::string::npos);
}

TEST_F(GameRulesFixture, savedWaitAtPhysicalWaypointResumesWithoutRearmingItsTimer)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::waitUntil, 0, 0);
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    ASSERT_TRUE(planArrival(id));
    Vehicles::Vehicle train(*head);
    train.veh2->routingHandle = head->routingHandle;
    train.veh2->subPosition = head->subPosition;
    ScenarioManager::setScenarioTicks(10);
    ASSERT_FALSE(finishApproach(id));
    const auto path = std::filesystem::temp_directory_path() / "openloco_arrived_wait_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    clear();
    ASSERT_TRUE(loadCompanion(path));
    ScenarioManager::setScenarioTicks(49);
    EXPECT_FALSE(finishApproach(id));
    EXPECT_EQ(runtimeForVehicle(id)->waitStart, 10U);
    ScenarioManager::setScenarioTicks(50);
    EXPECT_TRUE(finishApproach(id));
    EXPECT_EQ(head->currentOrder, 2);
    std::filesystem::remove(companionPath(path));
}

TEST_F(GameRulesFixture, sharedLineArrivalAndWaitStateIsIndependentAndDetachCopiesTiming)
{
    using namespace OpenLoco;
    auto* a = vehicle();
    auto* b = vehicle();
    const auto ai = enumValue(a->id), bi = enumValue(b->id);
    const auto line = Lines::createLine("Physical arrival", TransportMode::rail, 2);
    for (uint16_t station : { 10, 20, 30 })
    {
        Lines::LineOrder o;
        o.station = station;
        ASSERT_TRUE(Lines::addOrder(line.id, o).ok);
    }
    ASSERT_TRUE(Lines::assignVehicle(line.id, ai, TransportMode::rail).ok);
    ASSERT_TRUE(Lines::assignVehicle(line.id, bi, TransportMode::rail).ok);
    const Owner owner{ OwnerKind::line, line.id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::waitUntil, 0, 0);
    e.block.when = Timing::onArrival;
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    ASSERT_TRUE(planArrival(ai));
    ASSERT_TRUE(planArrival(bi));
    Vehicles::Vehicle ta(*a), tb(*b);
    ta.veh2->routingHandle = a->routingHandle;
    ta.veh2->subPosition = a->subPosition;
    tb.veh2->routingHandle = b->routingHandle;
    tb.veh2->subPosition = b->subPosition;
    ScenarioManager::setScenarioTicks(10);
    EXPECT_FALSE(finishApproach(ai));
    EXPECT_EQ(runtimeForVehicle(bi)->waiting, 0U);
    ScenarioManager::setScenarioTicks(20);
    EXPECT_FALSE(finishApproach(bi));
    ScenarioManager::setScenarioTicks(50);
    EXPECT_TRUE(finishApproach(ai));
    EXPECT_FALSE(finishApproach(bi));
    EXPECT_EQ(runtimeForVehicle(bi)->waitStart, 20U);
    ASSERT_TRUE(Lines::unassignVehicle(bi).ok);
    ASSERT_NE(find({ OwnerKind::vehicle, bi }), nullptr);
    EXPECT_EQ(find({ OwnerKind::vehicle, bi })->blocks[0].when, Timing::onArrival);
    EXPECT_FALSE(isApproaching(bi));
}

TEST(RulesTests, arrivalDecisionBudgetPausePreservesTheAlreadyVisitedEntry)
{
    auto p = route();
    auto b = section(p, Kind::ifBlock, 1, 2);
    b.when = Timing::onArrival;
    ASSERT_TRUE(add(p, b));
    Runtime r;
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[1].key);
    markArrived(p, r, p.rows[1].key);
    r.steps = 255;
    EXPECT_EQ(next(p, r, 0, yes).kind, RunKind::yielded);
    EXPECT_EQ(r.arrivalRow, p.rows[1].key);
    EXPECT_TRUE(validRuntime(p, r));
    const auto saved = r;
    EXPECT_EQ(next(p, r, 1, yes).order, p.rows[2].key);
    auto restored = saved;
    EXPECT_EQ(next(p, restored, 1, yes).order, p.rows[2].key);
    EXPECT_EQ(restored.pc, r.pc);
    EXPECT_EQ(restored.branches, r.branches);
}

TEST_F(GameRulesFixture, editingAndRemovingAWaitAtItsReachedPointDoesNotLeaveAStaleApproach)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::waitUntil, 0, 0);
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    ASSERT_TRUE(planArrival(id));
    Vehicles::Vehicle train(*head);
    train.veh2->routingHandle = head->routingHandle;
    train.veh2->subPosition = head->subPosition;
    ASSERT_FALSE(finishApproach(id));
    e.kind = EditKind::updateBlock;
    e.block = find(owner)->blocks[0];
    e.block.condition.compare = Compare::equal;
    e.block.condition.value = getCurrentYear();
    ASSERT_TRUE(submit(owner, e));
    EXPECT_TRUE(finishApproach(id));
    EXPECT_FALSE(isApproaching(id));
    EXPECT_EQ(head->currentOrder, 2);
    reached(id);
    ASSERT_TRUE(prepareVehicle(id));
    reached(id);
    ASSERT_TRUE(prepareVehicle(id));
    ASSERT_TRUE(planArrival(id));
    e.kind = EditKind::removeBlock;
    ASSERT_TRUE(submit(owner, e));
    EXPECT_TRUE(prepareVehicle(id));
    EXPECT_FALSE(isApproaching(id));
    EXPECT_EQ(runtimeForVehicle(id), nullptr);
}

TEST_F(GameRulesFixture, legacyPrematureWaitFinishesThePhysicalApproachBeforeRestartingTimeout)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::waitUntil, 0, 0);
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::less;
    e.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, e));
    reached(id);
    ASSERT_FALSE(prepareVehicle(id));
    Vehicles::Vehicle train(*head);
    train.veh2->routingHandle = head->routingHandle;
    train.veh2->subPosition = head->subPosition + 1;
    const auto path = std::filesystem::temp_directory_path() / "openloco_legacy_approach_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    std::ifstream in(companionPath(path));
    std::string text((std::istreambuf_iterator<char>(in)), {});
    in.close();
    text.resize(text.find("PHASES 1"));
    {
        std::ofstream out(companionPath(path));
        out << text;
    }
    clear();
    ASSERT_TRUE(loadCompanion(path));
    EXPECT_TRUE(isApproaching(id));
    EXPECT_EQ(runtimeForVehicle(id)->waiting, 0U);
    EXPECT_FALSE(finishApproach(id));
    ScenarioManager::setScenarioTicks(100);
    train.veh2->subPosition = head->subPosition;
    EXPECT_FALSE(finishApproach(id));
    EXPECT_EQ(runtimeForVehicle(id)->waitStart, 100U);
    ScenarioManager::setScenarioTicks(140);
    EXPECT_TRUE(finishApproach(id));
    std::filesystem::remove(companionPath(path));
}

TEST_F(GameRulesFixture, savingDuringAnArrivalDecisionBudgetPauseResumesAtTheSamePoint)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::ifBlock, 0, 0);
    e.block.when = Timing::onArrival;
    e.block.condition.check = Check::year;
    e.block.condition.compare = Compare::equal;
    e.block.condition.value = getCurrentYear();
    ASSERT_TRUE(submit(owner, e));
    e.block = section(*find(owner), Kind::goTo, 0, 0);
    e.block.parent = find(owner)->blocks[0].id;
    e.block.target = find(owner)->rows[0].key;
    ASSERT_TRUE(submit(owner, e));
    ASSERT_TRUE(planArrival(id));
    Vehicles::Vehicle train(*head);
    train.veh2->routingHandle = head->routingHandle;
    train.veh2->subPosition = head->subPosition;
    EXPECT_FALSE(finishApproach(id));
    ASSERT_EQ(runtimeForVehicle(id)->steps, 256);
    ASSERT_NE(runtimeForVehicle(id)->arrived, 0U);
    const auto before = *runtimeForVehicle(id);
    const auto path = std::filesystem::temp_directory_path() / "openloco_arrival_budget_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    clear();
    ASSERT_TRUE(loadCompanion(path));
    ASSERT_NE(runtimeForVehicle(id), nullptr);
    EXPECT_EQ(runtimeForVehicle(id)->pc, before.pc);
    EXPECT_EQ(runtimeForVehicle(id)->arrived, before.arrived);
    EXPECT_EQ(runtimeForVehicle(id)->arrivalRow, before.arrivalRow);
    ScenarioManager::setScenarioTicks(1);
    EXPECT_FALSE(finishApproach(id));
    EXPECT_EQ(runtimeForVehicle(id)->steps, 256);
    EXPECT_EQ(head->currentOrder, 0);
    std::filesystem::remove(companionPath(path));
}

TEST(RulesTests, onArrivalBreakExitsAnInfiniteLoopOnlyAfterReachingItsWaypoint)
{
    auto p = route();
    auto loop = section(p, Kind::repeat, 0, 2);
    loop.number = 0;
    ASSERT_TRUE(add(p, loop));
    auto b = section(p, Kind::breakLoop, 1, 1);
    b.when = Timing::onArrival;
    b.target = p.blocks[0].id;
    ASSERT_TRUE(add(p, b));
    Runtime r;
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[1].key);
    EXPECT_FALSE(r.repeats.empty());
    markArrived(p, r, p.rows[1].key);
    EXPECT_EQ(next(p, r, 0, yes).order, p.rows[3].key);
    EXPECT_TRUE(r.repeats.empty());
    EXPECT_FALSE(r.skipped.contains(p.rows[1].key));
    EXPECT_TRUE(r.skipped.contains(p.rows[2].key));
}

TEST(RulesTests, repeatedArrivalChecksRequireANewPhysicalVisitOnEveryIteration)
{
    auto p = route();
    auto loop = section(p, Kind::repeat, 1, 1);
    loop.number = 2;
    ASSERT_TRUE(add(p, loop));
    auto b = section(p, Kind::ifBlock, 1, 1);
    b.when = Timing::onArrival;
    ASSERT_TRUE(add(p, b));
    Runtime r;
    int checks = 0;
    Evaluate check = [&](const Condition&) { ++checks; return CheckResult{ true, true, {} }; };
    EXPECT_EQ(next(p, r, 0, check).order, p.rows[0].key);
    EXPECT_EQ(next(p, r, 0, check).order, p.rows[1].key);
    markArrived(p, r, p.rows[1].key);
    EXPECT_EQ(next(p, r, 0, check).order, p.rows[1].key);
    EXPECT_EQ(checks, 1);
    EXPECT_EQ(r.arrived, 0U);
    markArrived(p, r, p.rows[1].key);
    EXPECT_EQ(next(p, r, 0, check).order, p.rows[2].key);
    EXPECT_EQ(checks, 2);
}

TEST_F(GameRulesFixture, truncatedAndInvalidPhysicalPhaseRecordsAreRejectedBeforeAttachment)
{
    using namespace OpenLoco;
    auto* head = vehicle();
    const auto id = enumValue(head->id);
    const Owner owner{ OwnerKind::vehicle, id };
    auto p = snapshot(owner);
    Edit e;
    e.block = section(p, Kind::waitUntil, 0, 0);
    ASSERT_TRUE(submit(owner, e));
    const auto path = std::filesystem::temp_directory_path() / "openloco_corrupt_phases_test.sv5";
    ASSERT_TRUE(saveCompanion(path));
    std::ifstream in(companionPath(path));
    std::string text((std::istreambuf_iterator<char>(in)), {});
    in.close();
    text.resize(text.find("PHASES 1"));
    const std::string phases[] = {
        "PHASES 1 0\n0\n",                                              // Omits the saved vehicle's phase record.
        "PHASES 1 0\n1\n" + std::to_string(id) + " 0 0 0 999999 0 0\n", // Missing physical anchor.
        "PHASES 1 0\n1\n" + std::to_string(id) + " 0 0 0 0 1 0\n",      // Reached with no approach anchor.
        "PHASES 1 0\n1\n" + std::to_string(id) + " 999999 0 0 0 0 0\n", // Missing pending arrival block.
        "PHASES 1 0\n1\n" + std::to_string(id) + " 0 0 999999 0 0 0\n"  // Invalid consumed arrival row.
    };
    for (const auto& extension : phases)
    {
        {
            std::ofstream out(companionPath(path));
            out << text << extension;
        }
        EXPECT_FALSE(loadCompanion(path));
        EXPECT_EQ(find(owner), nullptr);
        EXPECT_EQ(runtimeForVehicle(id), nullptr);
    }
    std::filesystem::remove(companionPath(path));
}
