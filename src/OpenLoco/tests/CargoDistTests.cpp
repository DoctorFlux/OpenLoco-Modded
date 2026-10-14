#include <OpenLoco/CargoDist/CargoDist.h>
#include <OpenLoco/CargoDist/Game.h>
#include <OpenLoco/CargoDist/Save.h>
#include <OpenLoco/CargoDist/View.h>
#include <OpenLoco/Entities/EntityManager.h>
#include <OpenLoco/GameCommands/GameCommands.h>
#include <OpenLoco/Lines/Line.h>
#include <OpenLoco/Localisation/StringIds.h>
#include <OpenLoco/Localisation/StringManager.h>
#include <OpenLoco/Random.h>
#include <OpenLoco/Rules/Rules.h>
#include <OpenLoco/Ui/Window.h>
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
TEST(CargoDistTests, inlineGeometryUsesTheSameBoundariesForDrawingAndHitTesting)
{
    const auto rows = layoutCargoRows({ { 0, 0, 12, 0, 10, 1, { "summary" } }, { 1, 0, 22, 0, 30, 1, { "summary", "destination 1", "destination 2" } }, { 2, 0, 12, 0, 2, 1, {} } });
    ASSERT_EQ(rows.rows.size(), 3);
    EXPECT_EQ(rows.height, 86);
    EXPECT_EQ(rows.rows[1].top, 22);
    EXPECT_EQ(rows.rows[2].top, 74);
    EXPECT_EQ(rows.at(-1), nullptr);
    EXPECT_EQ(rows.at(21)->key, 0);
    EXPECT_EQ(rows.at(22)->key, 1);
    EXPECT_EQ(rows.at(73)->key, 1);
    EXPECT_EQ(rows.at(74)->key, 2);
    EXPECT_EQ(rows.at(86), nullptr);
}
TEST(CargoDistTests, inlineSummariesAreReadOnlyGroupedAndBounded)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 30, 1, 0);
    s.assign(source);
    auto& packets = s.holdings.at(source).packets;
    packets.push_back(packets.front());
    packets.front().quantity = 10;
    packets.back().quantity = 20;
    ++s.totalPackets;
    const auto before = encode(s);
    size_t budget = 10;
    const auto summary = summarize(s, { source }, 0, 1, budget);
    ASSERT_EQ(summary.destinations.size(), 1);
    EXPECT_EQ(summary.destinations.front().quantity, 30);
    EXPECT_EQ(summary.destinations.front().next, 2);
    EXPECT_EQ(budget, 8);
    EXPECT_EQ(encode(s), before);
    budget = 1;
    const auto limited = summarize(s, { source }, 0, 1, budget);
    EXPECT_TRUE(limited.limited);
    EXPECT_EQ(limited.destinations.front().quantity, 10);
}
TEST(CargoDistTests, classicEntriesHaveTheirOriginalGeometry)
{
    auto s = network();
    s.settings.modes.fill(Mode::manual);
    s.reconcile(source, 1, 12, 1, 0);
    size_t budget = 0;
    const auto summary = summarize(s, { source }, 0, 1, budget);
    EXPECT_FALSE(summary.automatic);
    EXPECT_FALSE(summary.limited);
    EXPECT_TRUE(summary.destinations.empty());
    EXPECT_EQ(layoutCargoRows({ { 0, 0, 22, 0, 12, 1, {} }, { 1, 0, 22, 0, 0, 1, {} } }).height, 44);
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
    s.setService(30, { { 0, 0, 1, 3, 30, 10, 0 } });
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

TEST(CargoDistTests, serviceObservationLimitRejectsAtomically)
{
    auto s = network();
    const auto before = s.services;
    const auto revision = s.revision;
    s.setService(10, std::vector<Link>(kMaxRoutes + 1, Link{ 0, 0, 1, 2, 10, 1, 0 }));
    EXPECT_EQ(s.services, before);
    EXPECT_EQ(s.revision, revision);
    EXPECT_TRUE(s.limited);
}

TEST(CargoDistTests, transferredCargoCannotBoardAFeederGoingBackwards)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 12, 1, 0);
    s.assign(source);
    ASSERT_EQ(s.load(source, compartment, 12, { 2 }), 12);
    ASSERT_EQ(s.unload(compartment, 2, {}, UINT16_MAX, [](const auto&) { return 100; }).transferred, 12);
    const Holder hub{ false, 2, 0, 0 };
    EXPECT_EQ(s.available(hub, { 1 }), 0);
    EXPECT_EQ(s.load(hub, compartment, 12, { 1 }), 0);
    EXPECT_EQ(s.available(hub, { 3 }), 12);
    EXPECT_EQ(s.quantity(hub), 12);
}
TEST(CargoDistTests, publishedRouteRemovalReassignsWaitingDestinationsAtCargoBoundary)
{
    auto s = network();
    calculate(s);
    s.reconcile(source, 1, 12, 1, 4);
    s.assign(source);
    s.removeService(20);
    auto replacement = s.stations.at(3);
    replacement.id = 4;
    s.setStation(replacement);
    s.setService(30, { { 0, 0, 2, 4, 12, 5, 0 } });
    s.assign(source); // The previous complete plan remains in use.
    EXPECT_EQ(s.destined(source, 3), 12);
    calculate(s, 1);
    ASSERT_TRUE(s.assign(source));
    EXPECT_EQ(s.destined(source, 4), 12);
    EXPECT_EQ(s.destined(source, 3), 0);
    EXPECT_EQ(s.holdings.at(source).packets.front().origin, 1);
    EXPECT_EQ(s.holdings.at(source).packets.front().age, 4);
}
namespace
{
    struct AutomaticTransferFixture : CargoDistGameFixture
    {
        OpenLoco::Vehicles::VehicleHead* carrier = nullptr;
        OpenLoco::Vehicles::VehicleBody* carrierBody = nullptr;
        OpenLoco::Vehicles::VehicleBogie* carrierBogie = nullptr;
        void SetUp() override
        {
            CargoDistGameFixture::SetUp();
            using namespace OpenLoco;
            using namespace OpenLoco::Vehicles;
            head->mode = bogie->mode = TransportMode::road;
            head->vehicleType = VehicleType::truck;
            auto* hub = StationManager::get(StationId(2));
            hub->cargoStats[0].isAccepted(false);
            auto* end = StationManager::get(StationId(3));
            *end = OpenLoco::Station{};
            end->flags = StationFlags::allModes;
            end->name = StringIds::empty;
            end->owner = CompanyId(0);
            end->x = 96;
            end->cargoStats[0].origin = StationId(3);
            end->cargoStats[0].isAccepted(true);
            VehicleBase* pieces[7];
            const VehicleEntityType types[] = { VehicleEntityType::head, VehicleEntityType::vehicle_1, VehicleEntityType::vehicle_2, VehicleEntityType::bogie, VehicleEntityType::bogie, VehicleEntityType::body_start, VehicleEntityType::tail };
            for (size_t i = 0; i < 7; ++i)
            {
                pieces[i] = reinterpret_cast<VehicleBase*>(EntityManager::createEntityVehicle());
                pieces[i]->baseType = EntityBaseType::vehicle;
                pieces[i]->subType = types[i];
                pieces[i]->mode = TransportMode::rail;
                pieces[i]->owner = CompanyId(0);
                pieces[i]->head = pieces[0]->id;
                pieces[i]->tileX = 64;
                pieces[i]->nextCarId = EntityId::null;
                if (i)
                {
                    pieces[i - 1]->nextCarId = pieces[i]->id;
                }
            }
            carrier = reinterpret_cast<VehicleHead*>(pieces[0]);
            carrierBogie = reinterpret_cast<VehicleBogie*>(pieces[3]);
            carrierBody = reinterpret_cast<VehicleBody*>(pieces[5]);
            carrier->vehicleType = VehicleType::train;
            carrier->stationId = StationId(2);
            carrier->status = Status::loading;
            EntityManager::moveEntityToList(carrier, EntityManager::EntityListType::vehicleHead);
            OrderManager::allocateOrders(*carrier);
            for (uint16_t id : { 2, 3 })
            {
                OrderStopAt order{ StationId(id) };
                OrderManager::insertOrder(carrier, carrier->sizeOfOrderTable - 1, &order);
            }
            carrierBody->primaryCargo.type = 0;
            carrierBody->primaryCargo.acceptedTypes = 1;
            carrierBody->primaryCargo.maxQty = 20;
            carrierBody->creationDay = 42;
            CargoDist::stationChanged(2);
            CargoDist::stationChanged(3);
            CargoDist::vehicleChanged(*head);
            CargoDist::vehicleChanged(*carrier);
            calculate(gameState());
        }
        void reach(OpenLoco::Vehicles::VehicleHead& vehicle, uint16_t station, uint16_t offset)
        {
            vehicle.stationId = OpenLoco::StationId(station);
            vehicle.currentOrder = offset;
            vehicle.checkIfAtOrderStation();
            CargoDist::arrived(vehicle);
        }
        uint16_t waiting(uint16_t station) { return OpenLoco::StationManager::get(OpenLoco::StationId(station))->cargoStats[0].quantity; }
    };
}
TEST_F(AutomaticTransferFixture, normalTruckAndTrainOrdersTransferPartialLoadsWithoutFeederReloading)
{
    reach(*head, 1, 0);
    body->primaryCargo.maxQty = 5;
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 5);
    EXPECT_EQ(waiting(1), 7);
    reach(*head, 2, 2);
    unsigned deliveries = 0;
    ASSERT_TRUE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [&](const auto&) { ++deliveries; }));
    EXPECT_EQ(deliveries, 0);
    EXPECT_EQ(waiting(2), 5);
    EXPECT_EQ(body->primaryCargo.qty, 0);
    EXPECT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 0);
    reach(*carrier, 2, 0);
    carrierBody->primaryCargo.maxQty = 3;
    ASSERT_EQ(*CargoDist::load(*carrier, carrierBody->primaryCargo, *carrierBogie), 3);
    EXPECT_EQ(waiting(2), 2);
    reach(*carrier, 3, 2);
    ASSERT_TRUE(*CargoDist::unload(*carrier, carrierBody->primaryCargo, *carrierBogie, [&](const auto& p) { deliveries += p.quantity; }));
    EXPECT_EQ(deliveries, 3);
    EXPECT_FALSE(*CargoDist::unload(*carrier, carrierBody->primaryCargo, *carrierBogie, [&](const auto& p) { deliveries += p.quantity; }));
    EXPECT_EQ(deliveries, 3);
    EXPECT_EQ(waiting(1) + waiting(2) + carrierBody->primaryCargo.qty + body->primaryCargo.qty + deliveries, 12);
}
TEST_F(AutomaticTransferFixture, acceptingInterchangeDoesNotDeliverCargoForAnotherDestination)
{
    reach(*head, 1, 0);
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    OpenLoco::StationManager::get(OpenLoco::StationId(2))->cargoStats[0].isAccepted(true);
    CargoDist::stationChanged(2);
    calculate(gameState());
    reach(*head, 2, 2);
    unsigned deliveries = 0;
    ASSERT_TRUE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [&](const auto&) { ++deliveries; }));
    EXPECT_EQ(deliveries, 0);
    EXPECT_EQ(waiting(2), 12);
}
TEST_F(AutomaticTransferFixture, sameCarrierKeepsThroughCargoAtIntermediateStop)
{
    using namespace OpenLoco;
    using namespace OpenLoco::Vehicles;
    OrderStopAt final{ StationId(3) };
    OrderManager::insertOrder(head, head->sizeOfOrderTable - 1, &final);
    CargoDist::vehicleChanged(*head);
    calculate(gameState());
    reach(*head, 1, 0);
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    reach(*head, 2, 2);
    EXPECT_FALSE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [](const auto&) { ADD_FAILURE(); }));
    EXPECT_EQ(body->primaryCargo.qty, 12);
    EXPECT_EQ(waiting(2), 0);
}
TEST_F(AutomaticTransferFixture, saveReloadHandoffRebuildsReachedStopWithoutReloadingBackwards)
{
    reach(*head, 1, 0);
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    reach(*head, 2, 2);
    ASSERT_TRUE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [](const auto&) { ADD_FAILURE(); }));
    const auto path = std::filesystem::temp_directory_path() / "openloco_automatic_transfer.sv5";
    {
        std::ofstream file(path);
        file << "Disposable transfer save";
    }
    ASSERT_TRUE(saveCompanion(path));
    ASSERT_TRUE(loadCompanion(path));
    EXPECT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 0);
    reach(*carrier, 2, 0);
    ASSERT_EQ(*CargoDist::load(*carrier, carrierBody->primaryCargo, *carrierBogie), 12);
    EXPECT_EQ(vehicleDestined(OpenLoco::enumValue(carrier->id), 0, 3), 12);
    EXPECT_EQ(waiting(2), 0);
    std::filesystem::remove(companionPath(path));
    std::filesystem::remove(path);
}
TEST_F(CargoDistGameFixture, inlineExpansionUsesSeparateWindowSessionsAndResetEpochs)
{
    using namespace OpenLoco;
    // Plain strings make this geometry test independent of installed game assets/languages.
    std::vector<std::pair<StringId, const char*>> old;
    for (const auto id : { StringIds::cargo_transfer_to, StringIds::cargo_transfer_many, StringIds::cargo_transfer_connection, StringIds::cargo_transfer_finding })
    {
        old.emplace_back(id, StringManager::swapString(id, "Destination"));
    }
    Ui::Window first({ 0, 0 }, { 223, 136 }), second({ 250, 0 }, { 223, 136 });
    first.type = second.type = Ui::WindowType::station;
    first.number = 1;
    second.number = 2;
    first.rowHeight = second.rowHeight = 12;
    const auto height = cargoRows(first).height;
    ASSERT_GT(height, 12);
    toggleCargoRow(first, 0);
    EXPECT_GT(cargoRows(first).height, height);
    EXPECT_EQ(cargoRows(second).height, 0);
    toggleCargoRow(first, 0);
    EXPECT_EQ(cargoRows(first).height, height);
    toggleCargoRow(first, 0);
    closeCargoRows(first);
    EXPECT_EQ(cargoRows(first).height, height);
    auto settings = state().settings;
    settings.modes.fill(Mode::manual);
    ASSERT_TRUE(submitSettings(settings));
    EXPECT_EQ(cargoRows(first).height, 12);
    closeCargoRows(first);
    closeCargoRows(second);
    for (auto [id, value] : old)
    {
        StringManager::swapString(id, value);
    }
}
TEST_F(CargoDistGameFixture, compactModePresetsPreserveSavedAdvancedSettingsAndMixedModes)
{
    auto settings = state().settings;
    settings.distanceEffect = 77;
    settings.returning = 50;
    settings.accuracy = 9;
    settings.modes[0] = Mode::manual;
    ASSERT_TRUE(submitSettings(settings));
    EXPECT_EQ(transferMode(), TransferMode::custom);
    const auto automatic = automaticSettings();
    EXPECT_EQ(automatic.distanceEffect, 77);
    EXPECT_EQ(automatic.returning, 50);
    EXPECT_EQ(automatic.accuracy, 9);
    ASSERT_TRUE(submitSettings(automatic));
    EXPECT_EQ(transferMode(), TransferMode::automatic);
    settings = automatic;
    settings.modes.fill(Mode::manual);
    ASSERT_TRUE(submitSettings(settings));
    EXPECT_EQ(transferMode(), TransferMode::classic);
    std::istringstream input(encode(state()));
    State restored;
    ASSERT_TRUE(readState(input, restored, 42, quantities(state())));
    EXPECT_EQ(restored.settings, settings);
}

