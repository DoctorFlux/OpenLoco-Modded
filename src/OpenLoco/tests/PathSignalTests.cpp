#include <OpenLoco/Entities/EntityManager.h>
#include <OpenLoco/Lines/Line.h>
#include <OpenLoco/Map/SignalElement.h>
#include <OpenLoco/Map/TileManager.h>
#include <OpenLoco/Map/Track/Track.h>
#include <OpenLoco/Map/Track/TrackData.h>
#include <OpenLoco/Map/TrackElement.h>
#include <OpenLoco/Math/Vector.hpp>
#include <OpenLoco/Rules/Rules.h>
#include <OpenLoco/Scenario/ScenarioManager.h>
#include <OpenLoco/Signals/PathSearch.h>
#include <OpenLoco/Signals/PathSignals.h>
#include <OpenLoco/Vehicles/OrderManager.h>
#include <OpenLoco/Vehicles/RoutingManager.h>
#include <OpenLoco/Vehicles/Vehicle1.h>
#include <OpenLoco/Vehicles/Vehicle2.h>
#include <OpenLoco/Vehicles/VehicleBogie.h>
#include <OpenLoco/Vehicles/VehicleHead.h>
#include <OpenLoco/Vehicles/VehicleTail.h>
#include <OpenLoco/World/CompanyManager.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <string>

using namespace OpenLoco;
using namespace OpenLoco::Signals;

namespace
{
    class TestGraph final : public TrackGraph
    {
    public:
        StepEdge& add(int16_t from, int16_t to)
        {
            StepEdge edge;
            edge.step = TrackStep{ World::Pos3{ to, 0, 0 }, 0 };
            edge.connection = static_cast<uint16_t>(to);
            edge.cost = 32;
            auto& list = _edges[from];
            list.push_back(edge);
            return list.back();
        }

        StepEdge start(int16_t at) const
        {
            StepEdge edge;
            edge.step = TrackStep{ World::Pos3{ at, 0, 0 }, 0 };
            edge.connection = static_cast<uint16_t>(at);
            edge.cost = 32;
            return edge;
        }

        void successors(const TrackStep& from, std::vector<StepEdge>& out) override
        {
            const auto found = _edges.find(from.pos.x);
            if (found == _edges.end())
            {
                return;
            }
            out.insert(out.end(), found->second.begin(), found->second.end());
        }

    private:
        std::map<int16_t, std::vector<StepEdge>> _edges;
    };

    std::vector<int16_t> ids(const SearchResult& result)
    {
        std::vector<int16_t> out;
        for (const auto& edge : result.reservation)
        {
            out.push_back(edge.step.pos.x);
        }
        return out;
    }

    ReservedStep straight(int16_t x, int16_t y, uint8_t rotation)
    {
        return ReservedStep{ World::Pos3{ x, y, 0 }, static_cast<uint16_t>(rotation) };
    }

    struct WaypointSignalFixture : testing::Test
    {
        const World::Pos3 start{ 640, 640, 16 };
        World::Pos3 junction{}, blocked{}, target{};

        void SetUp() override
        {
            reset();
            EntityManager::reset();
            Vehicles::OrderManager::reset();
            Vehicles::RoutingManager::resetRoutingTable();
            Rules::clear();
            Lines::clearLines();
            CompanyManager::setControllingId(static_cast<CompanyId>(0));
            ScenarioManager::setScenarioTicks(0);
            World::TileManager::allocateMapElements();
            World::TileManager::initialise();
            junction = World::Track::getTrackConnectionEnd(start, 0).nextPos;
            blocked = World::Track::getTrackConnectionEnd(junction, 0).nextPos;
            target = World::Track::getTrackConnectionEnd(blocked, 0).nextPos;
            for (auto pos : { start, junction, blocked, target })
            {
                addPiece(pos, 0);
            }
            addPiece(junction, 2 << 3); // Free small curve leads to an unrelated dead end.
        }

        void TearDown() override
        {
            reset();
            setHoldValidator(nullptr);
            setReservationViewDirty(nullptr);
            Rules::clear();
            Lines::clearLines();
        }

        void addPiece(World::Pos3 entrance, uint16_t tad)
        {
            const auto piece = canonicalPiece(entrance, tad);
            const auto parts = World::TrackData::getTrackPiece(piece.trackId);
            for (const auto& part : parts)
            {
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, piece.rotation);
                auto* track = World::TileManager::insertElement(World::ElementType::track, { static_cast<coord_t>(piece.x + offset.x), static_cast<coord_t>(piece.y + offset.y) }, static_cast<uint8_t>((piece.z + part.z) / 4), 0)->as<World::TrackElement>();
                track->setTrackId(piece.trackId);
                track->setRotation(piece.rotation);
                track->setSequenceIndex(part.index);
                track->setFlag6(part.index == parts.back().index);
                track->setTrackObjectId(0);
                track->setOwner(static_cast<CompanyId>(0));
            }
        }

        Vehicles::VehicleHead* vehicle()
        {
            using namespace Vehicles;
            VehicleBase* parts[4]{};
            const VehicleEntityType types[]{ VehicleEntityType::head, VehicleEntityType::vehicle_1, VehicleEntityType::vehicle_2, VehicleEntityType::tail };
            const auto routing = RoutingManager::getAndAllocateFreeRoutingHandle().value();
            for (size_t i = 0; i < 4; ++i)
            {
                parts[i] = reinterpret_cast<VehicleBase*>(EntityManager::createEntityVehicle());
                parts[i]->baseType = EntityBaseType::vehicle;
                parts[i]->subType = types[i];
                parts[i]->mode = TransportMode::rail;
                parts[i]->owner = static_cast<CompanyId>(0);
                parts[i]->tileX = start.x;
                parts[i]->tileY = start.y;
                parts[i]->tileBaseZ = 4;
                parts[i]->routingHandle = routing;
                parts[i]->nextCarId = EntityId::null;
                parts[i]->head = parts[0]->id;
                if (i)
                {
                    parts[i - 1]->nextCarId = parts[i]->id;
                }
            }
            auto* head = reinterpret_cast<VehicleHead*>(parts[0]);
            EntityManager::moveEntityToList(head, EntityManager::EntityListType::vehicleHead);
            head->status = Status::travelling;
            head->vehicleType = VehicleType::train;
            head->trackType = 0;
            OrderManager::allocateOrders(*head);
            OrderRouteWaypoint waypoint({ static_cast<coord_t>(target.x / 32), static_cast<coord_t>(target.y / 32) }, 2, 0, 0);
            OrderManager::insertOrder(head, 0, &waypoint);
            head->currentOrder = 0;
            return head;
        }

