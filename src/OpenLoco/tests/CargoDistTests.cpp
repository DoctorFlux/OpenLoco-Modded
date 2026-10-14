#include <OpenLoco/CargoDist/CargoDist.h>
#include <OpenLoco/CargoDist/Game.h>
#include <OpenLoco/CargoDist/Save.h>
#include <OpenLoco/CargoDist/View.h>
#include <OpenLoco/Entities/EntityManager.h>
#include <OpenLoco/GameCommands/GameCommands.h>
#include <OpenLoco/Lines/Line.h>
#include <OpenLoco/Localisation/StringIds.h>
#include <OpenLoco/Random.h>
#include <OpenLoco/Rules/Rules.h>
#include <OpenLoco/Vehicles/OrderManager.h>
#include <OpenLoco/Vehicles/Orders.h>
#include <OpenLoco/Vehicles/VehicleBody.h>
#include <OpenLoco/Vehicles/VehicleBogie.h>
#include <OpenLoco/Vehicles/VehicleHead.h>
#include <OpenLoco/World/CompanyManager.h>
#include <OpenLoco/World/StationManager.h>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

using namespace OpenLoco::CargoDist;
namespace CargoDist = OpenLoco::CargoDist;
namespace
{
    State network()
    {
        State state;
        Station a;
        a.id = 1;
        a.x = 32;
        a.supply[0] = 120;
        Station b;
        b.id = 2;
        b.x = 64;
        Station c;
        c.id = 3;
        c.x = 96;
        c.accepts = 1;
        state.setStation(a);
        state.setStation(b);
        state.setStation(c);
        state.setService(10, { { 0, 0, 1, 2, 60, 8, 0 }, { 0, 0, 2, 1, 60, 8, 0 } });
        state.setService(20, { { 0, 0, 2, 3, 60, 5, 0 }, { 0, 0, 3, 2, 60, 5, 0 } });
        return state;
    }
    void calculate(State& state, size_t budget = 4096)
    {
        for (size_t i = 0; i < 100000 && (state.dirty || state.job); ++i)
        {
            state.calculate(budget);
        }
        ASSERT_FALSE(state.job);
        ASSERT_FALSE(state.dirty);
    }
    const Holder source{ false, 1, 0, 0 }, compartment{ true, 40, 0, 0 };
    std::map<Holder, std::pair<uint64_t, uint32_t>> quantities(const State& state)
    {
        std::map<Holder, std::pair<uint64_t, uint32_t>> result;
        for (const auto& [holder, holding] : state.holdings)
        {
            result[holder] = { holding.identity, state.quantity(holder) };
        }
        return result;
    }
    std::string encode(const State& state)
    {
        std::ostringstream out;
        EXPECT_TRUE(writeState(out, state, 42));
        return out.str();
    }
    uint64_t stop(uint16_t station)
    {
        return OpenLoco::Vehicles::OrderStopAt(static_cast<OpenLoco::StationId>(station)).getRaw();
    }
}
TEST(CargoDistTests, connectedDestinationUsesTransferHop)
{
    auto s = network();
    calculate(s);
    EXPECT_EQ(s.nextHop(0, 0, 1, 3), 2);
    EXPECT_EQ(s.nextHop(0, 0, 2, 3), 3);
    EXPECT_EQ(s.nextHop(1, 0, 1, 3), kUnknownStation);
}
TEST(CargoDistTests, partialLoadConservesStationAndCompartment)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 120, 1, 0);
    s.reconcile(compartment, 2, 0, kUnknownStation, 0);
    EXPECT_EQ(s.load(source, compartment, 17, { 2 }), 17);
    EXPECT_EQ(s.quantity(source), 103);
    EXPECT_EQ(s.quantity(compartment), 17);
    EXPECT_EQ(s.destined(compartment, 3), 17);
}
TEST(CargoDistTests, wrongServiceCannotBoardCargo)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 12, 1, 0);
    EXPECT_EQ(s.load(source, compartment, 12, { 1 }), 0);
    EXPECT_EQ(s.quantity(source), 12);
}
TEST(CargoDistTests, automaticTransferRetainsOriginAndCredits)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 12, 1, 4);
    s.load(source, compartment, 12, { 2 });
    const auto moved = s.unload(compartment, 2, { 1, 2 }, 100, [](const Packet&) { return 100; });
    EXPECT_EQ(moved.transferred, 12);
    EXPECT_EQ(moved.virtualCredit, 75);
    EXPECT_TRUE(moved.delivered.empty());
    const auto& packet = s.holdings.at({ false, 2, 0, 0 }).packets.front();
    EXPECT_EQ(packet.origin, 1);
    EXPECT_EQ(packet.destination, 3);
    EXPECT_EQ(packet.age, 4);
    EXPECT_EQ(packet.credit, 75);
}
TEST(CargoDistTests, throughCargoStaysOnVehicle)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 12, 1, 0);
    s.load(source, compartment, 12, { 2 });
    const auto before = s.holdings;
    auto result = s.unload(compartment, 2, { 3 }, 100, [](const Packet&) { return 100; });
    EXPECT_EQ(result.transferred, 0);
    EXPECT_EQ(s.quantity(compartment), 12);
    EXPECT_EQ(s.holdings.at(compartment), before.at(compartment));
}
TEST(CargoDistTests, finalDeliveryCarriesPriorCreditExactlyOnce)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 12, 1, 0);
    s.load(source, compartment, 12, { 2 });
    s.unload(compartment, 2, {}, 100, [](const Packet&) { return 100; });
    const Holder transfer{ false, 2, 0, 0 }, onward{ true, 41, 0, 0 };
    ASSERT_EQ(s.load(transfer, onward, 12, { 3 }), 12);
    auto result = s.unload(onward, 3, {}, 100, [](const Packet&) { return 200; });
    ASSERT_EQ(result.delivered.size(), 1);
    EXPECT_EQ(result.delivered.front().credit, 75);
    EXPECT_EQ(result.delivered.front().quantity, 12);
    EXPECT_TRUE(s.unload(onward, 3, {}, 100, [](const Packet&) { return 200; }).delivered.empty());
}
TEST(CargoDistTests, transferRoomLimitsDoNotDestroyOnboardCargo)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 12, 1, 0);
    s.load(source, compartment, 12, { 2 });
    auto result = s.unload(compartment, 2, {}, 5, [](const Packet&) { return 20; });
    EXPECT_EQ(result.transferred, 5);
    EXPECT_EQ(s.quantity(compartment), 7);
    EXPECT_EQ(s.quantity({ false, 2, 0, 0 }), 5);
}
TEST(CargoDistTests, creditSplitsPreserveEveryCurrencyUnit)
{
    Packet packet{ 17, 1, 3, 4, 101, 32, 0 };
    auto first = split(packet, 6), second = split(packet, 4), last = split(packet, 100);
    EXPECT_EQ(first.quantity + second.quantity + last.quantity, 17);
    EXPECT_EQ(first.credit + second.credit + last.credit, 101);
    EXPECT_EQ(packet.credit, 0);
}
TEST(CargoDistTests, nativeLossAndNewProductionReconcileExactly)
{
    State s;
    s.reconcile(source, 1, 100, 1, 0);
    s.reconcile(source, 1, 60, 1, 0);
    EXPECT_EQ(s.quantity(source), 60);
    s.reconcile(source, 1, 90, 1, 0);
    EXPECT_EQ(s.quantity(source), 90);
    EXPECT_EQ(s.packetCount(), 1);
}
TEST(CargoDistTests, reusedEntityIdentityDiscardsOldDestinationsAndCredit)
{
    State s;
    s.reconcile(compartment, 1, 5, 1, 0);
    auto& packet = s.holdings.at(compartment).packets.front();
    packet.destination = 3;
    packet.credit = 30;
    s.reconcile(compartment, 2, 7, 2, 0);
    EXPECT_EQ(s.quantity(compartment), 7);
    EXPECT_EQ(s.holdings.at(compartment).packets.front().origin, 2);
    EXPECT_EQ(s.holdings.at(compartment).packets.front().credit, 0);
}
TEST(CargoDistTests, detachedOrDeletedComponentCannotLeavePackets)
{
    State s;
    s.reconcile(compartment, 1, 5, 1, 0);
    s.forgetComponent(compartment.id);
    EXPECT_EQ(s.quantity(compartment), 0);
}
TEST(CargoDistTests, missingDestinationsBecomeUnassignedWithoutQuantityLoss)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 12, 1, 0);
    s.assign(source);
    s.removeStation(3);
    EXPECT_EQ(s.quantity(source), 12);
    EXPECT_EQ(s.holdings.at(source).packets.front().destination, kUnknownStation);
    EXPECT_EQ(s.nextHop(0, 0, 1, 3), kUnknownStation);
}
TEST(CargoDistTests, disconnectedProductionWaitsForConnection)
{
    auto s = network();
    s.removeService(20);
    calculate(s);
    s.reconcile(source, 1, 12, 1, 0);
    EXPECT_FALSE(s.assign(source));
    EXPECT_EQ(s.quantity(source), 12);
    s.setService(20, { { 0, 0, 2, 3, 60, 5, 0 } });
    calculate(s);
    EXPECT_TRUE(s.assign(source));
    EXPECT_EQ(s.destined(source, 3), 12);
}
TEST(CargoDistTests, multipleAcceptingDestinationsShareSupply)
{
    auto s = network();
    auto fourth = s.stations.at(3);
    fourth.id = 4;
    s.setStation(fourth);
    s.setService(30, { { 0, 0, 2, 4, 60, 5, 0 } });
    calculate(s);
    s.reconcile(source, 1, 100, 1, 0);
    ASSERT_TRUE(s.assign(source));
    EXPECT_EQ(s.destined(source, 3), 50);
    EXPECT_EQ(s.destined(source, 4), 50);
}
TEST(CargoDistTests, crowdedShortRouteCanUseLongerAlternative)
{
    auto s = network();
    s.setService(30, { { 0, 0, 1, 3, 1, 14, 0 } });
    calculate(s);
    s.reconcile(source, 1, 120, 1, 0);
    s.load(source, compartment, 60, { 2 });
    EXPECT_EQ(s.nextHop(0, 0, 1, 3), 3);
}
TEST(CargoDistTests, symmetricModeWeightsDestinationProduction)
{
    auto s = network();
    auto fourth = s.stations.at(3);
    fourth.id = 4;
    fourth.supply[0] = 3;
    s.setStation(fourth);
    s.setService(30, { { 0, 0, 2, 4, 60, 5, 0 } });
    auto settings = s.settings;
    settings.modes[0] = Mode::symmetric;
    ASSERT_TRUE(s.configure(settings));
    calculate(s);
    s.reconcile(source, 1, 100, 1, 0);
    s.assign(source);
    EXPECT_EQ(s.destined(source, 4), 75);
    EXPECT_EQ(s.destined(source, 3), 25);
}
TEST(CargoDistTests, manualSettingIsValidatedAndKeepsNativeQuantities)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 12, 1, 0);
    s.assign(source);
    auto settings = s.settings;
    settings.modes[0] = Mode::manual;
    EXPECT_TRUE(s.configure(settings));
    EXPECT_EQ(s.quantity(source), 12);
    EXPECT_FALSE(s.assign(source));
    EXPECT_EQ(s.holdings.at(source).packets.front().destination, kUnknownStation);
    settings.accuracy = 0;
    const auto before = s.settings;
    EXPECT_FALSE(s.configure(settings));
    EXPECT_EQ(s.settings, before);
}
TEST(CargoDistTests, budgetPausesProduceIdenticalRoutes)
{
    auto whole = network(), paused = network();
    calculate(whole);
    calculate(paused, 1);
    EXPECT_EQ(whole.routes, paused.routes);
}
TEST(CargoDistTests, recalculationKeepsPublishedSnapshotUntilComplete)
{
    auto s = network();
    calculate(s);
    const auto before = s.routes;
    s.setService(30, { { 0, 0, 1, 3, 20, 2, 0 } });
    s.calculate(1);
    ASSERT_TRUE(s.job);
    EXPECT_EQ(s.routes, before);
    calculate(s);
    EXPECT_EQ(s.nextHop(0, 0, 1, 3), 3);
}
TEST(CargoDistTests, mixedOriginsSurvivePartialLoadingAndLoss)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 10, 1, 4);
    s.reconcile(source, 1, 20, 2, 8);
    ASSERT_EQ(s.load(source, compartment, 15, { 2 }), 15);
    ASSERT_EQ(s.holdings.at(compartment).packets.size(), 2);
    EXPECT_EQ(s.holdings.at(compartment).packets[1].origin, 2);
    EXPECT_EQ(s.holdings.at(compartment).packets[1].age, 8);
    s.reconcile(compartment, 0, 8, 1, 0);
    EXPECT_EQ(s.quantity(compartment), 8);
}
TEST(CargoDistTests, agesSaturateAndOriginWaitingDoesNotGainTransitAge)
{
    State s;
    s.reconcile(source, 1, 3, 1, 0);
    s.age(source);
    EXPECT_EQ(s.holdings.at(source).packets.front().age, 0);
    s.reconcile(compartment, 1, 3, 1, 254);
    s.age(compartment);
    s.age(compartment);
    EXPECT_EQ(s.holdings.at(compartment).packets.front().age, 255);
}
TEST(CargoDistTests, saveReloadResumesPendingCalculation)
{
    auto s = network();
    s.reconcile(source, 1, 12, 1, 0);
    s.calculate(2);
    ASSERT_TRUE(s.job);
    const auto bytes = encode(s);
    std::istringstream input(bytes);
    State restored;
    ASSERT_TRUE(readState(input, restored, 42, quantities(s)));
    EXPECT_EQ(s.job, restored.job);
    calculate(s, 2);
    calculate(restored, 2);
    EXPECT_EQ(s.routes, restored.routes);
}
TEST(CargoDistTests, saveReloadDuringTransferPreservesCreditAndQuantity)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 12, 1, 3);
    s.load(source, compartment, 12, { 2 });
    s.unload(compartment, 2, {}, 100, [](const Packet&) { return 100; });
    std::istringstream input(encode(s));
    State restored;
    ASSERT_TRUE(readState(input, restored, 42, quantities(s)));
    EXPECT_EQ(restored.holdings, s.holdings);
    EXPECT_EQ(restored.settings, s.settings);
}
TEST(CargoDistTests, wrongSaveHashRejectsWithoutMutatingExistingState)
{
    auto s = network();
    calculate(s);
    std::istringstream input(encode(s));
    State restored;
    restored.settings.distanceEffect = 7;
    EXPECT_FALSE(readState(input, restored, 43, quantities(s)));
    EXPECT_EQ(restored.settings.distanceEffect, 7);
}
TEST(CargoDistTests, malformedTruncatedAndAppendedPayloadsAreRejected)
{
    auto s = network();
    s.reconcile(source, 1, 12, 1, 0);
    const auto good = encode(s);
    for (auto bytes : { good.substr(0, good.size() - 1), good + "extra", std::string("OCARGODIST 1 42 1 9999999999\n") })
    {
        std::istringstream input(bytes);
        State restored;
        EXPECT_FALSE(readState(input, restored, 42, quantities(s)));
    }
}
TEST(CargoDistTests, modifiedPacketCannotPassChecksum)
{
    auto s = network();
    s.reconcile(source, 1, 12, 1, 0);
    auto bytes = encode(s);
    bytes.back() = '1';
    std::istringstream input(bytes);
    State restored;
    EXPECT_FALSE(readState(input, restored, 42, quantities(s)));
}
TEST(CargoDistTests, mismatchedNativeQuantitiesAndIdentitiesAreRejected)
{
    auto s = network();
    s.reconcile(source, 1, 12, 1, 0);
    auto native = quantities(s);
    native.at(source).second = 13;
    std::istringstream first(encode(s));
    State restored;
    EXPECT_FALSE(readState(first, restored, 42, native));
    native = quantities(s);
    native.at(source).first = 2;
    std::istringstream second(encode(s));
    EXPECT_FALSE(readState(second, restored, 42, native));
}
TEST(CargoDistTests, unusedNonzeroNativeCargoCannotBeOmittedFromCompanion)
{
    auto s = network();
    auto native = quantities(s);
    native[source] = { 1, 12 };
    std::istringstream input(encode(s));
    State restored;
    EXPECT_FALSE(readState(input, restored, 42, native));
}
TEST(CargoDistTests, congestionColourHasStableThresholds)
{
    EXPECT_EQ(congestionColour(0, 0), 0);
    EXPECT_EQ(congestionColour(49, 100), 1);
    EXPECT_EQ(congestionColour(50, 100), 2);
    EXPECT_EQ(congestionColour(80, 100), 3);
    EXPECT_EQ(congestionColour(100, 100), 4);
}
TEST(CargoDistTests, possibleElseBranchesNeverCreateAnIfToElseService)
{
    using namespace OpenLoco::Rules;
    auto p = makeProgram({ stop(1), stop(2), stop(3), stop(4) }, true);
    Edit add;
    add.block.kind = Kind::ifBlock;
    add.block.first = p.rows[0].key;
    add.block.last = p.rows[1].key;
    ASSERT_TRUE(edit(p, add));
    const auto condition = p.blocks.front().id;
    add.block = {};
    add.block.kind = Kind::otherwise;
    add.block.first = p.rows[2].key;
    add.block.last = p.rows[2].key;
    add.block.parent = condition;
    ASSERT_TRUE(edit(p, add));
    auto service = possibleService(p);
    EXPECT_FALSE(service.limited);
    EXPECT_EQ(service.stops.size(), 4);
    EXPECT_FALSE(service.links.contains({ p.rows[1].key, p.rows[2].key }));
    EXPECT_TRUE(service.links.contains({ p.rows[1].key, p.rows[3].key }));
    EXPECT_TRUE(service.links.contains({ p.rows[2].key, p.rows[3].key }));
}
TEST(CargoDistTests, possibleServiceUsesBreakAndFiniteRepeatWithoutChangingProgram)
{
    using namespace OpenLoco::Rules;
    auto p = makeProgram({ stop(1), stop(2), stop(3) }, true);
    Edit add;
    add.block.kind = Kind::repeat;
    add.block.first = p.rows[0].key;
    add.block.last = p.rows[1].key;
    add.block.number = 0;
    ASSERT_TRUE(edit(p, add));
    auto loop = p.blocks.front().id;
    auto before = p;
    auto service = possibleService(p);
    EXPECT_FALSE(service.stops.contains(p.rows[2].key));
    EXPECT_EQ(p, before);
    add.block = {};
    add.block.kind = Kind::breakLoop;
    add.block.first = p.rows[1].key;
    add.block.last = p.rows[1].key;
    add.block.target = loop;
    ASSERT_TRUE(edit(p, add));
    service = possibleService(p);
    EXPECT_TRUE(service.stops.contains(p.rows[2].key));
}
TEST(CargoDistTests, possibleServiceHasAnExplicitWorkBound)
{
    auto p = OpenLoco::Rules::makeProgram({ stop(1), stop(2), stop(3) }, true);
    EXPECT_TRUE(OpenLoco::Rules::possibleService(p, 1).limited);
}
TEST(CargoDistTests, compactAndExpandedGeometryHasNoOverlap)
{
    for (int16_t width : { 440, 560, 620, 1000 })
    {
        for (int16_t height : { 240, 320, 340, 700 })
        {
            const auto layout = viewLayout(width, height);
            EXPECT_GT(layout.list.height, 0);
            EXPECT_LE(layout.filters.y + layout.filters.height, layout.list.y);
            EXPECT_LT(layout.list.y + layout.list.height, layout.controls.y);
            EXPECT_LT(layout.controls.y + layout.controls.height, layout.footer.y);
            EXPECT_LE(layout.footer.y + layout.footer.height, height);
            EXPECT_LT(layout.list.x + layout.list.width, width);
        }
    }
}
TEST(CargoDistTests, expandedGroupsConserveTotalsInEveryGroupingOrder)
{
    const std::vector<ViewEntry> entries = { { 0, 1, 2, 3, 10, 30 }, { 0, 1, 2, 4, 20, 60 } };
    for (uint8_t order = 0; order < 6; ++order)
    {
        auto rows = groupedRows(entries, order, {}, true);
        uint32_t roots = 0;
        for (const auto& row : rows)
        {
            roots += row.quantity;
        }
        EXPECT_EQ(roots, 30);
        std::set<std::string> expanded;
        for (const auto& row : rows)
        {
            expanded.insert(row.key);
        }
        rows = groupedRows(entries, order, expanded, true);
        for (const auto& row : rows)
        {
            expanded.insert(row.key);
        }
        rows = groupedRows(entries, order, expanded, true);
        uint32_t leaves = 0;
        int64_t credit = 0;
        for (const auto& row : rows)
        {
            if (row.depth == 2)
            {
                leaves += row.quantity;
                credit += row.credit;
            }
        }
        EXPECT_EQ(leaves, 30);
        EXPECT_EQ(credit, 90);
    }
}
TEST(CargoDistTests, independentViewSelectionsDoNotChangeAnotherView)
{
    const std::vector<ViewEntry> entries = { { 0, 1, 2, 3, 10, 0 }, { 0, 4, 2, 3, 20, 0 } };
    const auto first = groupedRows(entries, 0, {}, true);
    std::set<std::string> expanded{ first.front().key };
    const auto second = groupedRows(entries, 0, expanded, true);
    EXPECT_GT(second.size(), first.size());
    EXPECT_EQ(groupedRows(entries, 0, {}, true).size(), first.size());
}
TEST(CargoDistTests, saveKeepsUnsettledVehicleProfitSeparateFromCargoCash)
{
    auto s = network();
    s.incomeAdjustments[10] = 75;
    s.incomeAdjustments[20] = -75;
    std::istringstream input(encode(s));
    State restored;
    ASSERT_TRUE(readState(input, restored, 42, quantities(s)));
    EXPECT_EQ(restored.incomeAdjustments, s.incomeAdjustments);
}
namespace
{
    struct CargoDistGameFixture : testing::Test
    {
        OpenLoco::Vehicles::VehicleHead* head = nullptr;
        OpenLoco::Vehicles::VehicleBody* body = nullptr;
        OpenLoco::Vehicles::VehicleBogie* bogie = nullptr;
        void SetUp() override
        {
            using namespace OpenLoco;
            using namespace OpenLoco::Vehicles;
            EntityManager::reset();
            OrderManager::reset();
            StationManager::reset();
            Lines::clearLines();
            CompanyManager::setControllingId(static_cast<CompanyId>(0));
            GameCommands::setUpdatingCompanyId(static_cast<CompanyId>(0));
            VehicleBase* pieces[7];
            const VehicleEntityType types[] = { VehicleEntityType::head, VehicleEntityType::vehicle_1, VehicleEntityType::vehicle_2, VehicleEntityType::bogie, VehicleEntityType::bogie, VehicleEntityType::body_start, VehicleEntityType::tail };
            for (size_t i = 0; i < 7; ++i)
            {
                pieces[i] = reinterpret_cast<VehicleBase*>(EntityManager::createEntityVehicle());
                pieces[i]->baseType = EntityBaseType::vehicle;
                pieces[i]->subType = types[i];
                pieces[i]->mode = TransportMode::rail;
                pieces[i]->owner = static_cast<CompanyId>(0);
                pieces[i]->head = pieces[0]->id;
                pieces[i]->tileX = 32;
                pieces[i]->nextCarId = EntityId::null;
                if (i)
                {
                    pieces[i - 1]->nextCarId = pieces[i]->id;
                }
            }
            head = reinterpret_cast<VehicleHead*>(pieces[0]);
            bogie = reinterpret_cast<VehicleBogie*>(pieces[3]);
            body = reinterpret_cast<VehicleBody*>(pieces[5]);
            head->vehicleType = VehicleType::train;
            head->stationId = static_cast<StationId>(1);
            head->status = Status::loading;
            EntityManager::moveEntityToList(head, EntityManager::EntityListType::vehicleHead);
            OrderManager::allocateOrders(*head);
            for (uint16_t id : { 1, 2 })
            {
                OrderStopAt order(static_cast<StationId>(id));
                OrderManager::insertOrder(head, head->sizeOfOrderTable - 1, &order);
            }
            body->primaryCargo.type = 0;
            body->primaryCargo.maxQty = 20;
            body->primaryCargo.acceptedTypes = 1;
            body->creationDay = 42;
            for (uint16_t id : { 1, 2 })
            {
                auto* station = StationManager::get(static_cast<StationId>(id));
                *station = OpenLoco::Station{};
                station->flags = StationFlags::allModes;
                station->name = StringIds::empty;
                station->owner = static_cast<CompanyId>(0);
                station->x = static_cast<int16_t>(id * 32);
                station->cargoStats[0].origin = static_cast<StationId>(id);
                station->cargoStats[0].isAccepted(id == 2);
            }
            StationManager::get(static_cast<StationId>(1))->cargoStats[0].quantity = 12;
            CargoDist::tick();
            calculate(gameState());
        }
        void TearDown() override
        {
            OpenLoco::EntityManager::reset();
            OpenLoco::StationManager::reset();
            OpenLoco::Lines::clearLines();
        }
    };
}
TEST_F(CargoDistGameFixture, nativePrimaryCompartmentLoadsWithMatchingStationTotals)
{
    const auto moved = CargoDist::load(*head, body->primaryCargo, *bogie);
    ASSERT_TRUE(moved);
    EXPECT_EQ(*moved, 12);
    EXPECT_EQ(body->primaryCargo.qty, 12);
    EXPECT_EQ(OpenLoco::StationManager::get(static_cast<OpenLoco::StationId>(1))->cargoStats[0].quantity, 0);
    EXPECT_EQ(vehicleDestined(OpenLoco::enumValue(head->id), 0, 2), 12);
}
TEST_F(CargoDistGameFixture, nativeManualModeDelegatesWithoutChangingCargo)
{
    auto settings = state().settings;
    settings.modes[0] = Mode::manual;
    ASSERT_TRUE(submitSettings(settings));
    EXPECT_FALSE(CargoDist::load(*head, body->primaryCargo, *bogie).has_value());
    EXPECT_EQ(body->primaryCargo.qty, 0);
    EXPECT_EQ(OpenLoco::StationManager::get(static_cast<OpenLoco::StationId>(1))->cargoStats[0].quantity, 12);
}
TEST_F(CargoDistGameFixture, nativeDeliveryIsCalledOnceAfterReachingDestination)
{
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    head->stationId = static_cast<OpenLoco::StationId>(2);
    int callbacks = 0;
    const auto result = CargoDist::unload(*head, body->primaryCargo, *bogie, [&](const Packet& packet) {
        ++callbacks;
        EXPECT_EQ(packet.quantity, 12);
        EXPECT_EQ(packet.origin, 1);
        EXPECT_EQ(reportedPayment(100), 100);
    });
    ASSERT_TRUE(result);
    EXPECT_TRUE(*result);
    EXPECT_EQ(callbacks, 1);
    EXPECT_EQ(body->primaryCargo.qty, 0);
    const auto repeat = CargoDist::unload(*head, body->primaryCargo, *bogie, [&](const Packet&) { ++callbacks; });
    ASSERT_TRUE(repeat);
    EXPECT_FALSE(*repeat);
    EXPECT_EQ(callbacks, 1);
}
TEST_F(CargoDistGameFixture, nativeResetClearsPacketsServicesAndViewGeneration)
{
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    const auto epoch = generation();
    OpenLoco::EntityManager::reset();
    EXPECT_TRUE(state().holdings.empty());
    EXPECT_TRUE(state().services.empty());
    EXPECT_NE(epoch, generation());
}
TEST_F(CargoDistGameFixture, foreignCompanyCannotSubmitDistributionSettings)
{
    const auto before = state().settings;
    OpenLoco::GameCommands::setUpdatingCompanyId(static_cast<OpenLoco::CompanyId>(1));
    auto settings = before;
    settings.modes[0] = Mode::manual;
    EXPECT_FALSE(submitSettings(settings));
    EXPECT_EQ(state().settings, before);
}
TEST_F(CargoDistGameFixture, missingCompanionMigratesNativeAmountsAutomatically)
{
    const auto path = std::filesystem::temp_directory_path() / "openloco_cargodist_missing_test.sv5";
    std::filesystem::remove(companionPath(path));
    ASSERT_TRUE(loadCompanion(path));
    EXPECT_NE(state().settings.modes[0], Mode::manual);
    EXPECT_EQ(state().quantity({ false, 1, 0, 0 }), 12);
}
TEST_F(CargoDistGameFixture, actualCompanionReplacementAndWrongSaveRecovery)
{
    const auto path = std::filesystem::temp_directory_path() / "openloco_cargodist_game_test.sv5";
    {
        std::ofstream file(path, std::ios::binary);
        file << "Disposable native save fingerprint";
    }
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    ASSERT_TRUE(saveCompanion(path));
    ASSERT_TRUE(saveCompanion(path));
    ASSERT_TRUE(loadCompanion(path));
    EXPECT_EQ(vehicleDestined(OpenLoco::enumValue(head->id), 0, 2), 12);
    {
        std::ofstream file(path, std::ios::binary);
        file << "Different save";
    }
    EXPECT_FALSE(loadCompanion(path));
    EXPECT_EQ(body->primaryCargo.qty, 12);
    EXPECT_EQ(state().quantity({ true, OpenLoco::enumValue(body->id), 0, 0 }), 12);
    std::filesystem::remove(companionPath(path));
    std::filesystem::remove(path);
}
TEST_F(CargoDistGameFixture, nativeSecondaryCompartmentUsesItsOwnPacketOwnership)
{
    bogie->secondaryCargo.type = 0;
    bogie->secondaryCargo.acceptedTypes = 1;
    bogie->secondaryCargo.maxQty = 5;
    const auto moved = CargoDist::load(*head, bogie->secondaryCargo, *bogie);
    ASSERT_TRUE(moved);
    EXPECT_EQ(*moved, 5);
    EXPECT_EQ(body->primaryCargo.qty, 0);
    EXPECT_EQ(bogie->secondaryCargo.qty, 5);
    EXPECT_EQ(state().quantity({ true, OpenLoco::enumValue(bogie->id), 0, 1 }), 5);
    EXPECT_EQ(OpenLoco::StationManager::get(static_cast<OpenLoco::StationId>(1))->cargoStats[0].quantity, 7);
}
TEST_F(CargoDistGameFixture, privateServicesSupportEveryNativeTransportMode)
{
    using namespace OpenLoco;
    using namespace OpenLoco::Vehicles;
    for (auto [mode, type] : { std::pair{ TransportMode::rail, VehicleType::train }, std::pair{ TransportMode::road, VehicleType::bus }, std::pair{ TransportMode::road, VehicleType::truck }, std::pair{ TransportMode::road, VehicleType::tram }, std::pair{ TransportMode::air, VehicleType::aircraft }, std::pair{ TransportMode::water, VehicleType::ship } })
    {
        for (auto* component = static_cast<VehicleBase*>(head); component; component = component->nextVehicleComponent())
        {
            component->mode = mode;
        }
        head->vehicleType = type;
        CargoDist::vehicleChanged(*head);
        ASSERT_TRUE(state().services.contains(enumValue(head->id)));
        EXPECT_FALSE(state().services.at(enumValue(head->id)).empty());
    }
}
TEST_F(CargoDistGameFixture, cargoMetadataMovesWhenNativeArticulatedCarrierChanges)
{
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    using namespace OpenLoco;
    auto* next = reinterpret_cast<Vehicles::VehicleBody*>(EntityManager::createEntityVehicle());
    next->baseType = EntityBaseType::vehicle;
    next->subType = Vehicles::VehicleEntityType::body_continued;
    next->creationDay = body->creationDay;
    const auto before = state().holdings.at({ true, enumValue(body->id), 0, 0 }).packets;
    CargoDist::movedCargo(enumValue(body->id), enumValue(next->id), 0);
    EXPECT_EQ(state().quantity({ true, enumValue(body->id), 0, 0 }), 0);
    EXPECT_EQ(state().holdings.at({ true, enumValue(next->id), 0, 0 }).packets, before);
}
TEST_F(CargoDistGameFixture, nativeVirtualCreditDoesNotChangeCashAccumulator)
{
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    gameState().holdings.at({ true, OpenLoco::enumValue(body->id), 0, 0 }).packets.front().credit = 75;
    head->stationId = static_cast<OpenLoco::StationId>(2);
    head->var_58 = 0;
    ASSERT_TRUE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [&](const Packet&) {
        head->var_58 += 100;
        EXPECT_EQ(reportedPayment(100), 25);
    }));
    EXPECT_EQ(head->var_58, 100);
    EXPECT_EQ(takeIncomeAdjustment(OpenLoco::enumValue(head->id)), -75);
    EXPECT_EQ(takeIncomeAdjustment(OpenLoco::enumValue(head->id)), 0);
}
TEST_F(CargoDistGameFixture, destinationCheckRunsThroughExistingRulesInterpreter)
{
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    using namespace OpenLoco::Rules;
    const Owner owner{ OwnerKind::vehicle, OpenLoco::enumValue(head->id) };
    const auto program = snapshot(owner);
    Edit command;
    command.block.kind = Kind::ifBlock;
    command.block.first = program.rows[0].key;
    command.block.last = program.rows[0].key;
    command.block.condition.check = Check::cargoOnboardFor;
    command.block.condition.stop = program.rows[1].key;
    command.block.condition.cargo = 0;
    command.block.condition.compare = Compare::greater;
    command.block.condition.value = 0;
    ASSERT_TRUE(submit(owner, command));
    ASSERT_TRUE(prepareVehicle(OpenLoco::enumValue(head->id)));
    EXPECT_EQ(head->currentOrder, 0);
}
TEST(CargoDistTests, repeatedSingleUnitProductionIsDistributedFairly)
{
    auto s = network();
    auto fourth = s.stations.at(3);
    fourth.id = 4;
    s.setStation(fourth);
    s.setService(30, { { 0, 0, 2, 4, 60, 5, 0 } });
    calculate(s);
    uint32_t third = 0, fourthCount = 0;
    for (size_t i = 0; i < 100; ++i)
    {
        s.reconcile(source, 1, 1, 1, 0);
        ASSERT_TRUE(s.assign(source));
        third += s.destined(source, 3);
        fourthCount += s.destined(source, 4);
        s.reconcile(source, 1, 0, 1, 0);
    }
    EXPECT_EQ(third, 50);
    EXPECT_EQ(fourthCount, 50);
}
TEST(CargoDistTests, incrementalAssignmentPreservesLargeLegacyPiles)
{
    auto s = network();
    auto fourth = s.stations.at(3);
    fourth.id = 4;
    s.setStation(fourth);
    s.setService(30, { { 0, 0, 2, 4, 60, 5, 0 } });
    calculate(s);
    s.reconcile(source, 1, 65535, 1, 0);
    ASSERT_TRUE(s.assign(source));
    EXPECT_EQ(s.quantity(source), 65535);
    EXPECT_LE(s.destined(source, 3) + s.destined(source, 4), s.settings.accuracy * 16U);
    for (size_t i = 0; i < 300; ++i)
    {
        s.assign(source);
    }
    EXPECT_EQ(s.destined(source, 3) + s.destined(source, 4), 65535);
    EXPECT_EQ(s.totalPackets, s.packetCount());
}
TEST_F(CargoDistGameFixture, sharedLineUsesTheSameDestinationRoutingAndCompartmentLedger)
{
    using namespace OpenLoco;
    const auto created = Lines::createLine("Cargo shared", TransportMode::rail, 2);
    ASSERT_TRUE(created.ok);
    for (uint16_t station : { 1, 2 })
    {
        Lines::LineOrder order;
        order.station = station;
        ASSERT_TRUE(Lines::addOrder(created.id, order).ok);
    }
    ASSERT_TRUE(Lines::assignVehicle(created.id, enumValue(head->id), TransportMode::rail).ok);
    CargoDist::vehicleChanged(*head);
    calculate(gameState());
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    EXPECT_EQ(vehicleDestined(enumValue(head->id), 0, 2), 12);
    EXPECT_FALSE(lineLinks(created.id).empty());
}
TEST_F(CargoDistGameFixture, sharedLineLoadingRestrictionIsRespected)
{
    using namespace OpenLoco;
    const auto created = Lines::createLine("Cargo restricted", TransportMode::rail, 2);
    ASSERT_TRUE(created.ok);
    for (uint16_t station : { 1, 2 })
    {
        Lines::LineOrder order;
        order.station = station;
        if (station == 1)
        {
            order.filters.push_back({ 0, 100, false, true });
        }
        ASSERT_TRUE(Lines::addOrder(created.id, order).ok);
    }
    ASSERT_TRUE(Lines::assignVehicle(created.id, enumValue(head->id), TransportMode::rail).ok);
    CargoDist::vehicleChanged(*head);
    calculate(gameState());
    EXPECT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 0);
    EXPECT_EQ(body->primaryCargo.qty, 0);
    EXPECT_EQ(StationManager::get(static_cast<StationId>(1))->cargoStats[0].quantity, 12);
}
TEST(CargoDistTests, closedConsumersCannotReceiveNewBoardingCargo)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 12, 1, 0);
    s.assign(source);
    auto closed = s.stations.at(3);
    closed.accepts = 0;
    s.setStation(closed);
    EXPECT_EQ(s.available(source, { 2 }), 0);
    EXPECT_EQ(s.load(source, compartment, 12, { 2 }), 0);
    EXPECT_EQ(s.quantity(source), 12);
}
TEST(CargoDistTests, metadataLimitRejectsFragmentationWithoutLosingNativeQuantity)
{
    auto s = network();
    auto fourth = s.stations.at(3);
    fourth.id = 4;
    s.setStation(fourth);
    s.setService(30, { { 0, 0, 2, 4, 60, 5, 0 } });
    calculate(s);
    s.reconcile(source, 1, 12, 1, 0);
    s.totalPackets = kMaxPackets - 64000;
    const auto before = s.holdings.at(source);
    EXPECT_FALSE(s.assign(source));
    EXPECT_EQ(s.holdings.at(source), before);
    EXPECT_EQ(s.quantity(source), 12);
}