TEST_F(AutomaticTransferFixture, passengerAndMailModesTransferAcrossTheOtherNativeModes)
{
    using namespace OpenLoco;
    using namespace OpenLoco::Vehicles;
    auto settings = state().settings;
    settings.modes[0] = Mode::symmetric;
    ASSERT_TRUE(submitSettings(settings));
    // Both passenger and mail use the symmetric packet/transfer path. No modal special cases.
    for (auto [mode, type] : { std::pair{ TransportMode::road, VehicleType::bus }, std::pair{ TransportMode::road, VehicleType::tram }, std::pair{ TransportMode::water, VehicleType::ship }, std::pair{ TransportMode::air, VehicleType::aircraft } })
    {
        for (auto* part = static_cast<VehicleBase*>(carrier); part; part = part->nextVehicleComponent())
        {
            part->mode = mode;
        }
        carrier->vehicleType = type;
        CargoDist::vehicleChanged(*carrier);
        calculate(gameState());
        reach(*head, 1, 0);
        ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
        reach(*head, 2, 2);
        ASSERT_TRUE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [](const auto&) { ADD_FAILURE(); }));
        reach(*carrier, 2, 0);
        ASSERT_EQ(*CargoDist::load(*carrier, carrierBody->primaryCargo, *carrierBogie), 12);
        reach(*carrier, 3, 2);
        uint32_t delivered = 0;
        ASSERT_TRUE(*CargoDist::unload(*carrier, carrierBody->primaryCargo, *carrierBogie, [&](const auto& p) { delivered += p.quantity; }));
        EXPECT_EQ(delivered, 12);
        StationManager::get(StationId(1))->cargoStats[0].quantity = 12;
        CargoDist::stationChanged(1);
        calculate(gameState());
    }
}
TEST_F(AutomaticTransferFixture, sharedRepeatedStopsUseReachedOccurrenceFiltersAndSingleCapacity)
{
    using namespace OpenLoco;
    const auto created = Lines::createLine("Repeated transfer", TransportMode::rail, 2);
    ASSERT_TRUE(created.ok);
    for (uint16_t station : { 2, 3, 2, 3 })
    {
        Lines::LineOrder order;
        order.station = station;
        if (station == 2 && Lines::findLine(created.id)->orders.empty())
        {
            order.filters.push_back({ 0, 0, false, true });
        }
        ASSERT_TRUE(Lines::addOrder(created.id, order).ok);
    }
    ASSERT_TRUE(Lines::assignVehicle(created.id, enumValue(carrier->id), TransportMode::rail).ok);
    CargoDist::vehicleChanged(*carrier);
    calculate(gameState());
    reach(*head, 1, 0);
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    reach(*head, 2, 2);
    ASSERT_TRUE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [](const auto&) { ADD_FAILURE(); }));
    reach(*carrier, 2, 0);
    EXPECT_EQ(*CargoDist::load(*carrier, carrierBody->primaryCargo, *carrierBogie), 0);
    reach(*carrier, 2, 4);
    ASSERT_EQ(*CargoDist::load(*carrier, carrierBody->primaryCargo, *carrierBogie), 12);
    const auto& links = state().services.at(enumValue(carrier->id));
    unsigned matching = 0;
    for (const auto& link : links)
    {
        if (link.from == 2 && link.to == 3 && link.cargo == 0)
        {
            ++matching;
            EXPECT_EQ(link.capacity, 20);
        }
    }
    EXPECT_EQ(matching, 1);
}
TEST_F(AutomaticTransferFixture, noUnloadIntermediateRestrictionKeepsCargoForALaterStop)
{
    using namespace OpenLoco;
    const auto created = Lines::createLine("Through transfer", TransportMode::road, 2);
    ASSERT_TRUE(created.ok);
    for (uint16_t station : { 1, 2, 3 })
    {
        Lines::LineOrder order;
        order.station = station;
        if (station == 2)
        {
            order.filters.push_back({ 0, 0, false, false });
        }
        ASSERT_TRUE(Lines::addOrder(created.id, order).ok);
    }
    ASSERT_TRUE(Lines::assignVehicle(created.id, enumValue(head->id), TransportMode::road).ok);
    CargoDist::vehicleChanged(*head);
    calculate(gameState());
    reach(*head, 1, 0);
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    reach(*head, 2, 2);
    EXPECT_FALSE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [](const auto&) { ADD_FAILURE(); }));
    EXPECT_EQ(body->primaryCargo.qty, 12);
    reach(*head, 3, 4);
    uint32_t delivered = 0;
    ASSERT_TRUE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [&](const auto& p) { delivered += p.quantity; }));
    EXPECT_EQ(delivered, 12);
}
TEST_F(AutomaticTransferFixture, explicitFullLoadDoesNotPreventInitialServiceDiscovery)
{
    using namespace OpenLoco;
    using namespace OpenLoco::Vehicles;
    OrderWaitFor wait(0);
    OrderManager::insertOrder(head, 2, &wait);
    CargoDist::vehicleChanged(*head);
    calculate(gameState());
    reach(*head, 1, 0);
    EXPECT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    EXPECT_EQ(vehicleDestined(enumValue(head->id), 0, 3), 12);
}
TEST_F(AutomaticTransferFixture, explicitUnloadForcesAHandoffDespiteAThroughService)
{
    using namespace OpenLoco;
    using namespace OpenLoco::Vehicles;
    OrderUnloadAll unload(0);
    OrderManager::insertOrder(head, 4, &unload);
    OrderStopAt final{ StationId(3) };
    OrderManager::insertOrder(head, 5, &final);
    CargoDist::vehicleChanged(*head);
    calculate(gameState());
    reach(*head, 1, 0);
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    reach(*head, 2, 2);
    ASSERT_TRUE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [](const auto&) { ADD_FAILURE(); }));
    EXPECT_EQ(waiting(2), 12);
    EXPECT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 0);
}
TEST_F(CargoDistGameFixture, unresolvedRepeatedStopContextPreservesOnboardCargo)
{
    using namespace OpenLoco;
    using namespace OpenLoco::Vehicles;
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    OrderStopAt repeat{ StationId(1) };
    OrderManager::insertOrder(head, head->sizeOfOrderTable - 1, &repeat);
    head->currentOrder = 1; // Not an order boundary; cannot identify a repeated occurrence safely.
    EXPECT_FALSE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [](const auto&) { ADD_FAILURE(); }));
    EXPECT_EQ(body->primaryCargo.qty, 12);
}