        Vehicles::VehicleBogie* occupiedWagon(EntityId owner)
        {
            auto* wagon = reinterpret_cast<Vehicles::VehicleBogie*>(EntityManager::createEntityVehicle());
            wagon->baseType = EntityBaseType::vehicle;
            wagon->subType = Vehicles::VehicleEntityType::bogie;
            wagon->head = owner;
            wagon->mode = TransportMode::rail;
            wagon->tileX = blocked.x;
            wagon->tileY = blocked.y;
            wagon->tileBaseZ = 4;
            wagon->trackAndDirection.track._data = 0;
            wagon->moveTo(blocked);
            return wagon;
        }

        void signalAt(World::Pos3 pos, uint16_t tad, SignalKind kind)
        {
            auto tile = World::TileManager::get(pos);
            World::TileElementEntry* trackEntry = nullptr;
            for (auto& el : tile)
            {
                if (auto* track = el.as<World::TrackElement>(); track && track->trackId() == ((tad >> 3) & 0x3F) && track->rotation() == (tad & 3))
                {
                    track->setHasSignal(true);
                    trackEntry = &el;
                    break;
                }
            }
            ASSERT_NE(trackEntry, nullptr);
            auto* post = World::TileManager::insertElementAfterNoReorg(trackEntry, World::ElementType::signal, { pos.x, pos.y }, 4, 0)->as<World::SignalElement>();
            post->getLeft().setHasSignal(true);
            post->setRotation(tad & 3);
            setSignalKind(facingSide(pos, tad), kind);
        }

        World::Track::TrackConnections signal(SignalKind kind)
        {
            signalAt(start, 0, kind);
            World::Track::TrackConnections tc;
            tc.connections.push_back(World::Track::AdditionalTaDFlags::hasSignal);
            return tc;
        }
    };
}

TEST_F(WaypointSignalFixture, freeCircleDoesNotSubstituteForBlockedDirectWaypointApproach)
{
    auto pos = junction;
    uint16_t tad = 2 << 3;
    for (int i = 0; i < 4; ++i)
    {
        if (i != 0)
        {
            addPiece(pos, tad);
        }
        if (i == 1)
        {
            signalAt(pos, tad, SignalKind::path);
        }
        const auto end = World::Track::getTrackConnectionEnd(pos, tad);
        pos = end.nextPos;
        tad = static_cast<uint16_t>((2 << 3) | end.nextRotation);
    }
    ASSERT_EQ(pos, junction);
    auto* head = vehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    wagon->tileX = target.x;
    wagon->tileY = target.y;
    wagon->moveTo(target);
    const auto tc = signal(SignalKind::path);
    for (auto mode : { ReserveStart::pathSignal, ReserveStart::chainSignal, ReserveStart::extend })
    {
        EXPECT_FALSE(reserveFrom(*head, start, tc, mode));
        EXPECT_TRUE(isTrainWaiting(enumValue(head->id)));
        EXPECT_EQ(pathHoldCount(), 0U);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    }
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    EXPECT_EQ(pendingPath(enumValue(head->id))->back().pos, target);
    EXPECT_EQ(pendingPath(enumValue(head->id))->size(), 4U);
}

TEST_F(WaypointSignalFixture, blockedUnsignalledExtensionKeepsApproachPostRedAcrossRetries)
{
    using namespace Vehicles;
    auto* head = vehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    const auto owner = enumValue(head->id);
    const auto side = facingSide(start, 0);
    signalAt(start, 0, SignalKind::path);
    World::SignalElement* post = nullptr;
    // Locate the native post, independently of the tile's surface/track order.
    for (auto& el : World::TileManager::get(start))
    {
        if (auto* signal = el.as<World::SignalElement>())
        {
            post = signal;
        }
    }
    ASSERT_NE(post, nullptr);
    post->getLeft().setFrame(6); // Native yellow from the planner passing a later signal.
    post->getLeft().setUnk4(2);
    post->getLeft().setAllLights(0xF);
    const auto begin = head->routingHandle;
    RoutingManager::setRouting(begin, World::Track::AdditionalTaDFlags::hasSignal);
    head->routingHandle.setIndex(1);
    RoutingManager::setRouting(head->routingHandle, 0);
    head->tileX = junction.x;
    head->tileY = junction.y;
    setPathMode(owner, true);
    World::Track::TrackConnections tc;
    tc.connections.push_back(0); // Failed decision is beyond the approach post.
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        setSignalKind(side, kind);
        EXPECT_FALSE(reserveFrom(*head, blocked, tc, ReserveStart::extend));
        EXPECT_TRUE(isSignalWaiting(side));
        EXPECT_EQ(postFrameForDisplay(kind, isSignalWaiting(side), post->getLeft().frame(), 7), 3);
        EXPECT_EQ(post->getLeft().frame(), 6); // Rendering must not change packed state or lights.
        EXPECT_EQ(post->getLeft().getUnk4(), 2);
        EXPECT_EQ(post->getLeft().allLights(), 0xF);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 1);
        EXPECT_FALSE(reserveFrom(*head, blocked, tc, ReserveStart::extend));
        EXPECT_TRUE(isSignalWaiting(side));
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 15);
    }
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    ASSERT_TRUE(reserveFrom(*head, blocked, tc, ReserveStart::extend));
    EXPECT_FALSE(isSignalWaiting(side));
    EXPECT_EQ(postFrameForDisplay(SignalKind::path, isSignalWaiting(side), post->getLeft().frame(), 7), 6);
    EXPECT_EQ(head->currentOrder, 0);
}

TEST_F(WaypointSignalFixture, approachWaitStopsAtFacingBlockBoundary)
{
    using namespace Vehicles;
    auto* head = vehicle();
    auto* other = vehicle();
    occupiedWagon(other->id);
    signalAt(start, 0, SignalKind::chain);
    signalAt(junction, 0, SignalKind::none);
    RoutingManager::setRouting(head->routingHandle, World::Track::AdditionalTaDFlags::hasSignal);
    head->routingHandle.setIndex(1);
    RoutingManager::setRouting(head->routingHandle, World::Track::AdditionalTaDFlags::hasSignal);
    head->tileX = junction.x;
    head->tileY = junction.y;
    setPathMode(enumValue(head->id), true);
    World::Track::TrackConnections tc;
    tc.connections.push_back(0);
    EXPECT_FALSE(reserveFrom(*head, blocked, tc, ReserveStart::extend));
    EXPECT_FALSE(isSignalWaiting(facingSide(start, 0)));
    EXPECT_FALSE(isSignalWaiting(facingSide(junction, 0)));
}