TEST(CargoDistTests, anAvailableAlternativeServiceCanBoardEvenWhenAnotherIsFaster)
{
    auto s = network();
    s.setService(30, { { 0, 0, 1, 3, 30, 1, 0 } });
    calculate(s);
    ASSERT_EQ(s.nextHop(0, 0, 1, 3), 3);
    s.reconcile(source, 1, 20, 1, 0);
    s.assign(source);
    EXPECT_EQ(s.available(source, { 2 }), 20);
    EXPECT_EQ(s.load(source, compartment, 12, { 2 }), 12);
    EXPECT_EQ(s.quantity(source) + s.quantity(compartment), 20);
}
TEST(CargoDistTests, plannedFlowsIncludeUntaggedLegacyCargoWithoutEditingIt)
{
    auto s = network();
    auto fourth = s.stations.at(3);
    fourth.id = 4;
    s.setStation(fourth);
    s.setService(30, { { 0, 0, 2, 4, 60, 5, 0 } });
    calculate(s);
    s.reconcile(source, 1, 100, 1, 0);
    const auto before = encode(s);
    auto planned = s.pendingFlows();
    ASSERT_EQ(planned.size(), 2);
    EXPECT_EQ(planned[0].quantity + planned[1].quantity, 100);
    EXPECT_EQ(planned[0].quantity, 50);
    EXPECT_EQ(encode(s), before);
}
TEST(CargoDistTests, serviceObservationsResumeAcrossSaveReload)
{
    auto s = network();
    s.lastVisits[10] = { 2, 12345 };
    std::istringstream input(encode(s));
    State restored;
    ASSERT_TRUE(readState(input, restored, 42, quantities(s)));
    EXPECT_EQ(restored.lastVisits, s.lastVisits);
}