TEST_F(AutomaticTransferFixture, foreverLoopRemovesMentionedButUnservedDestination)
{
    using namespace OpenLoco;
    using namespace OpenLoco::Vehicles;
    auto* alternate = StationManager::get(StationId(4));
    *alternate = OpenLoco::Station{};
    alternate->name = StringIds::empty;
    alternate->flags = StationFlags::allModes;
    alternate->owner = CompanyId(0);
    alternate->x = 128;
    CargoDist::stationChanged(4);
    OrderStopAt extra{ StationId(4) };
    OrderManager::insertOrder(carrier, carrier->sizeOfOrderTable - 1, &extra);
    const auto owner = Rules::ownerForVehicle(enumValue(carrier->id));
    const auto p = Rules::snapshot(owner);
    Rules::Edit edit;
    edit.block.kind = Rules::Kind::repeat;
    edit.block.first = edit.block.last = p.rows[0].key;
    edit.block.number = 0;
    ASSERT_TRUE(Rules::submit(owner, edit));
    CargoDist::vehicleChanged(*carrier);
    calculate(gameState());
    reach(*head, 1, 0);
    EXPECT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 0);
    EXPECT_EQ(waiting(1), 12);
    for (const auto& link : state().services.at(enumValue(carrier->id)))
    {
        EXPECT_NE(link.to, 3);
    }
}
TEST_F(AutomaticTransferFixture, repeatedAcceptingStopHonoursItsOwnUnloadRestriction)
{
    using namespace OpenLoco;
    const auto created = Lines::createLine("Repeated delivery", TransportMode::rail, 2);
    ASSERT_TRUE(created.ok);
    for (uint16_t station : { 2, 3, 2, 3 })
    {
        Lines::LineOrder order;
        order.station = station;
        if (station == 3 && Lines::findLine(created.id)->orders.size() == 1)
        {
            order.filters.push_back({ 0, 0, true, false });
        }
        ASSERT_TRUE(Lines::addOrder(created.id, order).ok);
    }
    ASSERT_TRUE(Lines::assignVehicle(created.id, enumValue(carrier->id), TransportMode::rail).ok);
    CargoDist::vehicleChanged(*carrier);
    calculate(gameState());
    reach(*head, 1, 0);
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    reach(*head, 2, 2);
    ASSERT_TRUE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [](const auto&) { ADD_FAILURE(); }));
    reach(*carrier, 2, 0);
    ASSERT_EQ(*CargoDist::load(*carrier, carrierBody->primaryCargo, *carrierBogie), 12);
    unsigned deliveries = 0;
    reach(*carrier, 3, 2);
    EXPECT_FALSE(*CargoDist::unload(*carrier, carrierBody->primaryCargo, *carrierBogie, [&](const auto&) { ++deliveries; }));
    EXPECT_EQ(carrierBody->primaryCargo.qty, 12);
    reach(*carrier, 3, 6);
    ASSERT_TRUE(*CargoDist::unload(*carrier, carrierBody->primaryCargo, *carrierBogie, [&](const auto&) { ++deliveries; }));
    EXPECT_EQ(deliveries, 1);
}
TEST_F(AutomaticTransferFixture, inlineVehicleRowsKeepNativeModeHeightsAndOwnerChangesCollapseDetails)
{
    using namespace OpenLoco;
    Ui::Window window({ 0, 0 }, { 265, 177 });
    window.type = Ui::WindowType::vehicle;
    window.number = enumValue(head->id);
    reach(*head, 1, 0);
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    for (uint16_t height : { 22, 45, 82 })
    {
        window.rowHeight = height;
        closeCargoRows(window);
        ASSERT_EQ(cargoRows(window).rows.size(), 1);
        EXPECT_EQ(cargoRows(window).rows.front().baseHeight, height);
        EXPECT_EQ(cargoRows(window).height, height + 10);
        toggleCargoRow(window, height + 1);
        EXPECT_GT(cargoRows(window).height, height + 10);
        EXPECT_EQ(cargoRows(window).at(cargoRows(window).height - 1)->key, enumValue(bogie->id));
    }
    head->owner = CompanyId(1);
    EXPECT_EQ(cargoRows(window).height, 92);
    closeCargoRows(window);
}