TEST_F(WaypointSignalFixture, approachWaitUsesNearestFacingPostAcrossRoutingRingWrap)
{
    using namespace Vehicles;
    auto* head = vehicle();
    signalAt(start, 0, SignalKind::path);
    signalAt(junction, 0, SignalKind::chain);
    // An opposite-direction post must never inherit this train's red wait.
    for (auto& el : World::TileManager::get(junction))
    {
        if (auto* post = el.as<World::SignalElement>())
        {
            post->getRight().setHasSignal(true);
        }
    }
    const auto opposite = behindSide(junction, 0);
    setSignalKind(opposite, SignalKind::path);
    auto handle = head->routingHandle;
    handle.setIndex(62);
    RoutingManager::setRouting(handle, World::Track::AdditionalTaDFlags::hasSignal);
    handle.setIndex(63);
    RoutingManager::setRouting(handle, World::Track::AdditionalTaDFlags::hasSignal);
    RoutingManager::setRouting(head->routingHandle, 0); // Slot zero follows slot 63.
    head->tileX = blocked.x;
    head->tileY = blocked.y;
    World::Track::TrackConnections tc;
    onPathWait(*head, target, tc);
    EXPECT_TRUE(isSignalWaiting(facingSide(junction, 0)));
    EXPECT_FALSE(isSignalWaiting(facingSide(start, 0)));
    EXPECT_FALSE(isSignalWaiting(opposite));
    clearWaitingSignals(enumValue(head->id));

    // A train travelling in reverse uses the right-hand side of the same post.
    RoutingManager::resetRoutings(head->routingHandle);
    const auto reverse = entranceOf(opposite);
    head->tileX = reverse.pos.x;
    head->tileY = reverse.pos.y;
    RoutingManager::setRouting(head->routingHandle, reverse.connection | World::Track::AdditionalTaDFlags::hasSignal);
    onPathWait(*head, start, tc);
    EXPECT_TRUE(isSignalWaiting(opposite));
    EXPECT_FALSE(isSignalWaiting(facingSide(junction, 0)));
}

TEST_F(WaypointSignalFixture, nativeProgressClearsOnlyItsOwnApproachWait)
{
    using namespace Vehicles;
    auto* head = vehicle();
    auto* other = vehicle();
    signalAt(start, 0, SignalKind::chain);
    RoutingManager::setRouting(head->routingHandle, World::Track::AdditionalTaDFlags::hasSignal);
    const auto side = facingSide(start, 0);
    World::Track::TrackConnections tc;
    onPathWait(*head, junction, tc); // Native protected-piece refusal also records this wait.
    setWaitingSignals(enumValue(other->id), { side });
    EXPECT_TRUE(isTrainWaiting(enumValue(head->id)));
    onHeadEntersPiece(*head, junction, 0);
    EXPECT_FALSE(isTrainWaiting(enumValue(head->id)));
    EXPECT_TRUE(isSignalWaiting(side));
    forgetTrain(enumValue(other->id));
    EXPECT_FALSE(isSignalWaiting(side));
}

TEST(PathSignalTests, requiredWaypointCannotBeReachedByCirclingBackAfterAReservationBoundary)
{
    TestGraph graph;
    graph.add(1, 2);
    auto& goal = graph.add(2, 3);
    goal.isTarget = true;
    goal.blocked = true;
    graph.add(2, 8);
    graph.add(8, 9).signal = EntrySignal::path;
    graph.add(9, 1).signal = EntrySignal::path;
    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = request.requireTarget = true;
    for (auto mode : { SearchMode::pathSignal, SearchMode::chainSignal, SearchMode::extend })
    {
        request.mode = mode;
        EXPECT_FALSE(findReservation(graph, request).found);
    }
}

TEST_F(WaypointSignalFixture, junctionSignalsReserveBeforeTrainHasEnteredPathMode)
{
    auto* head = vehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    signalAt(junction, 0, SignalKind::path);
    const auto tc = World::Track::getTrackConnections(junction, World::TrackData::getUnkTrack(0).rotationBegin, head->owner, 0, 0, 0);
    ASSERT_EQ(tc.connections.size(), 2U);
    ASSERT_FALSE(isInPathMode(enumValue(head->id)));
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        setSignalKind(facingSide(junction, 0), kind);
        const auto mode = reservationStartAtJunction(*head, junction, tc);
        ASSERT_TRUE(mode);
        EXPECT_FALSE(reserveFrom(*head, junction, tc, *mode));
        EXPECT_TRUE(isSignalWaiting(facingSide(junction, 0)));
        EXPECT_EQ(pathHoldCount(), 0U);
        EXPECT_EQ(head->currentOrder, 0U);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    }
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    const auto mode = reservationStartAtJunction(*head, junction, tc);
    ASSERT_TRUE(mode);
    const auto chosen = reserveFrom(*head, junction, tc, *mode);
    ASSERT_TRUE(chosen);
    EXPECT_EQ(*chosen & World::Track::AdditionalTaDFlags::basicTaDMask, 0U);
    EXPECT_EQ(pendingPath(enumValue(head->id))->back().pos, target);
}

TEST_F(WaypointSignalFixture, ordinaryJunctionsRemainVanillaUnlessTrainAlreadyUsesPathMode)
{
    auto* head = vehicle();
    const auto tc = World::Track::getTrackConnections(junction, World::TrackData::getUnkTrack(0).rotationBegin, head->owner, 0, 0, 0);
    ASSERT_EQ(tc.connections.size(), 2U);
    EXPECT_FALSE(reservationStartAtJunction(*head, junction, tc));
    setPathMode(enumValue(head->id), true);
    EXPECT_EQ(reservationStartAtJunction(*head, junction, tc), ReserveStart::extend);
    setPathMode(enumValue(head->id), false);
    signalAt(junction, 0, SignalKind::chain);
    auto signalled = World::Track::getTrackConnections(junction, World::TrackData::getUnkTrack(0).rotationBegin, head->owner, 0, 0, 0);
    EXPECT_TRUE(reservationStartAtJunction(*head, junction, signalled));
    head->var_5C = 1;
    EXPECT_FALSE(reservationStartAtJunction(*head, junction, signalled));
}