TEST(CargoDistTests, deletedOriginDoesNotAttachToAReusedStationName)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 12, 1, 0);
    s.load(source, compartment, 12, { 2 });
    s.lastVisits[10] = { 1, 123 };
    s.removeStation(1);
    Station replacement;
    replacement.id = 1;
    replacement.x = 320;
    s.setStation(replacement);
    const auto& packet = s.holdings.at(compartment).packets.front();
    EXPECT_TRUE(packet.originRemoved);
    EXPECT_EQ(packet.originX, 32);
    EXPECT_EQ(packet.quantity, 12);
    EXPECT_FALSE(s.lastVisits.contains(10));
    std::istringstream input(encode(s));
    State restored;
    ASSERT_TRUE(readState(input, restored, 42, quantities(s)));
    EXPECT_TRUE(restored.holdings.at(compartment).packets.front().originRemoved);
}

TEST(CargoDistTests, graphCollectionAndFlatteningRespectSingleStepBudgets)
{
    auto s = network();
    s.calculate(1);
    ASSERT_TRUE(s.job);
    EXPECT_EQ(s.job->phase, 0);
    EXPECT_EQ(s.job->aggregate.size(), 1);
    EXPECT_TRUE(s.job->links.empty());
    while (s.job && s.job->phase == 0)
    {
        s.calculate(1);
    }
    ASSERT_TRUE(s.job);
    EXPECT_EQ(s.job->phase, 1);
    EXPECT_EQ(s.job->links.size(), 1);
    const auto remaining = s.job->aggregate.size();
    s.calculate(1);
    ASSERT_TRUE(s.job);
    EXPECT_EQ(s.job->aggregate.size(), remaining - 1);
    EXPECT_EQ(s.job->links.size(), 2);
    std::istringstream input(encode(s));
    State restored;
    ASSERT_TRUE(readState(input, restored, 42, quantities(s)));
    EXPECT_EQ(restored.job, s.job);
    calculate(s, 1);
    calculate(restored, 1);
    EXPECT_EQ(restored.routes, s.routes);
}
TEST(CargoDistTests, routePublicationAlsoResumesWithinItsBudget)
{
    auto s = network();
    bool sawPublication = false;
    for (size_t i = 0; i < 1000 && (s.dirty || s.job); ++i)
    {
        s.calculate(1);
        if (s.job && s.job->publishAfter && s.job->initialised)
        {
            sawPublication = true;
            std::istringstream input(encode(s));
            State restored;
            ASSERT_TRUE(readState(input, restored, 42, quantities(s)));
            EXPECT_EQ(restored.job, s.job);
            calculate(restored, 1);
            calculate(s, 1);
            EXPECT_EQ(restored.routes, s.routes);
            break;
        }
    }
    EXPECT_TRUE(sawPublication);
}
TEST(CargoDistTests, deletedStationsClearUnsafeRoutesAndPendingReferences)
{
    auto s = network();
    calculate(s);
    s.setService(30, { { 0, 0, 1, 3, 1, 1, 0 } });
    s.calculate(1);
    ASSERT_TRUE(s.job);
    s.removeStation(2);
    EXPECT_FALSE(s.job);
    for (const auto& [key, candidates] : s.routes)
    {
        EXPECT_NE(key.from, 2);
        EXPECT_NE(key.to, 2);
        for (const auto& route : candidates)
        {
            EXPECT_NE(route.next, 2);
        }
    }
    std::istringstream input(encode(s));
    State restored;
    EXPECT_TRUE(readState(input, restored, 42, quantities(s)));
}
TEST_F(CargoDistGameFixture, waitingCargoGetsDestinationsBeforeAnyVehicleLoads)
{
    EXPECT_EQ(body->primaryCargo.qty, 0);
    CargoDist::stationChanged(1);
    EXPECT_EQ(state().destined({ false, 1, 0, 0 }, 2), 12);
    EXPECT_EQ(state().quantity({ false, 1, 0, 0 }), 12);
    EXPECT_EQ(body->primaryCargo.qty, 0);
}
TEST_F(CargoDistGameFixture, fixedSeedManualQueriesPreserveNativeQuantitiesAndRandomSequence)
{
    auto settings = state().settings;
    settings.modes.fill(Mode::manual);
    ASSERT_TRUE(submitSettings(settings));
    const auto original = OpenLoco::gPrng1();
    OpenLoco::gPrng1() = OpenLoco::Core::Prng(12345, 67890);
    auto expected = OpenLoco::gPrng1();
    for (size_t i = 0; i < 20; ++i)
    {
        EXPECT_EQ(availableFor(*head, 0), 12);
        EXPECT_FALSE(CargoDist::load(*head, body->primaryCargo, *bogie));
        EXPECT_FALSE(CargoDist::unload(*head, body->primaryCargo, *bogie, [](const Packet&) {}));
        EXPECT_EQ(OpenLoco::gPrng1().randNext(), expected.randNext());
        EXPECT_EQ(body->primaryCargo.qty, 0);
        EXPECT_EQ(OpenLoco::StationManager::get(static_cast<OpenLoco::StationId>(1))->cargoStats[0].quantity, 12);
    }
    OpenLoco::gPrng1() = original;
}
TEST_F(CargoDistGameFixture, articulatedReplacementMaintainsTheCachedPacketTotal)
{
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    using namespace OpenLoco;
    auto* next = reinterpret_cast<Vehicles::VehicleBody*>(EntityManager::createEntityVehicle());
    next->baseType = EntityBaseType::vehicle;
    next->subType = Vehicles::VehicleEntityType::body_continued;
    next->creationDay = body->creationDay;
    gameState().reconcile({ true, enumValue(next->id), 0, 0 }, 99, 7, 1, 0);
    CargoDist::movedCargo(enumValue(body->id), enumValue(next->id), 0);
    EXPECT_EQ(state().quantity({ true, enumValue(next->id), 0, 0 }), 12);
    EXPECT_EQ(state().totalPackets, state().packetCount());
}
TEST_F(CargoDistGameFixture, companionRejectsNonexistentServiceHeadsWithoutAttachingRecords)
{
    const auto path = std::filesystem::temp_directory_path() / "openloco_cargodist_owner_test.sv5";
    {
        std::ofstream file(path, std::ios::binary);
        file << "Disposable owner validation fixture";
    }
    gameState().setService(19799, { { 0, 0, 1, 2, 10, 1, 0 } });
    ASSERT_TRUE(saveCompanion(path));
    EXPECT_FALSE(loadCompanion(path));
    EXPECT_FALSE(state().services.contains(19799));
    EXPECT_EQ(state().quantity({ false, 1, 0, 0 }), 12);
    std::filesystem::remove(companionPath(path));
    std::filesystem::remove(path);
}

TEST_F(CargoDistGameFixture, serviceUsageIncludesEveryCompartmentAndSurvivesEmptyLoadAttempts)
{
    bogie->secondaryCargo.type = 0;
    bogie->secondaryCargo.acceptedTypes = 1;
    bogie->secondaryCargo.maxQty = 5;
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    ASSERT_EQ(*CargoDist::load(*head, bogie->secondaryCargo, *bogie), 0);
    for (const auto& link : state().services.at(OpenLoco::enumValue(head->id)))
    {
        if (link.from == 1 && link.cargo == 0)
        {
            EXPECT_EQ(link.capacity, 25);
            EXPECT_EQ(link.usage, 12);
        }
    }
}