TEST_F(AutomaticTransferFixture, possibleBranchesDoNotForceConditionExecutionDuringBoarding)
{
    using namespace OpenLoco;
    const auto owner = Rules::ownerForVehicle(enumValue(carrier->id));
    const auto p = Rules::snapshot(owner);
    Rules::Edit edit;
    edit.block.kind = Rules::Kind::ifBlock;
    edit.block.first = edit.block.last = p.rows[0].key;
    edit.block.condition.check = Rules::Check::cargoWaiting;
    edit.block.condition.stop = p.rows[0].key;
    edit.block.condition.cargo = 0;
    edit.block.condition.compare = Rules::Compare::greater;
    edit.block.condition.value = 0;
    ASSERT_TRUE(Rules::submit(owner, edit));
    CargoDist::vehicleChanged(*carrier);
    calculate(gameState());
    reach(*head, 1, 0);
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    reach(*head, 2, 2);
    ASSERT_TRUE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [](const auto&) { ADD_FAILURE(); }));
    carrier->stationId = StationId(2);
    carrier->currentOrder = 0;
    CargoDist::stopReached(*carrier);
    const auto before = Rules::snapshot(owner);
    const auto* runtime = Rules::runtimeForVehicle(enumValue(carrier->id));
    ASSERT_EQ(*CargoDist::load(*carrier, carrierBody->primaryCargo, *carrierBogie), 12);
    EXPECT_EQ(carrier->currentOrder, 0);
    EXPECT_EQ(Rules::snapshot(owner), before);
    EXPECT_EQ(Rules::runtimeForVehicle(enumValue(carrier->id)), runtime);
}

TEST_F(AutomaticTransferFixture, adjacentPrivateStopsWaitForSafeContextAfterReload)
{
    using namespace OpenLoco;
    Vehicles::OrderStopAt repeat{ StationId(2) };
    Vehicles::OrderManager::insertOrder(carrier, 2, &repeat);
    CargoDist::vehicleChanged(*carrier);
    calculate(gameState());
    reach(*head, 1, 0);
    ASSERT_EQ(*CargoDist::load(*head, body->primaryCargo, *bogie), 12);
    reach(*head, 2, 2);
    ASSERT_TRUE(*CargoDist::unload(*head, body->primaryCargo, *bogie, [](const auto&) { ADD_FAILURE(); }));
    reach(*carrier, 2, 0);
    EXPECT_EQ(carrier->currentOrder, 2);
    EXPECT_EQ(availableFor(*carrier, 0), 12);
    replaceState(gameState()); // As on load, only transient context is discarded.
    EXPECT_EQ(*CargoDist::load(*carrier, carrierBody->primaryCargo, *carrierBogie), 0);
    reach(*carrier, 2, 2);
    EXPECT_EQ(*CargoDist::load(*carrier, carrierBody->primaryCargo, *carrierBogie), 12);
}