TEST(PathSignalTests, changingOutgoingPieceDoesNotDisguiseReturningToSameJunction)
{
    TestGraph graph;
    graph.add(2, 8);
    graph.add(8, 9).signal = EntrySignal::path;
    graph.add(9, 2).isTarget = true;
    auto wrong = graph.start(2);
    wrong.step.tad = 16;
    auto correct = graph.start(2);
    correct.isTarget = correct.blocked = true;
    SearchRequest request;
    request.starts = { wrong, correct };
    request.hasTarget = request.requireTarget = true;
    EXPECT_FALSE(findReservation(graph, request).found);
    request.starts.back().blocked = false;
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(result.reservation.front().step.tad, 0U);
    EXPECT_EQ(result.reservation.size(), 1U);
}

TEST(PathSignalTests, requiredAlternativesWithDifferentHistoryAreNotCollapsedByEndpoint)
{
    TestGraph graph;
    graph.add(1, 2).step.tad = 16;
    graph.add(2, 4).signal = EntrySignal::path;
    graph.add(1, 8).cost = 100;
    graph.add(8, 4).signal = EntrySignal::path;
    graph.add(4, 5);
    graph.add(5, 2).isTarget = true;
    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = request.requireTarget = true;
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_TRUE(result.reachedTarget);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 8 }));
}

TEST(PathSignalTests, distinctJunctionEntrancesAndHeightsRemainValidAlternatives)
{
    for (const bool differentHeight : { false, true })
    {
        TestGraph graph;
        graph.add(2, 8);
        graph.add(8, 9).signal = EntrySignal::path;
        auto& goal = graph.add(9, 2);
        goal.isTarget = true;
        goal.entryDirection = differentHeight ? 0 : 1;
        goal.step.pos.z = differentHeight ? 16 : 0;
        SearchRequest request;
        request.starts.push_back(graph.start(2));
        request.hasTarget = request.requireTarget = true;
        EXPECT_TRUE(findReservation(graph, request).found);
    }
}

TEST(PathSignalTests, mixedJunctionStartsUseTheirOwnChainRulesAndRejectOccupiedBlockStarts)
{
    TestGraph graph;
    auto& exit = graph.add(1, 2);
    exit.signal = EntrySignal::block;
    exit.exitOccupied = true;
    graph.add(2, 3).isTarget = true;
    SearchRequest request;
    auto chain = graph.start(1);
    chain.signal = EntrySignal::chain;
    request.starts.push_back(chain);
    request.hasTarget = request.requireTarget = true;
    EXPECT_FALSE(findReservation(graph, request).found); // Automatic path mode must respect this chain arm.
    exit.exitOccupied = false;
    EXPECT_TRUE(findReservation(graph, request).found);
    request.starts.front().signal = EntrySignal::block;
    request.starts.front().exitOccupied = true;
    EXPECT_FALSE(findReservation(graph, request).found);
}

TEST_F(WaypointSignalFixture, blockedWaypointWaitsInsteadOfTakingFreeDeadEnd)
{
    auto* head = vehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    const auto tc = signal(SignalKind::path);
    const auto cursor = head->currentOrder;
    EXPECT_FALSE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    EXPECT_TRUE(isTrainWaiting(enumValue(head->id)));
    EXPECT_EQ(pathHoldCount(), 0U);
    EXPECT_EQ(head->currentOrder, cursor);
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    ScenarioManager::setScenarioTicks(15);
    EXPECT_FALSE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    ScenarioManager::setScenarioTicks(16);
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    const auto* path = pendingPath(enumValue(head->id));
    ASSERT_NE(path, nullptr);
    ASSERT_EQ(path->size(), 4U);
    for (const auto& step : *path)
    {
        EXPECT_EQ(step.connection & World::Track::AdditionalTaDFlags::basicTaDMask, 0U);
    }
    EXPECT_EQ(path->back().pos, target);
    EXPECT_FALSE(isTrainWaiting(enumValue(head->id)));
    EXPECT_EQ(head->currentOrder, cursor);
}

TEST_F(WaypointSignalFixture, sharedDirectionalWaypointWaitsAtChainAndCombinedSignals)
{
    auto* head = vehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    const auto line = Lines::createLine("Required waypoint", TransportMode::rail, 4);
    ASSERT_TRUE(line.ok);
    for (const auto pos : { target, start })
    {
        Lines::LineOrder order;
        order.type = Vehicles::OrderType::RouteWaypoint;
        order.tileX = pos.x / 32;
        order.tileY = pos.y / 32;
        order.baseZ = 2; // Waypoints encode height in 8-unit steps, track elements in 4.
        ASSERT_TRUE(Lines::addOrder(line.id, order).ok);
    }
    ASSERT_TRUE(Lines::acceptStopKinds(line.id, Lines::LineKind::rail).ok);
    ASSERT_TRUE(Lines::assignVehicle(line.id, enumValue(head->id), TransportMode::rail).ok);
    const auto tc = signal(SignalKind::chain);
    for (auto kind : { SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        setSignalKind(facingSide(start, 0), kind);
        EXPECT_FALSE(reserveFrom(*head, start, tc, ReserveStart::chainSignal));
        EXPECT_TRUE(isSignalWaiting(facingSide(start, 0)));
        EXPECT_EQ(pathHoldCount(), 0U);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    }
    EXPECT_EQ(head->currentOrder, 0U);
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::chainSignal));
    EXPECT_EQ(pendingPath(enumValue(head->id))->back().pos, target);
    EXPECT_FALSE(isSignalWaiting(facingSide(start, 0)));
}

TEST_F(WaypointSignalFixture, sameTileWithWrongWaypointDirectionDoesNotCountAsTarget)
{
    auto* head = vehicle();
    head->getCurrentOrders().begin()->as<Vehicles::OrderRouteWaypoint>()->setDirection(1);
    const auto tc = signal(SignalKind::path);
    EXPECT_FALSE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    EXPECT_TRUE(isSignalWaiting(facingSide(start, 0)));
    EXPECT_EQ(pathHoldCount(), 0U);
    EXPECT_EQ(head->currentOrder, 0U);
}

TEST_F(WaypointSignalFixture, evaluatingTrainIsExcludedFromWaypointOccupation)
{
    auto* head = vehicle();
    occupiedWagon(head->id);
    const auto tc = signal(SignalKind::path);
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    EXPECT_EQ(pendingPath(enumValue(head->id))->back().pos, target);
    EXPECT_FALSE(isTrainWaiting(enumValue(head->id)));
}

TEST(PathSignalTests, requiredTargetRejectsUnrelatedFreeFallbackInEverySearchMode)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).blocked = true;
    graph.add(3, 4).isTarget = true;
    graph.add(2, 8); // Unrelated free dead end.
    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = true;
    request.requireTarget = true;
    for (auto mode : { SearchMode::pathSignal, SearchMode::chainSignal, SearchMode::extend })
    {
        request.mode = mode;
        const auto result = findReservation(graph, request);
        EXPECT_FALSE(result.found);
        EXPECT_TRUE(result.reservation.empty());
    }
    request.requireTarget = false; // Station/no-strict-order compatibility keeps its fallback.
    EXPECT_TRUE(findReservation(graph, request).found);
}

TEST(PathSignalTests, requiredWaypointCanUseAnotherRouteToTheExactTarget)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).blocked = true;
    graph.add(3, 4).isTarget = true;
    graph.add(2, 5);
    graph.add(5, 6);
    graph.add(6, 4).isTarget = true;
    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = request.requireTarget = true;
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_TRUE(result.reachedTarget);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 5, 6, 4 }));
}

TEST(PathSignalTests, waypointBeyondNextSignalReservesOnlyItsFreeApproach)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).signal = EntrySignal::path;
    graph.add(3, 4).blocked = true;
    graph.add(4, 5).isTarget = true;
    graph.add(1, 8); // Free fallback in the wrong direction cannot win.
    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = request.requireTarget = true;
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_TRUE(result.reachedTarget);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2 }));
}

TEST(PathSignalTests, requiredWaypointDoesNotTakeWrongChainExitOrBackOfOneWaySignal)
{
    TestGraph graph;
    graph.add(1, 2);
    auto& exit = graph.add(2, 3);
    exit.signal = EntrySignal::block;
    exit.exitOccupied = true;
    graph.add(3, 4).isTarget = true;
    graph.add(2, 8).signal = EntrySignal::block;
    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.mode = SearchMode::chainSignal;
    request.hasTarget = request.requireTarget = true;
    EXPECT_FALSE(findReservation(graph, request).found);
    TestGraph oneWay;
    oneWay.add(1, 2).signal = EntrySignal::path;
    oneWay.add(2, 3).signal = EntrySignal::blockedFromBehind;
    oneWay.add(3, 4).isTarget = true;
    request.mode = SearchMode::pathSignal;
    EXPECT_FALSE(findReservation(oneWay, request).found);
}

TEST(PathSignalTests, requiredWaypointProofContinuesBeyondReservationLengthWithoutClaimingMore)
{
    TestGraph graph;
    for (int16_t i = 1; i < 80; ++i)
    {
        graph.add(i, i + 1).isTarget = i == 79;
    }
    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = request.requireTarget = true;
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_TRUE(result.reachedTarget);
    ASSERT_EQ(result.reservation.size(), 64U);
    EXPECT_EQ(result.reservation.front().step.pos.x, 1);
    EXPECT_EQ(result.reservation.back().step.pos.x, 64);
    request.maxExpansions = 64;
    EXPECT_FALSE(findReservation(graph, request).found); // No unproved fallback on exhaustion.
    request.hasTarget = false;
    EXPECT_FALSE(findReservation(graph, request).found);
}

TEST(PathSignalTests, requiredWaypointWaitsAtBlockedStartAndOccupiedTarget)
{
    TestGraph graph;
    auto& target = graph.add(1, 2);
    target.isTarget = true;
    target.blocked = true;
    SearchRequest request;
    auto start = graph.start(1);
    request.starts.push_back(start);
    request.hasTarget = request.requireTarget = true;
    EXPECT_FALSE(findReservation(graph, request).found);
    target.blocked = false;
    request.starts.front().blocked = true;
    EXPECT_FALSE(findReservation(graph, request).found);
    request.starts.front().blocked = false;
    const auto result = findReservation(graph, request);
    EXPECT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2 }));
}

TEST(PathSignalTests, waitingSpecialPostsShowRedAndResumeTheirNativeFrame)
{
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        EXPECT_EQ(postFrameForDisplay(kind, true, 0, 4), 3);
        EXPECT_EQ(postFrameForDisplay(kind, true, 6, 7), 3);
        EXPECT_EQ(postFrameForDisplay(kind, false, 0, 4), 0);
        EXPECT_EQ(postFrameForDisplay(kind, false, 6, 7), 6);
    }
    EXPECT_EQ(postFrameForDisplay(SignalKind::none, true, 0, 4), 0);
    EXPECT_EQ(postFrameForDisplay(SignalKind::path, true, 0, 1), 0);
    EXPECT_EQ(postFrameForDisplay(SignalKind::path, true, 0, 0), 0);
}

TEST(PathSignalTests, waitingPostRemainsRedUntilEveryWaitingTrainClearsAndInvalidatesOnlyChanges)
{
    clearAll();
    static int dirty = 0;
    dirty = 0;
    setReservationViewDirty(+[](const PieceKey&) { ++dirty; });
    const SideKey side{ PieceKey{ 320, 320, 16, 0, 0 }, 0 };
    setSignalKind(side, SignalKind::path);
    setWaitingSignals(1, { side });
    EXPECT_EQ(dirty, 1);
    setWaitingSignals(1, { side });
    setWaitingSignals(2, { side });
    EXPECT_EQ(dirty, 1);
    EXPECT_TRUE(isTrainWaiting(1));
    EXPECT_TRUE(isTrainWaiting(2));
    clearWaitingSignals(1);
    EXPECT_TRUE(isSignalWaiting(side));
    EXPECT_FALSE(isTrainWaiting(1));
    EXPECT_EQ(dirty, 1);
    clearWaitingSignals(2);
    EXPECT_FALSE(isSignalWaiting(side));
    EXPECT_EQ(dirty, 2);
    setWaitingSignals(3, { side });
    forgetTrain(3);
    EXPECT_FALSE(isSignalWaiting(side));
    EXPECT_EQ(dirty, 4);
    setWaitingSignals(4, { side });
    clearAll();
    EXPECT_FALSE(isSignalWaiting(side));
    EXPECT_EQ(dirty, 6);
    setReservationViewDirty(nullptr);
    clearAll();
}

TEST(PathSignalTests, registryStoresOnlySpecialKinds)
{
    clearAll();
    const SideKey side{ PieceKey{ 320, 320, 0, 0, 0 }, 1 };
    setSignalKind(side, SignalKind::chain);
    EXPECT_EQ(getSignalKind(side), SignalKind::chain);
    EXPECT_EQ(specialSignalCount(), 1U);
    setSignalKind(side, SignalKind::none);
    EXPECT_EQ(getSignalKind(side), SignalKind::none);
    EXPECT_EQ(specialSignalCount(), 0U);
}

TEST(PathSignalTests, sidesAndPiecesRoundTrip)
{
    const PieceKey piece{ 640, 640, 32, 1, 4 };
    for (uint8_t side = 0; side < 2; ++side)
    {
        const auto entrance = entranceOf(SideKey{ piece, side });
        EXPECT_EQ(canonicalPiece(entrance.pos, entrance.connection), piece);
        EXPECT_EQ(facingSide(entrance.pos, entrance.connection).side, side);
        EXPECT_EQ(behindSide(entrance.pos, entrance.connection).side, side == 0 ? 1 : 0);
    }
}

TEST(PathSignalTests, crossingConflictsAndParallelDoesNot)
{
    const PieceKey alongX{ 320, 320, 0, 0, 0 };
    const PieceKey crossing{ 320, 320, 0, 1, 0 };
    const PieceKey parallel{ 320, 352, 0, 0, 0 };
    const PieceKey bridge{ 320, 320, 64, 1, 0 };
    EXPECT_TRUE(piecesConflict(alongX, crossing));
    EXPECT_FALSE(piecesConflict(alongX, parallel));
    EXPECT_FALSE(piecesConflict(alongX, bridge));

    const PieceKey diagonalA{ 0, 0, 0, 0, 1 };
    const PieceKey diagonalB{ 32, 32, 0, 0, 1 };
    EXPECT_FALSE(piecesConflict(diagonalA, diagonalB));
}

TEST(PathSignalTests, reservationIsAllOrNothing)
{
    clearAll();
    ASSERT_TRUE(reservePath(1, { straight(320, 320, 0) }));
    EXPECT_FALSE(reservePath(2, { straight(320, 352, 0), straight(320, 320, 1) }));
    EXPECT_FALSE(holdAt(canonicalPiece(World::Pos3{ 320, 352, 0 }, 0)).has_value());
    EXPECT_EQ(pathHoldCount(), 1U);

    ASSERT_TRUE(reservePath(2, { straight(320, 352, 0) }));
    EXPECT_EQ(pathHoldCount(), 2U);
    EXPECT_TRUE(isHeldByProtectedOther(canonicalPiece(World::Pos3{ 320, 320, 0 }, 1), 2));
    EXPECT_FALSE(isHeldByProtectedOther(canonicalPiece(World::Pos3{ 320, 320, 0 }, 0), 1));

    ASSERT_TRUE(consumePendingFront(1, World::Pos3{ 320, 320, 0 }, 0));
    EXPECT_EQ(holdAt(canonicalPiece(World::Pos3{ 320, 320, 0 }, 0))->kind, HoldKind::occupied);
    releaseHold(1, canonicalPiece(World::Pos3{ 320, 320, 0 }, 0));
    EXPECT_TRUE(canHold(canonicalPiece(World::Pos3{ 320, 320, 0 }, 1), 2));

    forgetTrain(2);
    EXPECT_EQ(holdCount(), 0U);
}

TEST(PathSignalTests, unprotectedOccupancyDoesNotStopOtherTrains)
{
    clearAll();
    const auto piece = canonicalPiece(World::Pos3{ 320, 320, 0 }, 0);
    holdOccupied(7, piece, false);
    EXPECT_FALSE(isHeldByProtectedOther(piece, 8));
    EXPECT_FALSE(canHold(piece, 8));
    holdOccupied(7, piece, true);
    EXPECT_TRUE(isHeldByProtectedOther(piece, 8));
}

TEST(PathSignalTests, chainAspectFollowsThePicture)
{
    EXPECT_EQ(evaluateChainAspect(false, 3, 0), ChainAspect::green);
    EXPECT_EQ(evaluateChainAspect(false, 3, 1), ChainAspect::blue);
    EXPECT_EQ(evaluateChainAspect(false, 3, 3), ChainAspect::red);
    EXPECT_EQ(evaluateChainAspect(true, 3, 0), ChainAspect::red);
    EXPECT_EQ(evaluateChainAspect(false, 0, 0), ChainAspect::green);
}

TEST(PathSignalTests, claimedSignalsShowTheirRoute)
{
    DisplayChoice claimed;
    claimed.claimed = true;
    EXPECT_EQ(chooseDisplayState(SignalKind::path, claimed), DisplayState::green);
    EXPECT_EQ(chooseDisplayState(SignalKind::path | SignalKind::chain, claimed), DisplayState::green);
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, claimed), DisplayState::green);

    DisplayChoice waitsAhead = claimed;
    waitsAhead.claimEndsAtWait = true;
    EXPECT_EQ(chooseDisplayState(SignalKind::path, waitsAhead), DisplayState::yellow);

    DisplayChoice clearBlock;
    EXPECT_EQ(chooseDisplayState(SignalKind::path, clearBlock), DisplayState::green);
    EXPECT_EQ(chooseDisplayState(SignalKind::path | SignalKind::chain, clearBlock), DisplayState::green);
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, clearBlock), DisplayState::green);

    DisplayChoice occupied = clearBlock;
    occupied.blockBusy = true;
    EXPECT_EQ(chooseDisplayState(SignalKind::path, occupied), DisplayState::red);
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, occupied), DisplayState::red);

    DisplayChoice aheadRed;
    aheadRed.aheadClear = false;
    EXPECT_EQ(chooseDisplayState(SignalKind::path, aheadRed), DisplayState::red);
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, aheadRed), DisplayState::red);

    DisplayChoice behindRed;
    behindRed.behindClear = false;
    EXPECT_EQ(chooseDisplayState(SignalKind::path | SignalKind::chain, behindRed), DisplayState::red);
}

TEST(PathSignalTests, chainPostFollowsItsExits)
{
    DisplayChoice noExits;
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, noExits), DisplayState::green);

    DisplayChoice busy;
    busy.blockBusy = true;
    busy.exitCount = 2;
    busy.openExitCount = 2;
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, busy), DisplayState::red);

    DisplayChoice allClosed;
    allClosed.exitCount = 2;
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, allClosed), DisplayState::red);

    DisplayChoice oneOpen;
    oneOpen.exitCount = 2;
    oneOpen.openExitCount = 1;
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, oneOpen), DisplayState::green);

    DisplayChoice waiting = oneOpen;
    waiting.trainWaitingHere = true;
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, waiting), DisplayState::red);
    EXPECT_EQ(chooseDisplayState(SignalKind::path, waiting), DisplayState::red);
}

TEST(PathSignalTests, twoStateSignalsShowProceedAsGreen)
{
    EXPECT_EQ(postStateFor(DisplayState::yellow, 2), DisplayState::green);
    EXPECT_EQ(postStateFor(DisplayState::yellow, 3), DisplayState::yellow);
    EXPECT_EQ(postStateFor(DisplayState::red, 2), DisplayState::red);
    EXPECT_EQ(postStateFor(DisplayState::green, 4), DisplayState::green);
}

TEST(PathSignalTests, claimsMarkEachTrainsLastSignal)
{
    const SideKey first{ PieceKey{ 320, 320, 0, 0, 0 }, 0 };
    const SideKey second{ PieceKey{ 352, 320, 0, 0, 0 }, 0 };
    const SideKey other{ PieceKey{ 384, 320, 0, 0, 0 }, 1 };
    const auto claims = buildSignalClaims({ { 3, { first, second } }, { 4, { other } } });
    ASSERT_EQ(claims.size(), 3U);
    EXPECT_EQ(claims.at(first).owner, 3U);
    EXPECT_FALSE(claims.at(first).isLast);
    EXPECT_TRUE(claims.at(second).isLast);
    EXPECT_EQ(claims.at(other).owner, 4U);
    EXPECT_TRUE(claims.at(other).isLast);
}

TEST(PathSignalTests, pathAndChainFlagsCombine)
{
    clearAll();
    const SideKey side{ PieceKey{ 320, 320, 0, 0, 0 }, 0 };
    setSignalKind(side, SignalKind::path | SignalKind::chain);
    EXPECT_TRUE(isPathSignal(getSignalKind(side)));
    EXPECT_TRUE(isChainSignal(getSignalKind(side)));
    EXPECT_EQ(specialSignalCount(), 1U);

    setSignalKind(side, SignalKind::path);
    EXPECT_TRUE(isPathSignal(getSignalKind(side)));
    EXPECT_FALSE(isChainSignal(getSignalKind(side)));
    EXPECT_EQ(getChainAspect(side), ChainAspect::green);
}

TEST(PathSignalTests, companionMigratesVersion1Kinds)
{
    clearAll();
    const auto save = std::filesystem::temp_directory_path() / "openloco-path-signals-v1.SV5";
    {
        std::ofstream file(companionPath(save), std::ios::binary | std::ios::trunc);
        file << "OSIGNALS 1\n";
        file << "KINDS 3\n";
        file << "320 320 0 0 0 0 1\n";
        file << "352 320 0 0 0 1 2\n";
        file << "384 320 0 0 0 0 3\n";
        file << "TRAINS 0\n";
    }
    ASSERT_TRUE(loadCompanion(save));
    EXPECT_EQ(getSignalKind(SideKey{ PieceKey{ 320, 320, 0, 0, 0 }, 0 }), SignalKind::path);
    EXPECT_EQ(getSignalKind(SideKey{ PieceKey{ 352, 320, 0, 0, 0 }, 1 }), SignalKind::path);
    EXPECT_EQ(getSignalKind(SideKey{ PieceKey{ 384, 320, 0, 0, 0 }, 0 }), SignalKind::chain);
    EXPECT_FALSE(companionNeedsLightRepair());
    std::filesystem::remove(companionPath(save));
}

TEST(PathSignalTests, companionVersion2RequestsRouteLightRepair)
{
    clearAll();
    const auto save = std::filesystem::temp_directory_path() / "openloco-path-signals-v2.SV5";
    {
        std::ofstream file(companionPath(save), std::ios::binary | std::ios::trunc);
        file << "OSIGNALS 2\n";
        file << "KINDS 1\n";
        file << "320 320 0 0 0 0 3\n";
        file << "TRAINS 0\n";
    }
    ASSERT_TRUE(loadCompanion(save));
    EXPECT_EQ(getSignalKind(SideKey{ PieceKey{ 320, 320, 0, 0, 0 }, 0 }), SignalKind::path | SignalKind::chain);
    EXPECT_TRUE(companionNeedsLightRepair());
    clearCompanionLightRepair();
    EXPECT_FALSE(companionNeedsLightRepair());
    std::filesystem::remove(companionPath(save));
}

TEST(PathSignalTests, reservationArrowTracksEveryPendingPath)
{
    clearAll();
    const auto step = straight(320, 320, 0);
    ASSERT_TRUE(reservePath(4, { step }));
    const auto side = facingSide(step.pos, step.connection);
    EXPECT_TRUE(sideHasReservation(side));
    const auto arrow = reservationArrowAt(side.piece);
    ASSERT_TRUE(arrow.has_value());
    EXPECT_EQ(arrow->owner, 4U);
    EXPECT_EQ(arrow->side, side.side);

    ASSERT_TRUE(consumePendingFront(4, step.pos, step.connection));
    const auto held = reservationArrowAt(side.piece);
    ASSERT_TRUE(held.has_value());
    EXPECT_EQ(held->owner, 4U);
    EXPECT_EQ(held->side, side.side);

    releaseHold(4, side.piece);
    EXPECT_FALSE(reservationArrowAt(side.piece).has_value());

    ASSERT_TRUE(reservePath(4, { step }));
    const SideKey waiting{ side.piece, side.side };
    setWaitingSignals(4, { waiting });
    EXPECT_TRUE(isSignalWaiting(waiting));
    EXPECT_TRUE(isTrainWaiting(4));
    EXPECT_FALSE(isTrainWaiting(5));
    clearWaitingSignals(4);
    EXPECT_FALSE(isSignalWaiting(waiting));
    EXPECT_FALSE(isTrainWaiting(4));
}

TEST(PathSignalTests, viewerFollowsTheReservedPath)
{
    clearAll();
    const auto first = straight(320, 320, 0);
    const auto second = straight(352, 320, 0);
    ASSERT_TRUE(reservePath(9, { first, second }));
    setReservationView(9);

    const auto firstPiece = canonicalPiece(first.pos, first.connection);
    const auto secondPiece = canonicalPiece(second.pos, second.connection);
    ASSERT_TRUE(viewedTravelSide(firstPiece).has_value());
    EXPECT_EQ(*viewedTravelSide(firstPiece), facingSide(first.pos, first.connection).side);
    ASSERT_TRUE(viewedTravelSide(secondPiece).has_value());

    setReservationView(kNoReservationView);
    EXPECT_FALSE(viewedTravelSide(firstPiece).has_value());
    EXPECT_FALSE(viewedTravelSide(secondPiece).has_value());
    EXPECT_EQ(reservationView(), kNoReservationView);
}

TEST(PathSignalTests, pathAndChainLooksThroughUntilAPathSignal)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).signal = EntrySignal::chain;
    graph.add(3, 4).signal = EntrySignal::path;
    graph.add(4, 5);

    SearchRequest request;
    request.mode = SearchMode::chainSignal;
    request.starts.push_back(graph.start(1));
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 3, 4, 5 }));
}

TEST(PathSignalTests, companionRoundTripAndBadFiles)
{
    clearAll();
    const SideKey path{ PieceKey{ 320, 320, 0, 0, 0 }, 0 };
    const SideKey chain{ PieceKey{ 384, 320, 0, 0, 0 }, 1 };
    setSignalKind(path, SignalKind::path);
    setSignalKind(chain, SignalKind::chain);
    ASSERT_TRUE(reservePath(5, { straight(320, 320, 0), straight(352, 320, 0) }));

    const auto save = std::filesystem::temp_directory_path() / "openloco-path-signals.SV5";
    ASSERT_TRUE(saveCompanion(save));
    {
        std::ifstream written(companionPath(save), std::ios::binary);
        std::string magic;
        int version = 0;
        written >> magic >> version;
        EXPECT_EQ(magic, "OSIGNALS");
        EXPECT_EQ(version, 3);
    }
    clearAll();
    ASSERT_TRUE(loadCompanion(save));
    EXPECT_FALSE(companionNeedsLightRepair());
    EXPECT_EQ(getSignalKind(path), SignalKind::path);
    EXPECT_EQ(getSignalKind(chain), SignalKind::chain);
    ASSERT_NE(pendingPath(5), nullptr);
    EXPECT_EQ(pendingPath(5)->size(), 2U);
    EXPECT_TRUE(isInPathMode(5));
    EXPECT_EQ(holdAt(canonicalPiece(World::Pos3{ 352, 320, 0 }, 0))->owner, 5);

    clearAll();
    const auto missing = std::filesystem::temp_directory_path() / "openloco-path-signals-missing.SV5";
    std::filesystem::remove(companionPath(missing));
    EXPECT_TRUE(loadCompanion(missing));
    EXPECT_EQ(specialSignalCount(), 0U);

    {
        std::ofstream bad(companionPath(missing), std::ios::binary | std::ios::trunc);
        bad << "NOTSIGNALS 9\n";
    }
    EXPECT_FALSE(loadCompanion(missing));
    EXPECT_EQ(specialSignalCount(), 0U);

    std::filesystem::remove(companionPath(save));
    std::filesystem::remove(companionPath(missing));
}

TEST(PathSignalTests, searchTakesTheFreeBranchAtAJunction)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).blocked = true;
    graph.add(2, 4);
    graph.add(3, 5).isTarget = true;
    graph.add(4, 6).isTarget = true;

    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = true;
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_TRUE(result.reachedTarget);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 4, 6 }));
}

TEST(PathSignalTests, pathReservationStopsBeforeTheNextSignal)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).signal = EntrySignal::path;
    graph.add(3, 4).isTarget = true;

    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = true;
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2 }));
}

TEST(PathSignalTests, chainReservesThroughToTheExitPath)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).signal = EntrySignal::chain;
    graph.add(3, 4);
    graph.add(4, 5).signal = EntrySignal::path;
    graph.add(5, 6);
    graph.add(6, 7).signal = EntrySignal::path;

    SearchRequest request;
    request.mode = SearchMode::chainSignal;
    request.starts.push_back(graph.start(1));
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 3, 4, 5, 6 }));
}

TEST(PathSignalTests, chainWaitsWhenItsOnlyExitBlockIsOccupied)
{
    TestGraph graph;
    graph.add(1, 2);
    auto& exit = graph.add(2, 3);
    exit.signal = EntrySignal::block;
    exit.exitOccupied = true;

    SearchRequest request;
    request.mode = SearchMode::chainSignal;
    request.starts.push_back(graph.start(1));
    EXPECT_FALSE(findReservation(graph, request).found);
}

TEST(PathSignalTests, chainUsesTheOpenExitWhenSomeAreRed)
{
    TestGraph graph;
    graph.add(1, 2);
    auto& red = graph.add(2, 3);
    red.signal = EntrySignal::block;
    red.exitOccupied = true;
    graph.add(2, 4).signal = EntrySignal::block;
    graph.add(4, 5);
    graph.add(5, 6).signal = EntrySignal::path;

    SearchRequest request;
    request.mode = SearchMode::chainSignal;
    request.starts.push_back(graph.start(1));
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 4, 5 }));
}

TEST(PathSignalTests, oneWaySignalFromBehindIsADeadEnd)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).signal = EntrySignal::blockedFromBehind;

    SearchRequest request;
    request.starts.push_back(graph.start(1));
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2 }));
}

TEST(PathSignalTests, noOrdersPicksTheNearestSafePoint)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3);
    graph.add(3, 4).signal = EntrySignal::path;
    graph.add(2, 10);
    graph.add(10, 11);
    graph.add(11, 12);
    graph.add(12, 13).signal = EntrySignal::path;

    SearchRequest request;
    request.starts.push_back(graph.start(1));
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_FALSE(result.reachedTarget);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 3 }));
}

TEST(PathSignalTests, blockedStartCannotBeReserved)
{
    TestGraph graph;
    auto start = graph.start(1);
    start.blocked = true;
    SearchRequest request;
    request.starts.push_back(start);
    EXPECT_FALSE(findReservation(graph, request).found);
}
