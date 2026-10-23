#include <OpenLoco/Entities/EntityManager.h>
#include <OpenLoco/Graphics/Colour.h>
#include <OpenLoco/Lines/Line.h>
#include <OpenLoco/Map/SignalElement.h>
#include <OpenLoco/Map/StationElement.h>
#include <OpenLoco/Map/TileManager.h>
#include <OpenLoco/Map/Track/SubpositionData.h>
#include <OpenLoco/Map/Track/Track.h>
#include <OpenLoco/Map/Track/TrackData.h>
#include <OpenLoco/Map/TrackElement.h>
#include <OpenLoco/Math/Vector.hpp>
#include <OpenLoco/Random.h>
#include <OpenLoco/Rules/Rules.h>
#include <OpenLoco/Scenario/ScenarioManager.h>
#include <OpenLoco/Signals/PathSearch.h>
#include <OpenLoco/Signals/PathSignals.h>
#include <OpenLoco/Ui/Windows/Construction/Construction.h>
#include <OpenLoco/Vehicles/OrderManager.h>
#include <OpenLoco/Vehicles/RoutingManager.h>
#include <OpenLoco/Vehicles/Vehicle1.h>
#include <OpenLoco/Vehicles/Vehicle2.h>
#include <OpenLoco/Vehicles/VehicleBody.h>
#include <OpenLoco/Vehicles/VehicleBogie.h>
#include <OpenLoco/Vehicles/VehicleHead.h>
#include <OpenLoco/Vehicles/VehicleTail.h>
#include <OpenLoco/World/CompanyManager.h>
#include <OpenLoco/World/Station.h>
#include <OpenLoco/World/StationManager.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <string>

using namespace OpenLoco;
using namespace OpenLoco::Signals;
using namespace OpenLoco::Literals;

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

        void approaching(Vehicles::VehicleHead& head)
        {
            const auto before = start - World::TrackData::getUnkTrack(0).pos;
            addPiece(before, 0);
            Vehicles::Vehicle train(head);
            train.applyToComponents([&](auto& component) {
                component.tileX = before.x;
                component.tileY = before.y;
                component.tileBaseZ = before.z / 4;
                component.trackAndDirection.track._data = 0;
            });
            Vehicles::RoutingManager::setRouting(head.routingHandle, 0);
        }

        Vehicles::Sub4ACEE7Result decide(Vehicles::VehicleHead& head)
        {
            head.subPosition = static_cast<uint16_t>(World::TrackData::getTrackSubPositon(head.trackAndDirection.track._data).size() - 1);
            head.remainingDistance = 0;
            head.var_3C = 0;
            return head.sub_4ACEE7(0x368A, 0, false);
        }

        void expectRefused(Vehicles::VehicleHead& head)
        {
            auto next = head.routingHandle;
            next.setIndex(next.getIndex() + 1);
            const auto beforeOrder = head.currentOrder;
            const auto result = decide(head);
            EXPECT_EQ(result.status, 3);
            EXPECT_EQ(result.flags & kPathWaitFlag, kPathWaitFlag);
            EXPECT_EQ(Vehicles::RoutingManager::getRouting(next), Vehicles::RoutingManager::kAllocatedButFreeRouting);
            EXPECT_EQ(head.currentOrder, beforeOrder);
            EXPECT_TRUE(isTrainWaiting(enumValue(head.id)));
            auto* physicalFront = Vehicles::Vehicle(head).veh2;
            const auto beforePosition = physicalFront->getTrackLoc();
            physicalFront->subPosition = static_cast<uint16_t>(World::TrackData::getTrackSubPositon(physicalFront->trackAndDirection.track._data).size() - 1);
            physicalFront->remainingDistance = 0;
            const auto motion = physicalFront->updateTrackMotion(0x368A, false);
            EXPECT_TRUE(motion.hasFlags(Vehicles::UpdateVar1136114Flags::unk_m00));
            EXPECT_EQ(physicalFront->getTrackLoc(), beforePosition);
        }

        void checkCommittedPhysicalEntry(SignalKind kind, bool plannerPassedBlock = false)
        {
            auto* head = vehicle();
            auto* other = vehicle();
            approaching(*head);
            signal(kind);
            ASSERT_EQ(decide(*head).status, 0);
            ASSERT_TRUE(isInPathMode(enumValue(head->id)));
            ASSERT_NE(pendingPath(enumValue(head->id)), nullptr);
            ASSERT_NE(pendingPath(enumValue(head->id))->front().pos, start);
            if (plannerPassedBlock)
            {
                onPassedBlockSignal(*head);
                ASSERT_FALSE(isInPathMode(enumValue(head->id)));
            }
            auto* wagon = occupiedWagon(other->id);
            wagon->tileX = start.x;
            wagon->tileY = start.y;
            wagon->moveTo(start);
            auto* front = Vehicles::Vehicle(*head).veh2;
            const auto before = front->getTrackLoc();
            const auto handle = front->routingHandle;
            auto ahead = handle;
            ahead.setIndex(ahead.getIndex() + 1);
            Vehicles::Vehicle(*head).veh1->routingHandle = ahead;
            front->subPosition = static_cast<uint16_t>(World::TrackData::getTrackSubPositon(0).size() - 1);
            front->remainingDistance = 0;
            const auto refused = front->updateTrackMotion(0x368A, true);
            EXPECT_TRUE(refused.hasFlags(Vehicles::UpdateVar1136114Flags::unk_m00));
            EXPECT_EQ(front->getTrackLoc(), before);
            EXPECT_EQ(front->routingHandle, handle);
            EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(start, 0)));
            wagon->tileX = -1;
            wagon->moveTo({ 960, 960, 16 });
            front->updateTrackMotion(0x368A, true);
            EXPECT_EQ(front->getTrackLoc(), start);
        }
    };
}

TEST_F(WaypointSignalFixture, physicalFrontRechecksCommittedPathSignalPiece)
{
    checkCommittedPhysicalEntry(SignalKind::path);
}

TEST_F(WaypointSignalFixture, waypointBeforeStoppedWagonDoesNotEndSignalProtection)
{
    auto* head = vehicle();
    auto* other = vehicle();
    other->status = Vehicles::Status::stopped;
    occupiedWagon(other->id);
    auto* waypoint = head->getCurrentOrders().begin()->as<Vehicles::OrderRouteWaypoint>();
    waypoint->setWaypoint({ static_cast<coord_t>(junction.x / 32), static_cast<coord_t>(junction.y / 32) }, 2);
    approaching(*head);
    signal(SignalKind::path);
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        setSignalKind(facingSide(start, 0), kind);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
        refreshApproachSignals(*head);
        EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(start, 0)));
        expectRefused(*head);
    }
}

TEST_F(WaypointSignalFixture, waypointBeforeForeignReservationDoesNotEndSignalProtection)
{
    auto* head = vehicle();
    auto* other = vehicle();
    ASSERT_TRUE(reservePath(enumValue(other->id), { { blocked, 0 }, { target, 0 } }));
    auto* waypoint = head->getCurrentOrders().begin()->as<Vehicles::OrderRouteWaypoint>();
    waypoint->setWaypoint({ static_cast<coord_t>(junction.x / 32), static_cast<coord_t>(junction.y / 32) }, 2);
    approaching(*head);
    signal(SignalKind::path);
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        setSignalKind(facingSide(start, 0), kind);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
        expectRefused(*head);
        EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(start, 0)));
    }
}

TEST_F(WaypointSignalFixture, sharedWaypointBeforeStoppedWagonKeepsSignalRedUntilClear)
{
    auto* head = vehicle();
    auto* other = vehicle();
    other->status = Vehicles::Status::stopped;
    auto* wagon = occupiedWagon(other->id);
    const auto line = Lines::createLine("Waypoint protection", TransportMode::rail, 4);
    ASSERT_TRUE(line.ok);
    Lines::LineOrder order;
    order.type = Vehicles::OrderType::RouteWaypoint;
    order.tileX = junction.x / 32;
    order.tileY = junction.y / 32;
    order.baseZ = 2;
    ASSERT_TRUE(Lines::addOrder(line.id, order).ok);
    ASSERT_TRUE(Lines::acceptStopKinds(line.id, Lines::LineKind::rail).ok);
    ASSERT_TRUE(Lines::assignVehicle(line.id, enumValue(head->id), TransportMode::rail).ok);
    approaching(*head);
    signal(SignalKind::chain);
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        setSignalKind(facingSide(start, 0), kind);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
        expectRefused(*head);
        EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(start, 0)));
    }
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    ASSERT_EQ(decide(*head).status, 0);
    ASSERT_NE(pendingPath(enumValue(head->id)), nullptr);
    EXPECT_EQ(pendingPath(enumValue(head->id))->back().pos, target);
    EXPECT_FALSE(isSignalBlockedForDisplay(facingSide(start, 0)));
}

TEST_F(WaypointSignalFixture, passingWaypointReplansLaterForksWithoutReleasingOccupiedHolds)
{
    auto* head = vehicle();
    auto* first = head->getCurrentOrders().begin()->as<Vehicles::OrderRouteWaypoint>();
    first->setWaypoint({ static_cast<coord_t>(junction.x / 32), static_cast<coord_t>(junction.y / 32) }, 2);
    Vehicles::OrderRouteWaypoint next({ static_cast<coord_t>(target.x / 32), static_cast<coord_t>(target.y / 32) }, 2, 0, 0);
    Vehicles::OrderManager::insertOrder(head, sizeof(Vehicles::OrderRouteWaypoint), &next);
    approaching(*head);
    signal(SignalKind::path);
    ASSERT_EQ(decide(*head).status, 0);
    ASSERT_NE(pendingPath(enumValue(head->id)), nullptr);
    ASSERT_EQ(pendingPath(enumValue(head->id))->back().pos, target);
    head->tileX = start.x;
    head->tileY = start.y;
    head->routingHandle.setIndex(head->routingHandle.getIndex() + 1);
    ASSERT_EQ(decide(*head).status, 0);
    EXPECT_EQ(head->currentOrder, sizeof(Vehicles::OrderRouteWaypoint));
    EXPECT_TRUE(pendingPath(enumValue(head->id))->empty());
    for (auto pos : { start, junction })
    {
        ASSERT_TRUE(holdAt(canonicalPiece(pos, 0)));
        EXPECT_EQ(holdAt(canonicalPiece(pos, 0))->kind, HoldKind::occupied);
        EXPECT_TRUE(holdAt(canonicalPiece(pos, 0))->isProtected);
    }
    const auto tc = World::Track::getTrackConnections(blocked, 0, head->owner, 0, 0, 0);
    ASSERT_TRUE(reserveFrom(*head, blocked, tc, ReserveStart::extend));
    EXPECT_EQ(pendingPath(enumValue(head->id))->back().pos, target);
}

TEST_F(WaypointSignalFixture, physicalFrontRechecksCommittedChainSignalPiece)
{
    checkCommittedPhysicalEntry(SignalKind::chain);
}

TEST_F(WaypointSignalFixture, physicalFrontRechecksCommittedCombinedSignalPiece)
{
    checkCommittedPhysicalEntry(SignalKind::path | SignalKind::chain);
}

TEST_F(WaypointSignalFixture, physicalFrontProtectsRouteAfterPlannerPassesOrdinaryBlockSignal)
{
    checkCommittedPhysicalEntry(SignalKind::path, true);
}

TEST_F(WaypointSignalFixture, ordinaryUnprotectedPhysicalRoutingKeepsNativeBehavior)
{
    auto* head = vehicle();
    approaching(*head);
    auto train = Vehicles::Vehicle(*head);
    auto next = head->routingHandle;
    next.setIndex(next.getIndex() + 1);
    Vehicles::RoutingManager::setRouting(next, 0);
    train.veh1->routingHandle = next;
    // A special post elsewhere must not opt this ordinary train into path mode.
    signalAt(target, 0, SignalKind::path);
    auto* front = train.veh2;
    front->subPosition = static_cast<uint16_t>(World::TrackData::getTrackSubPositon(0).size() - 1);
    front->remainingDistance = 0;
    EXPECT_FALSE(front->updateTrackMotion(0x368A, true).hasFlags(Vehicles::UpdateVar1136114Flags::unk_m00));
    EXPECT_EQ(front->getTrackLoc(), start);
    EXPECT_FALSE(isTrainWaiting(enumValue(head->id)));
    EXPECT_FALSE(isInPathMode(enumValue(head->id)));
}

TEST_F(WaypointSignalFixture, placementMotionDoesNotCreatePhysicalReservationWaits)
{
    auto* head = vehicle();
    auto* other = vehicle();
    approaching(*head);
    signal(SignalKind::path);
    ASSERT_EQ(decide(*head).status, 0);
    auto* wagon = occupiedWagon(other->id);
    wagon->tileX = start.x;
    wagon->tileY = start.y;
    wagon->moveTo(start);
    auto* front = Vehicles::Vehicle(*head).veh2;
    front->subPosition = static_cast<uint16_t>(World::TrackData::getTrackSubPositon(0).size() - 1);
    front->remainingDistance = 0x368A;
    // placeDownVehicle uses this non-simulation motion after its own checks.
    front->updateTrackMotion(0, false);
    EXPECT_EQ(front->getTrackLoc(), start);
    EXPECT_FALSE(isTrainWaiting(enumValue(head->id)));
}

TEST_F(WaypointSignalFixture, realMovementSamplesAlwaysDetectForeignRailOccupation)
{
    auto* head = vehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    setPathMode(enumValue(head->id), true);
    World::Track::TrackConnections tc;
    tc.connections.push_back(0);
    for (uint16_t tad = 0; tad < 44 * 8; ++tad)
    {
        wagon->tileX = start.x;
        wagon->tileY = start.y;
        wagon->trackAndDirection.track._data = tad;
        const auto movement = World::TrackData::getTrackSubPositon(tad);
        for (uint16_t sub = 0; sub < movement.size(); ++sub)
        {
            wagon->subPosition = sub;
            wagon->moveTo(start + movement[sub].loc);
            EXPECT_FALSE(physicalEntryIsClear(*head, start, tad, tc)) << "tad=" << tad << " sub=" << sub;
        }
    }
}

TEST_F(WaypointSignalFixture, foreignCurveOccupationBlocksItsConflictingJunctionTile)
{
    auto* head = vehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    constexpr uint16_t curve = 11 << 3;
    addPiece(junction, curve);
    ASSERT_TRUE(piecesConflict(canonicalPiece(junction, 0), canonicalPiece(junction, curve)));
    wagon->tileX = junction.x;
    wagon->tileY = junction.y;
    wagon->trackAndDirection.track._data = curve;
    const auto movement = World::TrackData::getTrackSubPositon(curve);
    const auto remote = std::find_if(movement.begin(), movement.end(), [&](const auto& step) {
        const auto loc = junction + step.loc;
        return loc.x / 32 != junction.x / 32 || loc.y / 32 != junction.y / 32;
    });
    ASSERT_NE(remote, movement.end());
    wagon->moveTo(junction + remote->loc);
    setPathMode(enumValue(head->id), true);
    const auto tc = World::Track::getTrackConnections(junction, 0, head->owner, 0, 0, 0);
    EXPECT_FALSE(physicalEntryIsClear(*head, junction, 0, tc));
    EXPECT_FALSE(reserveFrom(*head, junction, tc, ReserveStart::extend));
    EXPECT_EQ(pathHoldCount(), 0U);
    approaching(*head);
    signal(SignalKind::path);
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        setSignalKind(facingSide(start, 0), kind);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
        expectRefused(*head);
        EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(start, 0)));
    }
    const auto line = Lines::createLine("Curve safety", TransportMode::rail, 4);
    ASSERT_TRUE(line.ok);
    Lines::LineOrder order;
    order.type = Vehicles::OrderType::RouteWaypoint;
    order.tileX = target.x / 32;
    order.tileY = target.y / 32;
    order.baseZ = 2;
    ASSERT_TRUE(Lines::addOrder(line.id, order).ok);
    ASSERT_TRUE(Lines::acceptStopKinds(line.id, Lines::LineKind::rail).ok);
    ASSERT_TRUE(Lines::assignVehicle(line.id, enumValue(head->id), TransportMode::rail).ok);
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        setSignalKind(facingSide(start, 0), kind);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
        expectRefused(*head);
        EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(start, 0)));
    }
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    EXPECT_EQ(decide(*head).status, 0);
    ASSERT_NE(pendingPath(enumValue(head->id)), nullptr);
    EXPECT_EQ(pendingPath(enumValue(head->id))->back().pos, target);
}

TEST_F(WaypointSignalFixture, remoteReverseCurveOccupationAlsoBlocksConflictingPiece)
{
    auto* head = vehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    constexpr uint16_t curve = 11 << 3;
    addPiece(junction, curve);
    const auto end = World::Track::getTrackConnectionEnd(junction, curve);
    const auto reverseEntrance = end.nextPos - World::Pos3{ World::kRotationOffset[end.nextRotation], 0 };
    const uint16_t reverse = curve ^ 4;
    ASSERT_EQ(canonicalPiece(reverseEntrance, reverse), canonicalPiece(junction, curve));
    wagon->tileX = reverseEntrance.x;
    wagon->tileY = reverseEntrance.y;
    wagon->trackAndDirection.track._data = reverse;
    const auto movement = World::TrackData::getTrackSubPositon(reverse);
    const auto remote = std::find_if(movement.begin(), movement.end(), [&](const auto& step) {
        const auto loc = reverseEntrance + step.loc;
        return loc.x / 32 != junction.x / 32 || loc.y / 32 != junction.y / 32;
    });
    ASSERT_NE(remote, movement.end());
    wagon->moveTo(reverseEntrance + remote->loc);
    setPathMode(enumValue(head->id), true);
    World::Track::TrackConnections tc;
    tc.connections.push_back(0);
    EXPECT_FALSE(physicalEntryIsClear(*head, junction, 0, tc));
}

TEST_F(WaypointSignalFixture, expandedOccupationKeepsSelfGhostsAndSeparatedTracksClear)
{
    auto* head = vehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    setPathMode(enumValue(head->id), true);
    World::Track::TrackConnections tc;
    tc.connections.push_back(0);
    EXPECT_FALSE(physicalEntryIsClear(*head, blocked, 0, tc));
    wagon->head = head->id;
    EXPECT_TRUE(physicalEntryIsClear(*head, blocked, 0, tc));
    wagon->head = other->id;
    wagon->var_38 |= Vehicles::Flags38::isGhost;
    EXPECT_TRUE(physicalEntryIsClear(*head, blocked, 0, tc));
    wagon->var_38 &= ~Vehicles::Flags38::isGhost;
    wagon->tileBaseZ = 20;
    wagon->moveTo(blocked + World::Pos3{ 0, 0, 64 });
    EXPECT_TRUE(physicalEntryIsClear(*head, blocked, 0, tc));
    wagon->tileBaseZ = 4;
    wagon->tileY += 32;
    wagon->moveTo(blocked + World::Pos3{ 0, 32, 0 });
    EXPECT_TRUE(physicalEntryIsClear(*head, blocked, 0, tc));
}

TEST_F(WaypointSignalFixture, physicalSafetySurvivesUnprotectedOrdinarySignalHandoff)
{
    auto* head = vehicle();
    auto* other = vehicle();
    approaching(*head);
    signal(SignalKind::none);
    signalAt(target, 0, SignalKind::path);
    setPathMode(enumValue(head->id), true);
    ASSERT_EQ(decide(*head).status, 0);
    ASSERT_FALSE(isInPathMode(enumValue(head->id)));
    ASSERT_TRUE(holdAt(canonicalPiece(start, 0)));
    ASSERT_FALSE(holdAt(canonicalPiece(start, 0))->isProtected);
    auto* wagon = occupiedWagon(other->id);
    wagon->tileX = start.x;
    wagon->tileY = start.y;
    wagon->moveTo(start);
    auto train = Vehicles::Vehicle(*head);
    auto ahead = train.veh2->routingHandle;
    ahead.setIndex(ahead.getIndex() + 1);
    train.veh1->routingHandle = ahead;
    auto* front = train.veh2;
    const auto before = front->getTrackLoc();
    front->subPosition = static_cast<uint16_t>(World::TrackData::getTrackSubPositon(0).size() - 1);
    front->remainingDistance = 0;
    EXPECT_TRUE(front->updateTrackMotion(0x368A, true).hasFlags(Vehicles::UpdateVar1136114Flags::unk_m00));
    EXPECT_EQ(front->getTrackLoc(), before);
}

TEST_F(WaypointSignalFixture, nativeSpecialSignalCannotPassOccupiedRouteWithPassOverride)
{
    auto* head = vehicle();
    auto* other = vehicle();
    occupiedWagon(other->id);
    approaching(*head);
    signal(SignalKind::path);
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        setSignalKind(facingSide(start, 0), kind);
        head->var_5C = 2;
        Vehicles::Vehicle(*head).veh1->var_48 |= Vehicles::Flags48::passSignal;
        expectRefused(*head);
        EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(start, 0)));
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    }
}

TEST_F(WaypointSignalFixture, nativeJunctionCannotBypassBlockedWaypointWithPassOverride)
{
    auto* head = vehicle();
    auto* other = vehicle();
    occupiedWagon(other->id);
    Vehicles::RoutingManager::setRouting(head->routingHandle, 0);
    signalAt(junction, 0, SignalKind::path);
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        setSignalKind(facingSide(junction, 0), kind);
        head->var_5C = 2;
        expectRefused(*head);
        EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(junction, 0)));
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    }
}

TEST_F(WaypointSignalFixture, nativePendingReservationRechecksWagonBeforePassingSignal)
{
    auto* head = vehicle();
    auto* other = vehicle();
    approaching(*head);
    const auto tc = signal(SignalKind::path);
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    auto* wagon = occupiedWagon(other->id);
    expectRefused(*head);
    EXPECT_EQ(pathHoldCount(), 0U);
    EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(start, 0)));
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    ScenarioManager::setScenarioTicks(16);
    const auto resumed = decide(*head);
    EXPECT_EQ(resumed.status, 0);
    EXPECT_FALSE(isTrainWaiting(enumValue(head->id)));
    auto next = head->routingHandle;
    next.setIndex(next.getIndex() + 1);
    EXPECT_EQ(Vehicles::RoutingManager::getRouting(next) & World::Track::AdditionalTaDFlags::basicTaDMask, 0U);
}

TEST_F(WaypointSignalFixture, nativeFreshBlockedRouteStopsPhysicalFrontForEverySignalKind)
{
    auto* head = vehicle();
    auto* other = vehicle();
    occupiedWagon(other->id);
    approaching(*head);
    signal(SignalKind::path);
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        setSignalKind(facingSide(start, 0), kind);
        ASSERT_EQ(head->var_5C, 0);
        expectRefused(*head);
        EXPECT_EQ(postFrameForDisplay(kind, isSignalBlockedForDisplay(facingSide(start, 0)), 3, 7), 0);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    }
}

TEST_F(WaypointSignalFixture, nativeSharedLineWaitsWithOverrideAndResumesRequiredWaypoint)
{
    auto* head = vehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    const auto line = Lines::createLine("Required waypoint", TransportMode::rail, 4);
    ASSERT_TRUE(line.ok);
    Lines::LineOrder order;
    order.type = Vehicles::OrderType::RouteWaypoint;
    order.tileX = target.x / 32;
    order.tileY = target.y / 32;
    order.baseZ = 2;
    ASSERT_TRUE(Lines::addOrder(line.id, order).ok);
    ASSERT_TRUE(Lines::acceptStopKinds(line.id, Lines::LineKind::rail).ok);
    ASSERT_TRUE(Lines::assignVehicle(line.id, enumValue(head->id), TransportMode::rail).ok);
    approaching(*head);
    signal(SignalKind::path | SignalKind::chain);
    head->var_5C = 2;
    expectRefused(*head);
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    ScenarioManager::setScenarioTicks(16);
    EXPECT_EQ(decide(*head).status, 0);
    ASSERT_NE(pendingPath(enumValue(head->id)), nullptr);
    EXPECT_EQ(pendingPath(enumValue(head->id))->back().pos, target);
    EXPECT_FALSE(isTrainWaiting(enumValue(head->id)));
}

TEST_F(WaypointSignalFixture, pendingRevalidationAllowsOwnWagonsAndKeepsOccupiedHoldsOnRefusal)
{
    auto* head = vehicle();
    auto* other = vehicle();
    approaching(*head);
    occupiedWagon(head->id);
    const auto tc = signal(SignalKind::chain);
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::chainSignal));
    const auto occupied = canonicalPiece(head->getTrackLoc(), 0);
    holdOccupied(enumValue(head->id), occupied, true);
    ASSERT_TRUE(validatePendingPath(*head, start, tc));
    ASSERT_GT(pathHoldCount(), 0U);
    occupiedWagon(other->id);
    EXPECT_FALSE(validatePendingPath(*head, start, tc));
    EXPECT_EQ(pathHoldCount(), 0U);
    ASSERT_TRUE(holdAt(occupied));
    EXPECT_EQ(holdAt(occupied)->kind, HoldKind::occupied);
    EXPECT_EQ(holdAt(occupied)->owner, enumValue(head->id));
    EXPECT_TRUE(isInPathMode(enumValue(head->id)));
}

TEST_F(WaypointSignalFixture, blockedWaypointPostIsRedBeforeRoutingHeadReachesIt)
{
    auto* head = vehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    approaching(*head);
    signal(SignalKind::path);
    const auto side = facingSide(start, 0);
    refreshApproachSignals(*head);
    EXPECT_TRUE(isSignalBlockedForDisplay(side));
    EXPECT_FALSE(isTrainWaiting(enumValue(head->id)));
    EXPECT_EQ(postFrameForDisplay(SignalKind::path, isSignalBlockedForDisplay(side), 6, 7), 0);
    EXPECT_EQ(pathHoldCount(), 0U);
    EXPECT_EQ(head->currentOrder, 0);
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    ScenarioManager::setScenarioTicks(16);
    refreshApproachSignals(*head);
    EXPECT_FALSE(isSignalBlockedForDisplay(side));
    EXPECT_EQ(postFrameForDisplay(SignalKind::path, isSignalBlockedForDisplay(side), 6, 7), 6);
    EXPECT_EQ(pathHoldCount(), 0U);
}

TEST_F(WaypointSignalFixture, junctionFreeWrongExitCannotMakeRequiredBlockedExitYellow)
{
    auto* head = vehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    Vehicles::RoutingManager::setRouting(head->routingHandle, 0);
    signalAt(junction, 0, SignalKind::path);
    const auto tc = World::Track::getTrackConnections(junction, World::TrackData::getUnkTrack(0).rotationBegin, head->owner, 0, 0, 0);
    ASSERT_EQ(tc.connections.size(), 2U);
    const auto side = facingSide(junction, 0);
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        clearWaitingSignals(enumValue(head->id));
        clearApproachingSignals(enumValue(head->id));
        setSignalKind(side, kind);
        refreshApproachSignals(*head);
        EXPECT_TRUE(isSignalBlockedForDisplay(side));
        EXPECT_EQ(postFrameForDisplay(kind, isSignalBlockedForDisplay(side), 3, 7), 0);
        EXPECT_FALSE(reserveFrom(*head, junction, tc, isChainSignal(kind) ? ReserveStart::chainSignal : ReserveStart::pathSignal));
        EXPECT_TRUE(isSignalWaiting(side));
        EXPECT_EQ(postFrameForDisplay(kind, isSignalBlockedForDisplay(side), 3, 7), 0);
        EXPECT_EQ(head->currentOrder, 0);
        EXPECT_EQ(pathHoldCount(), 0U);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    }
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    ASSERT_TRUE(reserveFrom(*head, junction, tc, ReserveStart::chainSignal));
    EXPECT_EQ(pendingPath(enumValue(head->id))->front().connection & World::Track::AdditionalTaDFlags::basicTaDMask, 0);
    refreshApproachSignals(*head);
    EXPECT_FALSE(isSignalBlockedForDisplay(side));
    EXPECT_EQ(postFrameForDisplay(SignalKind::path, isSignalBlockedForDisplay(side), 6, 7), 6);
}

TEST(PathSignalTests, blockedPostUsesNativeRestingDangerFrameForEverySignalFamily)
{
    // Read-only inspection of SIGC3/SIGC4/semaphore art establishes frame 0
    // as red. Frame 3 is yellow on SIGC3, not red. No original art is bundled.
    const World::SignalElement::Side nativeNewPost{};
    ASSERT_EQ(nativeNewPost.frame(), 0);
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        for (uint8_t count : { 4, 7, 10 })
        {
            for (uint8_t frame = 0; frame < count; ++frame)
            {
                EXPECT_EQ(postFrameForDisplay(kind, true, frame, count), nativeNewPost.frame());
                EXPECT_EQ(postFrameForDisplay(kind, false, frame, count), frame);
                EXPECT_EQ(postFrameForDisplay(SignalKind::none, true, frame, count), frame);
            }
        }
    }
}

TEST(PathSignalTests, logicalColoursMapToNativeRedYellowAndLastProceedState)
{
    for (uint8_t count : { 2, 3, 4 })
    {
        EXPECT_EQ(static_cast<uint8_t>(postStateFor(DisplayState::red, count)), 0);
        EXPECT_EQ(static_cast<uint8_t>(postStateFor(DisplayState::yellow, count)), 1);
        EXPECT_EQ(static_cast<uint8_t>(postStateFor(DisplayState::green, count)), count - 1);
    }
}

TEST_F(WaypointSignalFixture, sharedApproachStaysRedThroughWaitAndPlannerProgress)
{
    auto* head = vehicle();
    auto* other = vehicle();
    occupiedWagon(other->id);
    const auto line = Lines::createLine("Approaching waypoint", TransportMode::rail, 4);
    ASSERT_TRUE(line.ok);
    Lines::LineOrder waypoint{};
    waypoint.type = Vehicles::OrderType::RouteWaypoint;
    waypoint.tileX = target.x / 32;
    waypoint.tileY = target.y / 32;
    waypoint.baseZ = 2;
    Lines::LineOrder back = waypoint;
    back.tileX = start.x / 32;
    back.tileY = start.y / 32;
    ASSERT_TRUE(Lines::replaceOrders(line.id, { waypoint, back }).ok);
    ASSERT_TRUE(Lines::acceptStopKinds(line.id, Lines::LineKind::rail).ok);
    ASSERT_TRUE(Lines::assignVehicle(line.id, enumValue(head->id), TransportMode::rail).ok);
    approaching(*head);
    const auto tc = signal(SignalKind::chain);
    const auto side = facingSide(start, 0);
    for (auto kind : { SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        clearApproachingSignals(enumValue(head->id));
        setSignalKind(side, kind);
        refreshApproachSignals(*head);
        EXPECT_TRUE(isSignalBlockedForDisplay(side));
        EXPECT_FALSE(reserveFrom(*head, start, tc, ReserveStart::chainSignal));
        EXPECT_TRUE(isSignalWaiting(side));
        onHeadEntersPiece(*head, head->getTrackLoc(), 0);
        EXPECT_FALSE(isSignalWaiting(side));
        EXPECT_TRUE(isSignalBlockedForDisplay(side)); // The approach remains blocked.
        EXPECT_EQ(postFrameForDisplay(kind, isSignalBlockedForDisplay(side), 6, 7), 0);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    }
    EXPECT_EQ(head->currentOrder, 0);
    EXPECT_EQ(pathHoldCount(), 0U);
}

TEST_F(WaypointSignalFixture, approachQueryExcludesSelfAndKeepsClearWaypointNativeColour)
{
    auto* head = vehicle();
    occupiedWagon(head->id);
    approaching(*head);
    signal(SignalKind::path);
    refreshApproachSignals(*head);
    const auto side = facingSide(start, 0);
    EXPECT_FALSE(isSignalBlockedForDisplay(side));
    EXPECT_EQ(postFrameForDisplay(SignalKind::path, isSignalBlockedForDisplay(side), 6, 7), 6);
    EXPECT_EQ(pathHoldCount(), 0U);
    EXPECT_EQ(pendingPath(enumValue(head->id)), nullptr); // No saved path-mode record is added.
}

TEST_F(WaypointSignalFixture, approachQueryStopsAtBlockBoundaryAndIgnoresGhosts)
{
    auto* head = vehicle();
    auto* other = vehicle();
    occupiedWagon(other->id);
    approaching(*head);
    signal(SignalKind::none);
    signalAt(junction, 0, SignalKind::chain);
    refreshApproachSignals(*head);
    EXPECT_FALSE(isSignalBlockedForDisplay(facingSide(start, 0)));
    EXPECT_FALSE(isSignalBlockedForDisplay(facingSide(junction, 0)));
    setSignalKind(facingSide(start, 0), SignalKind::path);
    head->var_38 |= Vehicles::Flags38::isGhost;
    refreshApproachSignals(*head);
    EXPECT_FALSE(isSignalBlockedForDisplay(facingSide(start, 0)));
}

TEST(PathSignalTests, approachingAndWaitingOwnersKeepOneRedAspectWithoutExtraRedraws)
{
    clearAll();
    static int dirty;
    dirty = 0;
    setReservationViewDirty(+[](const PieceKey&) { ++dirty; });
    const SideKey side{ PieceKey{ 320, 320, 16, 0, 0 }, 0 };
    setSignalKind(side, SignalKind::path);
    EXPECT_TRUE(beginApproachProbe(1, 0));
    EXPECT_FALSE(beginApproachProbe(1, 15));
    EXPECT_TRUE(beginApproachProbe(1, 16));
    setApproachingSignals(1, { side });
    EXPECT_EQ(dirty, 1);
    EXPECT_FALSE(isTrainWaiting(1));
    setWaitingSignals(1, { side });
    setApproachingSignals(2, { side });
    clearWaitingSignals(1);
    clearApproachingSignals(1);
    EXPECT_EQ(dirty, 1);
    EXPECT_TRUE(isSignalBlockedForDisplay(side));
    forgetTrain(2);
    EXPECT_EQ(dirty, 2);
    EXPECT_FALSE(isSignalBlockedForDisplay(side));
    EXPECT_TRUE(beginApproachProbe(1, 16)); // Clearing/reusing an owner resets its throttle.
    setApproachingSignals(1, { side });
    clearAll();
    EXPECT_FALSE(isSignalBlockedForDisplay(side));
    EXPECT_TRUE(beginApproachProbe(1, 0));
    setReservationViewDirty(nullptr);
    clearAll();
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
    approaching(*head);
    refreshApproachSignals(*head);
    EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(start, 0)));
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
    post->getLeft().setFrame(6); // Native proceed frame after the planner passes a later signal.
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
        EXPECT_EQ(postFrameForDisplay(kind, isSignalWaiting(side), post->getLeft().frame(), 7), 0);
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
    EXPECT_TRUE(reservationStartAtJunction(*head, junction, signalled));
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
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 2, 8 })); // Protect through to the next signal, past the waypoint.
}

TEST(PathSignalTests, waypointBeforeBlockedTrackCannotShortenReservation)
{
    for (auto mode : { SearchMode::pathSignal, SearchMode::chainSignal, SearchMode::extend })
    {
        TestGraph graph;
        graph.add(1, 2).isTarget = true;
        auto& occupied = graph.add(2, 3);
        occupied.blocked = true;
        graph.add(3, 4);
        SearchRequest request;
        request.starts.push_back(graph.start(1));
        request.mode = mode;
        request.hasTarget = request.requireTarget = true;
        EXPECT_FALSE(findReservation(graph, request).found);
        occupied.blocked = false;
        const auto result = findReservation(graph, request);
        ASSERT_TRUE(result.found);
        EXPECT_TRUE(result.reachedTarget);
        EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 3, 4 }));
    }
}

TEST(PathSignalTests, waypointDoesNotSkipChainExitBlockOccupation)
{
    TestGraph graph;
    graph.add(1, 2).isTarget = true;
    auto& exit = graph.add(2, 3);
    exit.signal = EntrySignal::block;
    exit.exitOccupied = true;
    graph.add(3, 4);
    graph.add(4, 5).signal = EntrySignal::path;
    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.mode = SearchMode::chainSignal;
    request.hasTarget = request.requireTarget = true;
    EXPECT_FALSE(findReservation(graph, request).found);
    exit.exitOccupied = false;
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 3, 4 }));
}

TEST(PathSignalTests, waypointProtectionStillEndsBeforeTheNextSafeSignal)
{
    TestGraph graph;
    graph.add(1, 2).isTarget = true;
    graph.add(2, 3);
    auto& nextSignal = graph.add(3, 4);
    nextSignal.signal = EntrySignal::path;
    nextSignal.blocked = true;
    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = request.requireTarget = true;
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 3 }));
    request.mode = SearchMode::chainSignal;
    EXPECT_FALSE(findReservation(graph, request).found); // A chain must protect through its exit.
}

TEST(PathSignalTests, waypointProtectionAfterTargetKeepsReservationAndSearchBounds)
{
    TestGraph graph;
    graph.add(1, 2).isTarget = true;
    for (int16_t i = 2; i < 100; ++i)
    {
        graph.add(i, i + 1);
    }
    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = request.requireTarget = true;
    request.maxExpansions = 2;
    EXPECT_FALSE(findReservation(graph, request).found);
    request.maxExpansions = 4096;
    const auto result = findReservation(graph, request);
    EXPECT_FALSE(result.found);
    EXPECT_FALSE(result.safeEndpoint);
}

TEST(PathSignalTests, stationTargetCannotEndProtectionAtItsPlatform)
{
    TestGraph graph;
    graph.add(1, 2).isTarget = true;
    graph.add(2, 3).blocked = true;
    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = true;
    EXPECT_FALSE(findReservation(graph, request).found);
    request.requireTarget = true;
    EXPECT_FALSE(findReservation(graph, request).found);
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
        auto& edge = graph.add(i, i + 1);
        edge.isTarget = i == 79;
        if (i == 64)
        {
            edge.signal = EntrySignal::path; // A real boundary, rather than the reservation cap.
        }
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
        EXPECT_EQ(postFrameForDisplay(kind, true, 0, 4), 0);
        EXPECT_EQ(postFrameForDisplay(kind, true, 6, 7), 0);
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
    EXPECT_EQ(postStateFor(DisplayState::yellow, 2), 1);
    EXPECT_EQ(postStateFor(DisplayState::yellow, 3), 1);
    EXPECT_EQ(postStateFor(DisplayState::red, 2), 0);
    EXPECT_EQ(postStateFor(DisplayState::green, 4), 3);
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
        EXPECT_EQ(version, 4);
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

namespace
{
    struct StationSignalFixture : WaypointSignalFixture
    {
        Station previous{};
        const StationId stationId = static_cast<StationId>(10);

        void SetUp() override
        {
            WaypointSignalFixture::SetUp();
            previous = *StationManager::get(stationId);
            auto* record = StationManager::get(stationId);
            *record = {};
            record->name = 1;
            record->owner = static_cast<CompanyId>(0);
            record->flags = StationFlags::transportModeRail;
            record->x = junction.x;
            record->y = junction.y;
            record->z = junction.z;
            record->stationTileSize = 1;
            record->stationTiles[0] = junction;
            for (auto& element : World::TileManager::get(junction))
            {
                auto* track = element.as<World::TrackElement>();
                if (!track || track->trackId() != 0)
                {
                    continue;
                }
                track->setHasStationElement(true);
                auto* platform = World::TileManager::insertElementAfterNoReorg(&element, World::ElementType::station, { junction.x, junction.y }, 4, 0)->as<World::StationElement>();
                platform->setStationId(stationId);
                platform->setStationType(StationType::trainStation);
                break;
            }
        }

        void TearDown() override
        {
            *StationManager::get(stationId) = previous;
            WaypointSignalFixture::TearDown();
        }

        Vehicles::VehicleHead* stationVehicle()
        {
            auto* head = vehicle();
            Vehicles::OrderStopAt order(stationId);
            Vehicles::OrderManager::insertOrder(head, 0, &order);
            head->currentOrder = 0;
            return head;
        }

        void atPlatform(Vehicles::VehicleHead& head, bool committedExit = false)
        {
            Vehicles::Vehicle train(head);
            train.applyToComponents([&](auto& component) {
                component.tileX = junction.x;
                component.tileY = junction.y;
                component.tileBaseZ = 4;
                component.trackAndDirection.track._data = 0;
                component.moveTo(junction);
            });
            Vehicles::RoutingManager::setRouting(head.routingHandle, 0);
            head.currentOrder = sizeof(Vehicles::OrderStopAt);
            head.stationId = stationId;
            head.status = Vehicles::Status::loading;
            if (committedExit)
            {
                head.routingHandle.setIndex(head.routingHandle.getIndex() + 1);
                Vehicles::RoutingManager::setRouting(head.routingHandle, 0);
                head.tileX = blocked.x;
                head.tileY = blocked.y;
            }
            rebuildTrainHolds(head);
        }
    };
}

TEST_F(StationSignalFixture, allSpecialSignalsRefuseBlockedStationExitAndResumeWhenClear)
{
    auto* head = stationVehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    approaching(*head);
    const auto tc = signal(SignalKind::path);
    for (const auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        setSignalKind(facingSide(start, 0), kind);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
        refreshApproachSignals(*head);
        EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(start, 0)));
        expectRefused(*head);
        EXPECT_FALSE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    }
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::chainSignal));
    ASSERT_NE(pendingPath(enumValue(head->id)), nullptr);
    EXPECT_EQ(pendingPath(enumValue(head->id))->back().pos, target);
    EXPECT_EQ(head->currentOrder, 0U);
    EXPECT_FALSE(isSignalBlockedForDisplay(facingSide(start, 0)));
}

TEST_F(StationSignalFixture, freeStationReservesItsExitAndReleasesOnlyAfterTail)
{
    auto* head = stationVehicle();
    const auto owner = enumValue(head->id);
    const auto tc = signal(SignalKind::path);
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    EXPECT_EQ(pendingPath(owner)->size(), 4U);
    onHeadEntersPiece(*head, start, tc.connections.front());
    onHeadEntersPiece(*head, junction, 0);
    ASSERT_TRUE(holdAt(canonicalPiece(junction, 0)));
    EXPECT_TRUE(holdAt(canonicalPiece(junction, 0))->isProtected);
    dropPendingPath(owner);
    EXPECT_TRUE(holdAt(canonicalPiece(junction, 0)));
    onTailLeavesPiece(owner, junction, 0);
    EXPECT_FALSE(holdAt(canonicalPiece(junction, 0)));
}

TEST_F(StationSignalFixture, platformDepartureChecksStationaryWagonWithoutSavedPathMode)
{
    auto* head = stationVehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    signal(SignalKind::path);
    atPlatform(*head);
    const auto owner = enumValue(head->id);
    ASSERT_FALSE(isInPathMode(owner));
    const auto order = head->currentOrder;
    EXPECT_FALSE(ensureStationDeparture(*head));
    EXPECT_TRUE(isStationDepartureWaiting(owner));
    EXPECT_EQ(head->currentOrder, order);
    EXPECT_EQ(head->status, Vehicles::Status::loading);
    const auto handle = Vehicles::Vehicle(*head).veh2->routingHandle;
    ASSERT_TRUE(Vehicles::Vehicle(*head).veh1->updateRail());
    ASSERT_TRUE(Vehicles::Vehicle(*head).veh2->update());
    EXPECT_EQ(Vehicles::Vehicle(*head).veh2->routingHandle, handle);
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    EXPECT_FALSE(ensureStationDeparture(*head)); // Fixed retry interval.
    ScenarioManager::setScenarioTicks(16);
    ASSERT_TRUE(ensureStationDeparture(*head));
    EXPECT_FALSE(isStationDepartureWaiting(owner));
    EXPECT_TRUE(isInPathMode(owner));
    EXPECT_TRUE(holdAt(canonicalPiece(junction, 0))->isProtected);
    EXPECT_EQ(pendingPath(owner)->back().pos, target);
}

TEST_F(StationSignalFixture, departureRevalidatesAlreadyCommittedExitPieces)
{
    auto* head = stationVehicle();
    auto* other = vehicle();
    signal(SignalKind::chain);
    atPlatform(*head, true);
    occupiedWagon(other->id);
    EXPECT_FALSE(ensureStationDeparture(*head));
    EXPECT_TRUE(isStationDepartureWaiting(enumValue(head->id)));
    EXPECT_TRUE(holdAt(canonicalPiece(junction, 0)));
    EXPECT_TRUE(holdAt(canonicalPiece(blocked, 0)));
}

TEST_F(StationSignalFixture, foreignReservationBlocksDepartureAndManualPassCannotBypass)
{
    auto* head = stationVehicle();
    auto* other = vehicle();
    signal(SignalKind::path | SignalKind::chain);
    atPlatform(*head);
    ASSERT_TRUE(reservePath(enumValue(other->id), { { blocked, 0 }, { target, 0 } }));
    head->vehicleFlags |= Vehicles::VehicleFlags::manualControl;
    Vehicles::Vehicle(*head).veh1->var_48 |= Vehicles::Flags48::passSignal;
    EXPECT_FALSE(ensureStationDeparture(*head));
    EXPECT_TRUE(isStationDepartureWaiting(enumValue(head->id)));
    EXPECT_EQ(holdAt(canonicalPiece(blocked, 0))->owner, enumValue(other->id));
}

TEST_F(StationSignalFixture, unrelatedPathPostBeyondBlockBoundaryKeepsClassicStationDeparture)
{
    auto* head = stationVehicle();
    signal(SignalKind::none);
    const auto beyond = World::Track::getTrackConnectionEnd(target, 0).nextPos;
    addPiece(beyond, 0);
    signalAt(target, 0, SignalKind::none);
    signalAt(beyond, 0, SignalKind::path);
    atPlatform(*head);
    EXPECT_TRUE(ensureStationDeparture(*head));
    EXPECT_FALSE(isInPathMode(enumValue(head->id)));
    EXPECT_EQ(pendingPath(enumValue(head->id)), nullptr);
}

TEST_F(StationSignalFixture, ownWagonDoesNotBlockPlatformDeparture)
{
    auto* head = stationVehicle();
    signal(SignalKind::path);
    atPlatform(*head);
    occupiedWagon(head->id);
    EXPECT_TRUE(ensureStationDeparture(*head));
}

TEST_F(StationSignalFixture, sharedStationOrdersProtectTheSameExit)
{
    auto* head = stationVehicle();
    auto* other = vehicle();
    occupiedWagon(other->id);
    const auto line = Lines::createLine("Station protection", TransportMode::rail, 4);
    ASSERT_TRUE(line.ok);
    Lines::LineOrder stop;
    stop.type = Vehicles::OrderType::StopAt;
    stop.station = enumValue(stationId);
    ASSERT_TRUE(Lines::addOrder(line.id, stop).ok);
    Lines::LineOrder waypoint;
    waypoint.type = Vehicles::OrderType::RouteWaypoint;
    waypoint.tileX = target.x / 32;
    waypoint.tileY = target.y / 32;
    waypoint.baseZ = 2;
    ASSERT_TRUE(Lines::addOrder(line.id, waypoint).ok);
    ASSERT_TRUE(Lines::acceptStopKinds(line.id, Lines::LineKind::rail).ok);
    ASSERT_TRUE(Lines::assignVehicle(line.id, enumValue(head->id), TransportMode::rail).ok);
    const auto tc = signal(SignalKind::chain);
    EXPECT_FALSE(reserveFrom(*head, start, tc, ReserveStart::chainSignal));
    EXPECT_EQ(head->currentOrder, 0U);
}

TEST_F(StationSignalFixture, saveReloadRebuildsTheStationDepartureGate)
{
    auto* head = stationVehicle();
    auto* other = vehicle();
    signal(SignalKind::path);
    atPlatform(*head);
    ASSERT_TRUE(ensureStationDeparture(*head));
    const auto path = std::filesystem::temp_directory_path() / "openloco-station-reservation.SV5";
    ASSERT_TRUE(saveCompanion(path));
    occupiedWagon(other->id);
    clearAll();
    ASSERT_TRUE(loadCompanion(path));
    rebuildTrainHolds(*head);
    EXPECT_FALSE(isStationDepartureWaiting(enumValue(head->id)));
    EXPECT_FALSE(ensureStationDeparture(*head));
    EXPECT_TRUE(isStationDepartureWaiting(enumValue(head->id)));
    std::filesystem::remove(companionPath(path));
}

TEST(PathSignalTests, exhaustedReservationLimitNeverBecomesSafeEndpoint)
{
    TestGraph graph;
    graph.add(1, 2).isTarget = true;
    graph.add(2, 3);
    graph.add(3, 4);
    graph.add(4, 5).signal = EntrySignal::path;
    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = request.requireTarget = true;
    request.maxReservedPieces = 3;
    EXPECT_FALSE(findReservation(graph, request).safeEndpoint);
    request.maxReservedPieces = 4;
    auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_TRUE(result.safeEndpoint);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 3, 4 }));
    request.maxExpansions = 1;
    EXPECT_FALSE(findReservation(graph, request).found);
}

TEST(PathSignalTests, failedReplacementPreservesPendingAndOccupiedProtection)
{
    clearAll();
    setHoldValidator(nullptr);
    const auto occupied = straight(320, 320, 0);
    const auto pending = straight(352, 320, 0);
    const auto foreign = straight(384, 320, 0);
    holdOccupied(1, canonicalPiece(occupied.pos, occupied.connection), true);
    ASSERT_TRUE(reservePath(1, { pending }));
    ASSERT_TRUE(reservePath(2, { foreign }));
    EXPECT_FALSE(reservePath(1, { pending, foreign }));
    ASSERT_EQ(pendingPath(1)->size(), 1U);
    EXPECT_EQ(pendingPath(1)->front().pos, pending.pos);
    EXPECT_EQ(holdAt(canonicalPiece(pending.pos, 0))->owner, 1U);
    EXPECT_TRUE(holdAt(canonicalPiece(occupied.pos, 0))->isProtected);
    ASSERT_TRUE(reservePath(1, { occupied }));
    EXPECT_FALSE(holdAt(canonicalPiece(pending.pos, 0)));
    EXPECT_EQ(holdAt(canonicalPiece(occupied.pos, 0))->kind, HoldKind::occupied);
    clearAll();
}

TEST(PathSignalTests, transientDepartureWaitClearsWithOwnerAndWorld)
{
    clearAll();
    setStationDepartureWaiting(1, true);
    EXPECT_TRUE(isStationDepartureWaiting(1));
    forgetTrain(1);
    EXPECT_FALSE(isStationDepartureWaiting(1));
    setStationDepartureWaiting(1, true);
    clearAll();
    EXPECT_FALSE(isStationDepartureWaiting(1));
}

TEST_F(StationSignalFixture, nextWaypointBeyondPlatformStillRequiresItsExactDirection)
{
    auto* head = stationVehicle();
    auto orders = head->getCurrentOrders().begin();
    ++orders;
    orders->as<Vehicles::OrderRouteWaypoint>()->setDirection(1);
    const auto tc = signal(SignalKind::path);
    EXPECT_FALSE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    EXPECT_EQ(head->currentOrder, 0U);
    EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(start, 0)));
}

TEST_F(StationSignalFixture, stationWithExitSignalReservesOnlyToThatSafePost)
{
    auto* head = stationVehicle();
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    wagon->tileX = target.x;
    wagon->tileY = target.y;
    wagon->moveTo(target);
    signalAt(blocked, 0, SignalKind::path);
    const auto tc = signal(SignalKind::path);
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    ASSERT_NE(pendingPath(enumValue(head->id)), nullptr);
    EXPECT_EQ(pendingPath(enumValue(head->id))->back().pos, junction);
    dropPendingPath(enumValue(head->id));
    ScenarioManager::setScenarioTicks(16);
    EXPECT_FALSE(reserveFrom(*head, start, tc, ReserveStart::chainSignal));
}

TEST_F(StationSignalFixture, stoppedParallelWagonDoesNotBlockStationDeparture)
{
    auto* head = stationVehicle();
    auto* other = vehicle();
    signal(SignalKind::path);
    atPlatform(*head);
    auto* wagon = occupiedWagon(other->id);
    const World::Pos3 parallel{ blocked.x, static_cast<coord_t>(blocked.y + 32), blocked.z };
    addPiece(parallel, 0);
    wagon->tileX = parallel.x;
    wagon->tileY = parallel.y;
    wagon->moveTo(parallel);
    EXPECT_TRUE(ensureStationDeparture(*head));
}

TEST_F(StationSignalFixture, stationDepartureRechecksEditedOrderWithoutDroppingPreviousSafeClaim)
{
    auto* head = stationVehicle();
    signal(SignalKind::path);
    atPlatform(*head);
    const auto owner = enumValue(head->id);
    ASSERT_TRUE(ensureStationDeparture(*head));
    const auto size = pendingPath(owner)->size();
    head->getCurrentOrders().begin()->as<Vehicles::OrderRouteWaypoint>()->setDirection(1);
    EXPECT_FALSE(ensureStationDeparture(*head));
    EXPECT_EQ(pendingPath(owner)->size(), size);
    EXPECT_TRUE(holdAt(canonicalPiece(junction, 0))->isProtected);
    EXPECT_TRUE(isStationDepartureWaiting(owner));
}

TEST_F(StationSignalFixture, conditionalOutgoingOrdersUseReadOnlyVehicleProjection)
{
    using namespace Rules;
    auto* head = stationVehicle();
    addPiece(blocked, 2 << 3);
    Vehicles::OrderRouteWaypoint alternative({ static_cast<coord_t>(blocked.x / 32), static_cast<coord_t>(blocked.y / 32) }, 2, 0, 2);
    Vehicles::OrderManager::insertOrder(head, head->sizeOfOrderTable - 1, &alternative);
    const auto owner = ownerForVehicle(enumValue(head->id));
    const auto p = snapshot(owner);
    ASSERT_EQ(p.rows.size(), 3U);
    Edit edit;
    edit.block.kind = Kind::ifBlock;
    edit.block.first = edit.block.last = p.rows[1].key;
    edit.block.condition.check = Check::always;
    edit.block.condition.compare = Compare::isTrue;
    ASSERT_TRUE(submit(owner, edit));
    const auto branch = find(owner)->blocks.front();
    edit.block = {};
    edit.block.kind = Kind::otherwise;
    edit.block.parent = branch.id;
    edit.block.first = edit.block.last = p.rows[2].key;
    ASSERT_TRUE(submit(owner, edit));
    signal(SignalKind::path);
    auto* other = vehicle();
    auto* wagon = occupiedWagon(other->id);
    wagon->tileX = target.x;
    wagon->tileY = target.y;
    wagon->moveTo(target);
    const auto before = *find(owner);
    const auto cursor = head->currentOrder;
    ASSERT_NE(runtimeForVehicle(enumValue(head->id)), nullptr);
    const auto previousRuntime = *runtimeForVehicle(enumValue(head->id));
    const auto tc = World::Track::getTrackConnections(start, 0, head->owner, 0, 0, 0);
    EXPECT_FALSE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    EXPECT_EQ(*find(owner), before);
    EXPECT_EQ(head->currentOrder, cursor);
    ASSERT_NE(runtimeForVehicle(enumValue(head->id)), nullptr);
    EXPECT_EQ(runtimeForVehicle(enumValue(head->id))->pc, previousRuntime.pc);
    EXPECT_EQ(runtimeForVehicle(enumValue(head->id))->branches, previousRuntime.branches);
    EXPECT_EQ(runtimeForVehicle(enumValue(head->id))->repeats, previousRuntime.repeats);
    EXPECT_EQ(runtimeForVehicle(enumValue(head->id))->current, previousRuntime.current);
    edit = {};
    edit.kind = EditKind::updateBlock;
    edit.block = branch;
    edit.block.condition.compare = Compare::isFalse;
    ASSERT_TRUE(submit(owner, edit));
    ScenarioManager::setScenarioTicks(16);
    const auto changed = *find(owner);
    const auto* live = runtimeForVehicle(enumValue(head->id));
    ASSERT_NE(live, nullptr);
    const auto runtime = *live;
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    EXPECT_EQ(pendingPath(enumValue(head->id))->back().connection & World::Track::AdditionalTaDFlags::basicTaDMask, 2U << 3);
    EXPECT_EQ(*find(owner), changed);
    live = runtimeForVehicle(enumValue(head->id));
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(live->pc, runtime.pc);
    EXPECT_EQ(live->current, runtime.current);
    EXPECT_EQ(live->repeats, runtime.repeats);
    EXPECT_EQ(live->branches, runtime.branches);
    EXPECT_EQ(live->prechecked, runtime.prechecked);
    EXPECT_EQ(live->budgetTick, runtime.budgetTick);
    EXPECT_EQ(live->steps, runtime.steps);
    EXPECT_EQ(live->waiting, runtime.waiting);
    EXPECT_EQ(live->waitStart, runtime.waitStart);
}

TEST_F(StationSignalFixture, nativeLoadingAndRestartBothHoldThePhysicalTrain)
{
    auto* head = stationVehicle();
    auto* other = vehicle();
    signal(SignalKind::path);
    atPlatform(*head, true);
    occupiedWagon(other->id);
    head->vehicleFlags |= Vehicles::VehicleFlags::manualControl;
    head->manualPower = 100;
    auto train = Vehicles::Vehicle(*head);
    train.veh2->sound.objectId = 0xFFFF;
    train.tail->sound.objectId = 0xFFFF;
    const auto location = train.veh2->getTrackLoc();
    const auto handle = train.veh2->routingHandle;
    const auto order = head->currentOrder;
    ASSERT_TRUE(head->update());
    EXPECT_EQ(head->status, Vehicles::Status::loading);
    EXPECT_EQ(head->currentOrder, order);
    EXPECT_TRUE(isStationDepartureWaiting(enumValue(head->id)));
    ASSERT_TRUE(train.veh1->updateRail());
    ASSERT_TRUE(train.veh2->update());
    EXPECT_EQ(train.veh2->getTrackLoc(), location);
    EXPECT_EQ(train.veh2->routingHandle, handle);
    head->status = Vehicles::Status::stopped;
    ScenarioManager::setScenarioTicks(16);
    ASSERT_TRUE(head->update());
    EXPECT_EQ(head->status, Vehicles::Status::waitingAtSignal);
    EXPECT_TRUE(isStationDepartureWaiting(enumValue(head->id)));
    ASSERT_TRUE(train.veh1->updateRail());
    ASSERT_TRUE(train.veh2->update());
    EXPECT_EQ(train.veh2->getTrackLoc(), location);
    EXPECT_EQ(train.veh2->routingHandle, handle);
}

TEST_F(StationSignalFixture, reversedStationFrontChecksItsNewUnsignalledExit)
{
    auto* head = stationVehicle();
    auto* other = vehicle();
    const auto before = start - World::TrackData::getUnkTrack(0).pos;
    addPiece(before, 0);
    signalAt(before, 0, SignalKind::path);
    for (auto& element : World::TileManager::get(before))
    {
        if (auto* post = element.as<World::SignalElement>())
        {
            post->getRight().setHasSignal(true);
        }
    }
    setSignalKind(facingSide(before, 4), SignalKind::path);
    atPlatform(*head);
    auto train = Vehicles::Vehicle(*head);
    train.applyToComponents([](auto& component) { component.trackAndDirection.track._data = 4; });
    Vehicles::RoutingManager::setRouting(head->routingHandle, 4);
    auto* order = head->getCurrentOrders().begin()->as<Vehicles::OrderRouteWaypoint>();
    ASSERT_NE(order, nullptr);
    order->setWaypoint({ static_cast<coord_t>(start.x / 32), static_cast<coord_t>(start.y / 32) }, 2);
    rebuildTrainHolds(*head);
    auto* wagon = occupiedWagon(other->id);
    wagon->tileX = start.x;
    wagon->tileY = start.y;
    wagon->moveTo(start);
    EXPECT_FALSE(ensureStationDeparture(*head));
    EXPECT_TRUE(isStationDepartureWaiting(enumValue(head->id)));
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    ScenarioManager::setScenarioTicks(16);
    EXPECT_TRUE(ensureStationDeparture(*head));
    EXPECT_FALSE(isStationDepartureWaiting(enumValue(head->id)));
    EXPECT_GT(specialSignalCount(), 0U);
    EXPECT_TRUE(isInPathMode(enumValue(head->id)));
    ASSERT_TRUE(holdAt(canonicalPiece(junction, 4)));
    EXPECT_EQ(holdAt(canonicalPiece(junction, 4))->owner, enumValue(head->id));
    EXPECT_TRUE(holdAt(canonicalPiece(junction, 4))->isProtected);
}

TEST_F(StationSignalFixture, terminalStationReversalRequiresAProvableReverseDeparture)
{
    auto* head = stationVehicle();
    auto* other = vehicle();
    const auto before = start - World::TrackData::getUnkTrack(0).pos;
    addPiece(before, 0);
    signalAt(before, 0, SignalKind::path);
    atPlatform(*head);
    for (const auto pos : { blocked, target })
    {
        for (auto& element : World::TileManager::get(pos))
        {
            const auto* track = element.as<World::TrackElement>();
            if (track && track->trackId() == 0)
            {
                World::TileManager::removeElement(element);
                break;
            }
        }
    }
    auto* order = head->getCurrentOrders().begin()->as<Vehicles::OrderRouteWaypoint>();
    ASSERT_NE(order, nullptr);
    order->setWaypoint({ static_cast<coord_t>(start.x / 32), static_cast<coord_t>(start.y / 32) }, 2);
    const auto cursor = head->currentOrder;
    EXPECT_TRUE(stationDepartureNeedsReverse(*head));
    EXPECT_EQ(head->currentOrder, cursor);
    EXPECT_EQ(head->trackAndDirection.track._data, 0U);
    auto* wagon = occupiedWagon(other->id);
    wagon->tileX = start.x;
    wagon->tileY = start.y;
    wagon->moveTo(start);
    EXPECT_FALSE(stationDepartureNeedsReverse(*head));
    EXPECT_EQ(head->currentOrder, cursor);
    EXPECT_EQ(head->trackAndDirection.track._data, 0U);
}

// Priority scheduling runs before any contender commits a planning-ring piece.
TEST_F(WaypointSignalFixture, expressPriorityIsIndependentOfVehicleListOrder)
{
    auto* local = vehicle();
    auto* express = vehicle();
    approaching(*local);
    approaching(*express);
    signal(SignalKind::path);
    Vehicles::Vehicle(*express).veh1->var_48 |= Vehicles::Flags48::expressMode;
    std::vector<Vehicles::VehicleHead*> order{ local, express };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), express);
    order = { express, local };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), express);
    ASSERT_EQ(decide(*express).status, 0);
    expectRefused(*local);
}

TEST_F(WaypointSignalFixture, equalPriorityPrefersLongestActualSignalWait)
{
    auto* first = vehicle();
    auto* second = vehicle();
    approaching(*first);
    approaching(*second);
    const auto tc = signal(SignalKind::chain);
    first->status = second->status = Vehicles::Status::waitingAtSignal;
    onPathWait(*first, start, tc);
    ScenarioManager::setScenarioTicks(40);
    onPathWait(*second, start, tc);
    std::vector<Vehicles::VehicleHead*> order{ second, first };
    for (bool express : { false, true })
    {
        if (express)
        {
            Vehicles::Vehicle(*first).veh1->var_48 |= Vehicles::Flags48::expressMode;
            Vehicles::Vehicle(*second).veh1->var_48 |= Vehicles::Flags48::expressMode;
        }
        prioritiseReservations(order);
        EXPECT_EQ(order.front(), first);
        order = { second, first };
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    }
}

TEST_F(WaypointSignalFixture, expressBeatsALongerWaitingLocal)
{
    auto* local = vehicle();
    auto* express = vehicle();
    approaching(*local);
    approaching(*express);
    const auto tc = signal(SignalKind::path | SignalKind::chain);
    onPathWait(*local, start, tc);
    ScenarioManager::setScenarioTicks(200);
    Vehicles::Vehicle(*express).veh1->var_48 |= Vehicles::Flags48::expressMode;
    std::vector<Vehicles::VehicleHead*> order{ local, express };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), express);
}

TEST_F(WaypointSignalFixture, waitingTrainCanTakeUnusedPathFromDistantEqualPriorityTrain)
{
    auto* distant = vehicle();
    auto* near = vehicle();
    approaching(*distant);
    approaching(*near);
    const auto tc = signal(SignalKind::path);
    ASSERT_TRUE(reserveFrom(*distant, start, tc, ReserveStart::pathSignal));
    // Put its physical front eight pieces behind its planning head. This is
    // unused lookahead, not a train already committed to the conflicting piece.
    auto far = distant->getTrackLoc();
    for (int i = 0; i < 8; ++i)
    {
        far -= World::TrackData::getUnkTrack(0).pos;
        addPiece(far, 0);
    }
    Vehicles::Vehicle(*distant).veh2->tileX = far.x;
    Vehicles::Vehicle(*distant).veh2->tileY = far.y;
    distant->routingHandle.setIndex(8);
    for (uint8_t i = 0; i <= 8; ++i)
    {
        auto handle = distant->routingHandle;
        handle.setIndex(i);
        Vehicles::RoutingManager::setRouting(handle, 0);
    }
    const auto before = distant->getTrackLoc();
    Vehicles::Vehicle(*distant).veh2->currentSpeed = 20.0_mph;
    near->status = Vehicles::Status::waitingAtSignal;
    onPathWait(*near, start, tc);
    std::vector<Vehicles::VehicleHead*> order{ distant, near };
    prioritiseReservations(order);
    ASSERT_EQ(order.front(), near);
    EXPECT_TRUE(reserveFrom(*near, start, tc, ReserveStart::pathSignal));
    ASSERT_NE(pendingPath(enumValue(distant->id)), nullptr);
    EXPECT_TRUE(pendingPath(enumValue(distant->id))->empty());
    EXPECT_EQ(distant->getTrackLoc(), before);
    EXPECT_EQ(holdAt(canonicalPiece(start, 0))->owner, enumValue(near->id));
}

TEST_F(WaypointSignalFixture, distantExpressKeepsPriorityOverWaitingLocal)
{
    auto* distant = vehicle();
    auto* near = vehicle();
    approaching(*distant);
    approaching(*near);
    const auto tc = signal(SignalKind::path);
    Vehicles::Vehicle(*distant).veh1->var_48 |= Vehicles::Flags48::expressMode;
    Vehicles::Vehicle(*distant).veh2->currentSpeed = 20.0_mph;
    ASSERT_TRUE(reserveFrom(*distant, start, tc, ReserveStart::pathSignal));
    near->status = Vehicles::Status::waitingAtSignal;
    onPathWait(*near, start, tc);
    std::vector<Vehicles::VehicleHead*> order{ near, distant };
    prioritiseReservations(order);
    ASSERT_EQ(order.front(), distant);
    EXPECT_FALSE(reserveFrom(*near, start, tc, ReserveStart::pathSignal));
    EXPECT_EQ(holdAt(canonicalPiece(start, 0))->owner, enumValue(distant->id));
}

TEST_F(WaypointSignalFixture, equalWaitPrefersNearestOrderedWaypoint)
{
    auto* further = vehicle();
    auto* closer = vehicle();
    approaching(*further);
    approaching(*closer);
    const auto tc = signal(SignalKind::path);
    closer->getCurrentOrders().begin()->as<Vehicles::OrderRouteWaypoint>()->setWaypoint({ static_cast<coord_t>(junction.x / 32), static_cast<coord_t>(junction.y / 32) }, 2);
    onPathWait(*further, start, tc);
    onPathWait(*closer, start, tc);
    std::vector<Vehicles::VehicleHead*> order{ further, closer };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), closer);
}

TEST_F(WaypointSignalFixture, exactConflictLotteryIsStableAcrossRetriesAndInputOrder)
{
    const auto saved = gPrng1();
    auto* first = vehicle();
    auto* second = vehicle();
    approaching(*first);
    approaching(*second);
    const auto tc = signal(SignalKind::chain);
    onPathWait(*first, start, tc);
    onPathWait(*second, start, tc);
    std::vector<Vehicles::VehicleHead*> order{ first, second };
    gPrng1() = Core::Prng(1234, 5678);
    prioritiseReservations(order);
    auto* winner = order.front();
    const auto drawn = gPrng1();
    for (int i = 0; i < 4; ++i)
    {
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
        order = { second, first };
        prioritiseReservations(order);
        EXPECT_EQ(order.front(), winner);
        EXPECT_EQ(gPrng1().srand_0(), drawn.srand_0());
        EXPECT_EQ(gPrng1().srand_1(), drawn.srand_1());
    }
    gPrng1() = saved;
}

TEST_F(WaypointSignalFixture, unrelatedServicesAndRoadVehiclesKeepTheirSlotsAndPrng)
{
    const auto saved = gPrng1();
    auto* first = vehicle();
    auto* road = vehicle();
    auto* unrelated = vehicle();
    approaching(*first);
    road->mode = TransportMode::road;
    Vehicles::Vehicle(*unrelated).veh1->var_48 |= Vehicles::Flags48::expressMode;
    unrelated->tileX = 1280;
    unrelated->tileY = 1280;
    signal(SignalKind::path);
    std::vector<Vehicles::VehicleHead*> order{ first, road, unrelated };
    const auto original = order;
    prioritiseReservations(order);
    EXPECT_EQ(order, original);
    EXPECT_EQ(gPrng1().srand_0(), saved.srand_0());
    EXPECT_EQ(gPrng1().srand_1(), saved.srand_1());
}

TEST_F(WaypointSignalFixture, ordinaryBlockConflictsUseExpressBeforeNativeAdmission)
{
    auto* first = vehicle();
    auto* second = vehicle();
    approaching(*first);
    approaching(*second);
    signal(SignalKind::none);
    Vehicles::Vehicle(*second).veh1->var_48 |= Vehicles::Flags48::expressMode;
    std::vector<Vehicles::VehicleHead*> order{ first, second };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), second);
}

TEST_F(WaypointSignalFixture, expressCannotTakeCommittedPlanningRingOrOccupiedWagons)
{
    auto* local = vehicle();
    auto* express = vehicle();
    approaching(*local);
    approaching(*express);
    const auto tc = signal(SignalKind::path);
    ASSERT_EQ(decide(*local).status, 0); // Commits the entrance to its ring.
    Vehicles::Vehicle(*express).veh1->var_48 |= Vehicles::Flags48::expressMode;
    onPathWait(*express, start, tc);
    std::vector<Vehicles::VehicleHead*> order{ local, express };
    prioritiseReservations(order);
    EXPECT_FALSE(reserveFrom(*express, start, tc, ReserveStart::pathSignal));
    const auto hold = holdAt(canonicalPiece(start, 0));
    ASSERT_TRUE(hold);
    EXPECT_EQ(hold->owner, enumValue(local->id));
    EXPECT_EQ(hold->kind, HoldKind::occupied);
    auto* wagon = occupiedWagon(local->id);
    ScenarioManager::setScenarioTicks(16);
    EXPECT_FALSE(reserveFrom(*express, start, tc, ReserveStart::pathSignal));
    EXPECT_EQ(wagon->getTrackLoc(), blocked);
}

TEST_F(WaypointSignalFixture, priorityDoesNotReleaseOldPathWhenPhysicalRevalidationFails)
{
    auto* local = vehicle();
    auto* express = vehicle();
    approaching(*local);
    approaching(*express);
    const auto tc = signal(SignalKind::path);
    ASSERT_TRUE(reserveFrom(*local, start, tc, ReserveStart::pathSignal));
    Vehicles::Vehicle(*express).veh1->var_48 |= Vehicles::Flags48::expressMode;
    std::vector<Vehicles::VehicleHead*> order{ local, express };
    prioritiseReservations(order);
    occupiedWagon(local->id); // Appears after the read-only prepass.
    EXPECT_FALSE(reserveFrom(*express, start, tc, ReserveStart::pathSignal));
    ASSERT_NE(pendingPath(enumValue(local->id)), nullptr);
    EXPECT_FALSE(pendingPath(enumValue(local->id))->empty());
    EXPECT_EQ(holdAt(canonicalPiece(start, 0))->owner, enumValue(local->id));
}

TEST_F(WaypointSignalFixture, priorityDropsOnlyUnusedClaimsAndRetainsOwnersOccupiedProtection)
{
    auto* local = vehicle();
    auto* express = vehicle();
    approaching(*local);
    approaching(*express);
    const auto tc = signal(SignalKind::path);
    const auto occupied = canonicalPiece(local->getTrackLoc(), 0);
    holdOccupied(enumValue(local->id), occupied, true);
    ASSERT_TRUE(reserveFrom(*local, start, tc, ReserveStart::pathSignal));
    Vehicles::Vehicle(*express).veh1->var_48 |= Vehicles::Flags48::expressMode;
    std::vector<Vehicles::VehicleHead*> order{ local, express };
    prioritiseReservations(order);
    EXPECT_TRUE(reserveFrom(*express, start, tc, ReserveStart::pathSignal));
    const auto hold = holdAt(occupied);
    ASSERT_TRUE(hold);
    EXPECT_EQ(hold->owner, enumValue(local->id));
    EXPECT_EQ(hold->kind, HoldKind::occupied);
    EXPECT_TRUE(hold->isProtected);
}

TEST(PathSignalTests, priorityPreviewIgnoresOnlyUnusedForeignReservations)
{
    clearAll();
    setHoldValidator(nullptr);
    const World::Pos3 pos{ 640, 640, 16 };
    const auto piece = canonicalPiece(pos, 0);
    ASSERT_TRUE(reservePath(7, { { pos, 0 } }));
    EXPECT_FALSE(canHold(piece, 8));
    EXPECT_TRUE(canHoldIgnoringPending(piece, 8));
    EXPECT_EQ(conflictingPendingOwners(8, { { pos, 0 } }), (std::vector<uint16_t>{ 7 }));
    holdOccupied(7, piece, true);
    EXPECT_FALSE(canHoldIgnoringPending(piece, 8));
    EXPECT_TRUE(canHoldIgnoringPending(piece, 7));
    clearAll();
}

TEST_F(WaypointSignalFixture, physicalFrontProgressBreaksAnOtherwiseEqualWait)
{
    auto* further = vehicle();
    auto* closer = vehicle();
    approaching(*further);
    approaching(*closer);
    const auto tc = signal(SignalKind::path);
    onPathWait(*further, start, tc);
    onPathWait(*closer, start, tc);
    Vehicles::Vehicle(*closer).veh2->subPosition = static_cast<uint16_t>(World::TrackData::getTrackSubPositon(0).size() - 1);
    std::vector<Vehicles::VehicleHead*> order{ further, closer };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), closer);
}

TEST_F(WaypointSignalFixture, exactTiesCanChooseEitherTrainAndReplayFromTheSameSeed)
{
    const auto saved = gPrng1();
    auto* first = vehicle();
    auto* second = vehicle();
    approaching(*first);
    approaching(*second);
    const auto tc = signal(SignalKind::path);
    onPathWait(*first, start, tc);
    onPathWait(*second, start, tc);
    std::set<EntityId> winners;
    for (uint32_t seed = 1; seed <= 32; ++seed)
    {
        clearReservationPriorities();
        const Core::Prng initial(seed * 0x9E3779B9U, seed * 0x7F4A7C15U);
        gPrng1() = initial;
        std::vector<Vehicles::VehicleHead*> order{ first, second };
        prioritiseReservations(order);
        auto* winner = order.front();
        winners.insert(winner->id);
        clearReservationPriorities();
        gPrng1() = initial;
        order = { second, first };
        prioritiseReservations(order);
        EXPECT_EQ(order.front(), winner);
    }
    EXPECT_EQ(winners.size(), 2U);
    gPrng1() = saved;
}

TEST_F(WaypointSignalFixture, deletionAndWorldResetRemoveOldWaitPriority)
{
    auto* first = vehicle();
    auto* second = vehicle();
    approaching(*first);
    approaching(*second);
    const auto tc = signal(SignalKind::path);
    onPathWait(*first, start, tc);
    ScenarioManager::setScenarioTicks(40);
    onPathWait(*second, start, tc);
    forgetTrain(enumValue(first->id));
    ScenarioManager::setScenarioTicks(80);
    onPathWait(*first, start, tc);
    std::vector<Vehicles::VehicleHead*> order{ first, second };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), second);
    clearAll(); // A new game/save must not inherit either waiting clock.
    setSignalKind(facingSide(start, 0), SignalKind::path);
    onPathWait(*first, start, tc);
    ScenarioManager::setScenarioTicks(100);
    onPathWait(*second, start, tc);
    order = { second, first };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), first);
}

TEST_F(WaypointSignalFixture, conflictingRailReorderingPreservesOtherVehicleSlots)
{
    auto* local = vehicle();
    auto* road = vehicle();
    auto* express = vehicle();
    approaching(*local);
    approaching(*express);
    road->mode = TransportMode::road;
    signal(SignalKind::path);
    Vehicles::Vehicle(*express).veh1->var_48 |= Vehicles::Flags48::expressMode;
    std::vector<Vehicles::VehicleHead*> order{ local, road, express };
    prioritiseReservations(order);
    EXPECT_EQ(order, (std::vector<Vehicles::VehicleHead*>{ express, road, local }));
}

TEST_F(WaypointSignalFixture, privateAndSharedServicesUseTheSamePriority)
{
    auto* local = vehicle();
    auto* express = vehicle();
    const auto line = Lines::createLine("Express priority", TransportMode::rail, 4);
    ASSERT_TRUE(line.ok);
    Lines::LineOrder stop;
    stop.type = Vehicles::OrderType::RouteWaypoint;
    stop.tileX = target.x / 32;
    stop.tileY = target.y / 32;
    stop.baseZ = 2;
    ASSERT_TRUE(Lines::addOrder(line.id, stop).ok);
    ASSERT_TRUE(Lines::acceptStopKinds(line.id, Lines::LineKind::rail).ok);
    ASSERT_TRUE(Lines::assignVehicle(line.id, enumValue(express->id), TransportMode::rail).ok);
    approaching(*local);
    approaching(*express);
    signal(SignalKind::chain);
    Vehicles::Vehicle(*express).veh1->var_48 |= Vehicles::Flags48::expressMode;
    const auto cursor = express->currentOrder;
    std::vector<Vehicles::VehicleHead*> order{ local, express };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), express);
    EXPECT_EQ(express->currentOrder, cursor);
    ASSERT_EQ(decide(*express).status, 0);
    expectRefused(*local);
}

TEST_F(WaypointSignalFixture, yieldedUnusedClaimsDisplayRedAtPathChainAndCombinedPosts)
{
    auto* local = vehicle();
    auto* express = vehicle();
    approaching(*local);
    approaching(*express);
    const auto tc = signal(SignalKind::path);
    Vehicles::Vehicle(*express).veh1->var_48 |= Vehicles::Flags48::expressMode;
    for (auto kind : { SignalKind::path, SignalKind::chain, SignalKind::path | SignalKind::chain })
    {
        clearAll();
        setSignalKind(facingSide(start, 0), kind);
        ASSERT_TRUE(reserveFrom(*local, start, tc, ReserveStart::pathSignal));
        std::vector<Vehicles::VehicleHead*> order{ local, express };
        prioritiseReservations(order);
        ASSERT_TRUE(reserveFrom(*express, start, tc, ReserveStart::pathSignal));
        EXPECT_TRUE(isTrainWaiting(enumValue(local->id)));
        EXPECT_EQ(postFrameForDisplay(kind, isSignalBlockedForDisplay(facingSide(start, 0)), 6, 7), 0);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    }
}

TEST_F(WaypointSignalFixture, opposingTrainApproachesArbitrateBeforeEitherCommitsTheConflict)
{
    auto* local = vehicle();
    auto* express = vehicle();
    approaching(*local);
    const auto after = World::Track::getTrackConnectionEnd(target, 0).nextPos;
    addPiece(after, 0);
    Vehicles::Vehicle(*express).applyToComponents([&](auto& part) {part.tileX=after.x;part.tileY=after.y;part.tileBaseZ=4;part.trackAndDirection.track._data=4; });
    Vehicles::RoutingManager::setRouting(express->routingHandle, 4);
    express->getCurrentOrders().begin()->as<Vehicles::OrderRouteWaypoint>()->setWaypoint({ static_cast<coord_t>(start.x / 32), static_cast<coord_t>(start.y / 32) }, 2);
    signal(SignalKind::path);
    signalAt(target, 0, SignalKind::path);
    for (const auto pos : { start, target })
    {
        for (auto& element : World::TileManager::get(pos))
        {
            if (auto* post = element.as<World::SignalElement>())
            {
                post->getRight().setHasSignal(true);
            }
        }
        setSignalKind(facingSide(pos, 4), SignalKind::path);
    }
    Vehicles::Vehicle(*express).veh1->var_48 |= Vehicles::Flags48::expressMode;
    std::vector<Vehicles::VehicleHead*> order{ local, express };
    prioritiseReservations(order);
    ASSERT_EQ(order.front(), express);
    const auto reverse = World::Track::getTrackConnections(target, World::TrackData::getUnkTrack(4).rotationBegin, express->owner, 0, 0, 0);
    ASSERT_TRUE(reserveFrom(*express, target, reverse, ReserveStart::pathSignal));
    const auto forward = World::Track::getTrackConnections(start, World::TrackData::getUnkTrack(0).rotationBegin, local->owner, 0, 0, 0);
    EXPECT_FALSE(reserveFrom(*local, start, forward, ReserveStart::pathSignal));
    EXPECT_TRUE(isSignalWaiting(facingSide(start, 0)));
}

TEST_F(WaypointSignalFixture, stoppedWinnerKeepsQueuePositionUntilItCanStartMoving)
{
    auto* first = vehicle();
    auto* second = vehicle();
    approaching(*first);
    approaching(*second);
    const auto tc = signal(SignalKind::path);
    onPathWait(*first, start, tc);
    ScenarioManager::setScenarioTicks(16);
    onPathWait(*second, start, tc);
    std::vector<Vehicles::VehicleHead*> order{ second, first };
    prioritiseReservations(order);
    ASSERT_EQ(order.front(), first);
    ASSERT_TRUE(reserveFrom(*first, start, tc, ReserveStart::pathSignal));
    ASSERT_FALSE(isTrainWaiting(enumValue(first->id)));
    ScenarioManager::setScenarioTicks(17);
    order = { second, first };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), first);
    EXPECT_FALSE(reserveFrom(*second, start, tc, ReserveStart::pathSignal));
}

TEST_F(WaypointSignalFixture, waitingExpressCanTakeUnusedLookaheadFromMovingExpress)
{
    auto* distant = vehicle();
    auto* near = vehicle();
    approaching(*distant);
    approaching(*near);
    const auto tc = signal(SignalKind::path | SignalKind::chain);
    for (auto* head : { distant, near })
    {
        Vehicles::Vehicle(*head).veh1->var_48 |= Vehicles::Flags48::expressMode;
    }
    ASSERT_TRUE(reserveFrom(*distant, start, tc, ReserveStart::chainSignal));
    Vehicles::Vehicle(*distant).veh2->currentSpeed = 20.0_mph;
    onPathWait(*near, start, tc);
    std::vector<Vehicles::VehicleHead*> order{ distant, near };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), near);
    EXPECT_TRUE(reserveFrom(*near, start, tc, ReserveStart::chainSignal));
    EXPECT_TRUE(pendingPath(enumValue(distant->id))->empty());
}

TEST_F(WaypointSignalFixture, unsignalledDepartureWaitKeepsFIFOWithoutASignalDisplayMarker)
{
    auto* first = vehicle();
    auto* second = vehicle();
    approaching(*first);
    approaching(*second);
    signal(SignalKind::path);
    World::Track::TrackConnections noPost;
    onPathWait(*first, first->getTrackLoc(), noPost);
    setStationDepartureWaiting(enumValue(first->id), true);
    ScenarioManager::setScenarioTicks(40);
    onPathWait(*second, second->getTrackLoc(), noPost);
    setStationDepartureWaiting(enumValue(second->id), true);
    first->status = second->status = Vehicles::Status::loading;
    ASSERT_FALSE(isTrainWaiting(enumValue(first->id)));
    ASSERT_FALSE(isTrainWaiting(enumValue(second->id)));
    std::vector<Vehicles::VehicleHead*> order{ second, first };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), first);
}

// Committing an entrance locks the complete merge, not just its first piece.
TEST(PathSignalTests, committedSectionSurvivesPendingInvalidationAndCannotYield)
{
    clearAll();
    setHoldValidator(nullptr);
    const std::vector<ReservedStep> route{ straight(320, 320, 0), straight(288, 320, 0), straight(256, 320, 0) };
    ASSERT_TRUE(reservePath(7, route));
    ASSERT_TRUE(consumePendingFront(7, route[0].pos, route[0].connection));
    dropPendingPath(7);
    for (const auto& step : route)
    {
        const auto piece = canonicalPiece(step.pos, step.connection);
        EXPECT_TRUE(isCommittedRoutePiece(7, piece));
        EXPECT_FALSE(canHoldIgnoringPending(piece, 8));
        ASSERT_TRUE(holdAt(piece));
        EXPECT_EQ(holdAt(piece)->owner, 7);
    }
    releaseHold(7, canonicalPiece(route[0].pos, 0));
    EXPECT_FALSE(canHoldIgnoringPending(canonicalPiece(route[1].pos, 0), 8));
    releaseAllHolds(7);
    EXPECT_TRUE(canHoldIgnoringPending(canonicalPiece(route[1].pos, 0), 8));
    clearAll();
}

TEST(PathSignalTests, committedSectionReplacesUnusedSuffixOnlyAfterCompleteValidation)
{
    clearAll();
    setHoldValidator(nullptr);
    const auto first = straight(320, 320, 0), old = straight(288, 320, 0), replacement = straight(320, 384, 0);
    ASSERT_TRUE(reservePath(7, { first, old }));
    ASSERT_TRUE(consumePendingFront(7, first.pos, first.connection));
    holdOccupied(8, canonicalPiece(replacement.pos, 0), true);
    EXPECT_FALSE(reservePath(7, { replacement }));
    EXPECT_TRUE(isCommittedRoutePiece(7, canonicalPiece(old.pos, 0)));
    ASSERT_TRUE(holdAt(canonicalPiece(old.pos, 0)));
    releaseAllHolds(8);
    ASSERT_TRUE(reservePath(7, { replacement }));
    EXPECT_FALSE(holdAt(canonicalPiece(old.pos, 0)));
    EXPECT_TRUE(isCommittedRoutePiece(7, canonicalPiece(replacement.pos, 0)));
    ASSERT_TRUE(holdAt(canonicalPiece(first.pos, 0)));
    EXPECT_EQ(holdAt(canonicalPiece(first.pos, 0))->kind, HoldKind::occupied);
    EXPECT_FALSE(canHoldIgnoringPending(canonicalPiece(replacement.pos, 0), 8));
    clearAll();
}

namespace
{
    struct ParallelMergeFixture : WaypointSignalFixture
    {
        const World::Pos3 parallel{ 640, 608, 16 }, common{ 512, 640, 16 }, exit{ 480, 640, 16 };
        static constexpr uint16_t bend = 13 << 3; // Native sBendRight: (-96,+32).
        void mergeSignalAt(World::Pos3 entrance, uint16_t tad, SignalKind kind)
        {
            const auto piece = canonicalPiece(entrance, tad);
            for (const auto& part : World::TrackData::getTrackPiece(piece.trackId))
            {
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, piece.rotation);
                const World::Pos3 pos{ static_cast<coord_t>(piece.x + offset.x), static_cast<coord_t>(piece.y + offset.y), static_cast<coord_t>(piece.z + part.z) };
                World::TileElementEntry* found = nullptr;
                for (auto& element : World::TileManager::get(pos))
                {
                    if (auto* track = element.as<World::TrackElement>(); track && track->trackId() == piece.trackId && track->rotation() == piece.rotation && track->sequenceIndex() == part.index)
                    {
                        track->setHasSignal(true);
                        found = &element;
                        break;
                    }
                }
                ASSERT_NE(found, nullptr);
                auto* post = World::TileManager::insertElementAfterNoReorg(found, World::ElementType::signal, { pos.x, pos.y }, pos.z / 4, 0)->as<World::SignalElement>();
                post->getLeft().setHasSignal(true);
                post->setRotation(piece.rotation);
            }
            setSignalKind(facingSide(entrance, tad), kind);
        }
        void setupMerge(SignalKind kind, Vehicles::VehicleHead*& first, Vehicles::VehicleHead*& second)
        {
            addPiece(parallel, bend);
            addPiece(common, 0);
            addPiece(exit, 0);
            first = vehicle();
            second = vehicle();
            approaching(*first);
            const World::Pos3 before{ 672, 608, 16 };
            addPiece(before, 0);
            Vehicles::Vehicle(*second).applyToComponents([&](auto& c) {c.tileX=before.x;c.tileY=before.y;c.tileBaseZ=4;c.trackAndDirection.track._data=0; });
            Vehicles::RoutingManager::setRouting(second->routingHandle, 0);
            for (auto* head : { first, second })
            {
                head->getCurrentOrders().begin()->as<Vehicles::OrderRouteWaypoint>()->setWaypoint({ 16, 20 }, 2);
            }
            signalAt(start, 0, kind);
            mergeSignalAt(parallel, bend, kind);
            signalAt(exit, 0, SignalKind::none);
        }
        void prepareTailMerge(SignalKind kind, Vehicles::VehicleHead*& first, Vehicles::VehicleHead*& second)
        {
            setupMerge(kind, first, second);
            for (auto pos : { World::Pos3{ 448, 640, 16 }, World::Pos3{ 416, 640, 16 }, World::Pos3{ 384, 640, 16 } })
            {
                addPiece(pos, 0);
            }
            signalAt({ 416, 640, 16 }, 0, SignalKind::none);
            ASSERT_EQ(decide(*first).status, 0);
            const auto owner = enumValue(first->id);
            if (const auto* pending = pendingPath(owner))
            {
                const std::vector<ReservedStep> steps(pending->begin(), pending->end());
                for (const auto& step : steps)
                {
                    onHeadEntersPiece(*first, step.pos, step.connection);
                }
            }
            onPassedBlockSignal(*first);
            const World::Pos3 after{ 384, 640, 16 };
            Vehicles::Vehicle train(*first);
            train.applyToComponents([&](auto& c) {c.tileX=after.x;c.tileY=after.y;c.tileBaseZ=4;c.trackAndDirection.track._data=0;c.moveTo(after); });
            auto at = first->routingHandle;
            at.setIndex(0);
            train.tail->routingHandle = at;
            train.tail->tileX = common.x;
            train.tail->tileY = common.y;
            train.tail->moveTo(common);
            Vehicles::RoutingManager::setRouting(at, 0);
            at.setIndex(1);
            Vehicles::RoutingManager::setRouting(at, World::Track::AdditionalTaDFlags::hasSignal);
            at.setIndex(2);
            Vehicles::RoutingManager::setRouting(at, 0);
            at.setIndex(3);
            Vehicles::RoutingManager::setRouting(at, World::Track::AdditionalTaDFlags::hasSignal);
            at.setIndex(4);
            Vehicles::RoutingManager::setRouting(at, 0);
            first->routingHandle = at;
            for (auto pos : { start, junction, blocked, target })
            {
                onTailLeavesPiece(owner, pos, 0);
            }
            if (kind == SignalKind::none)
            {
                holdOccupied(owner, canonicalPiece(common, 0), false);
            }
        }
        void refreshMergeBlock(Vehicles::VehicleHead& head)
        {
            Vehicles::TrackAndDirection::_TrackAndDirection reverse{ 0, 0 };
            reverse._data = 4;
            Vehicles::sub_4A2AD7(common, reverse, head.owner, head.trackType);
        }
        void checkTailClearance(SignalKind kind)
        {
            Vehicles::VehicleHead *first, *second;
            prepareTailMerge(kind, first, second);
            auto* leading = occupiedWagon(first->id);
            auto* last = occupiedWagon(first->id);
            const auto put = [&](auto& component, World::Pos3 pos) {component.tileX=pos.x;component.tileY=pos.y;component.tileBaseZ=4;component.trackAndDirection.track._data=0;component.moveTo(pos); };
            put(*leading, common);
            put(*last, common);
            Vehicles::Vehicle(*second).veh1->var_48 |= Vehicles::Flags48::expressMode;
            const auto assertRed = [&] {
                refreshMergeBlock(*second);
                EXPECT_TRUE(isRailPieceOccupied(canonicalPiece(common, 0)));
                EXPECT_EQ(decide(*second).status, 3);
                World::SignalElement* post = nullptr;
                for (auto& el : World::TileManager::get(parallel))
                {
                    if (auto* signal = el.as<World::SignalElement>())
                    {
                        post = signal;
                    }
                }
                ASSERT_NE(post, nullptr);
                EXPECT_TRUE(post->getLeft().isOccupied());
                EXPECT_EQ(postFrameForDisplay(kind, isSignalBlockedForDisplay(facingSide(parallel, bend)), 6, 7, post->getLeft().isOccupied()), 0);
                const auto tc = World::Track::getTrackConnections(parallel, 0, second->owner, 0, 0, 0);
                EXPECT_FALSE(physicalEntryIsClear(*second, common, 0, tc));
            };
            assertRed(); // Engine/front is already beyond the exit.
            const World::Pos3 after{ 384, 640, 16 };
            put(*leading, after);
            ScenarioManager::setScenarioTicks(16);
            assertRed(); // Last wagon and tail remain.
            put(*last, after);
            put(*Vehicles::Vehicle(*first).tail, after);
            Vehicles::Vehicle(*first).tail->routingHandle = first->routingHandle;
            for (auto pos : { common, exit, World::Pos3{ 448, 640, 16 }, World::Pos3{ 416, 640, 16 } })
            {
                onTailLeavesPiece(enumValue(first->id), pos, 0);
            }
            refreshMergeBlock(*second);
            ScenarioManager::setScenarioTicks(32);
            EXPECT_FALSE(isRailPieceOccupied(canonicalPiece(common, 0)));
            EXPECT_EQ(decide(*second).status, 0);
            EXPECT_FALSE(isSignalBlockedForDisplay(facingSide(parallel, bend)));
        }
        void checkMerge(SignalKind kind)
        {
            Vehicles::VehicleHead *first, *second;
            setupMerge(kind, first, second);
            ASSERT_EQ(decide(*first).status, 0);
            Vehicles::Vehicle(*second).veh1->var_48 |= Vehicles::Flags48::expressMode;
            second->status = Vehicles::Status::waitingAtSignal;
            World::Track::TrackConnections tc;
            tc.connections.push_back(bend | World::Track::AdditionalTaDFlags::hasSignal);
            onPathWait(*second, parallel, tc);
            std::vector<Vehicles::VehicleHead*> order{ first, second };
            prioritiseReservations(order);
            auto next = second->routingHandle;
            next.setIndex(next.getIndex() + 1);
            const auto result = decide(*second);
            EXPECT_EQ(result.status, 3);
            EXPECT_EQ(Vehicles::RoutingManager::getRouting(next), Vehicles::RoutingManager::kAllocatedButFreeRouting);
            if (kind != SignalKind::none)
            {
                ASSERT_TRUE(holdAt(canonicalPiece(common, 0)));
                EXPECT_EQ(holdAt(canonicalPiece(common, 0))->owner, enumValue(first->id));
                EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(parallel, bend)));
            }
        }
    };
}
TEST_F(ParallelMergeFixture, pathEntranceLocksWholeMergingSection) { checkMerge(SignalKind::path); }
TEST_F(ParallelMergeFixture, chainEntranceLocksWholeBlockAndExit) { checkMerge(SignalKind::chain); }
TEST_F(ParallelMergeFixture, combinedEntranceLocksWholeMergingSection) { checkMerge(SignalKind::path | SignalKind::chain); }
TEST_F(ParallelMergeFixture, ordinaryBlockEntranceKeepsNativeMergeExclusion) { checkMerge(SignalKind::none); }

TEST_F(WaypointSignalFixture, standaloneChainRejectsNativeOccupiedBlockEvenWhenSelectedPathIsFree)
{
    auto* head = vehicle();
    approaching(*head);
    const auto tc = signal(SignalKind::chain);
    for (auto& el : World::TileManager::get(start))
    {
        if (auto* post = el.as<World::SignalElement>())
        {
            post->getLeft().setIsOccupied(true);
        }
    }
    EXPECT_FALSE(reserveFrom(*head, start, tc, ReserveStart::chainSignal));
    expectRefused(*head);
}
TEST_F(WaypointSignalFixture, standaloneChainRechecksNativeBlockAfterPendingGrant)
{
    auto* head = vehicle();
    approaching(*head);
    const auto tc = signal(SignalKind::chain);
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::chainSignal));
    for (auto& el : World::TileManager::get(start))
    {
        if (auto* post = el.as<World::SignalElement>())
        {
            post->getLeft().setIsOccupied(true);
        }
    }
    EXPECT_FALSE(validatePendingPath(*head, start, tc));
}
TEST_F(WaypointSignalFixture, ordinaryOnlyPhysicalFrontRejectsForeignWagon)
{
    auto* head = vehicle();
    auto* other = vehicle();
    approaching(*head);
    signal(SignalKind::none);
    ASSERT_EQ(decide(*head).status, 0);
    auto* wagon = occupiedWagon(other->id);
    wagon->tileX = start.x;
    wagon->tileY = start.y;
    wagon->moveTo(start);
    auto* front = Vehicles::Vehicle(*head).veh2;
    const auto before = front->getTrackLoc();
    auto ahead = front->routingHandle;
    ahead.setIndex(ahead.getIndex() + 1);
    Vehicles::Vehicle(*head).veh1->routingHandle = ahead;
    front->subPosition = static_cast<uint16_t>(World::TrackData::getTrackSubPositon(0).size() - 1);
    front->remainingDistance = 0;
    EXPECT_FALSE(isRailLogicActive());
    const auto motion = front->updateTrackMotion(0x368A, true);
    EXPECT_TRUE(motion.hasFlags(Vehicles::UpdateVar1136114Flags::unk_m00));
    EXPECT_EQ(front->getTrackLoc(), before);
}

TEST_F(WaypointSignalFixture, signalWaitAgesSurviveCompanionLoad)
{
    auto* first = vehicle();
    auto* second = vehicle();
    approaching(*first);
    approaching(*second);
    const auto tc = signal(SignalKind::chain);
    first->status = second->status = Vehicles::Status::waitingAtSignal;
    onPathWait(*first, start, tc);
    ScenarioManager::setScenarioTicks(40);
    onPathWait(*second, start, tc);
    ScenarioManager::setScenarioTicks(80);
    const auto save = std::filesystem::temp_directory_path() / "openloco-priority-age.SV5";
    ASSERT_TRUE(saveCompanion(save));
    clearAll();
    ASSERT_TRUE(loadCompanion(save));
    const auto restored = capturePriorities();
    ASSERT_EQ(restored.size(), 2U);
    for (const auto& record : restored)
    {
        EXPECT_EQ(record.waitedTicks, record.owner == enumValue(first->id) ? 80U : 40U);
    }
    for (int i = 0; i < 3; ++i)
    {
        std::vector<Vehicles::VehicleHead*> order{ second, first };
        prioritiseReservations(order);
        EXPECT_EQ(order.front(), first);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    }
    std::filesystem::remove(companionPath(save));
}
TEST_F(WaypointSignalFixture, cachedPriorityLotterySurvivesLoadWithoutConsumingRandomness)
{
    const auto saved = gPrng1();
    auto* first = vehicle();
    auto* second = vehicle();
    approaching(*first);
    approaching(*second);
    const auto tc = signal(SignalKind::path);
    first->status = second->status = Vehicles::Status::waitingAtSignal;
    onPathWait(*first, start, tc);
    onPathWait(*second, start, tc);
    gPrng1() = Core::Prng(1234, 5678);
    std::vector<Vehicles::VehicleHead*> order{ first, second };
    prioritiseReservations(order);
    auto* winner = order.front();
    const auto drawn = gPrng1();
    const auto save = std::filesystem::temp_directory_path() / "openloco-priority-lottery.SV5";
    ASSERT_TRUE(saveCompanion(save));
    clearAll();
    ASSERT_TRUE(loadCompanion(save));
    order = { second, first };
    prioritiseReservations(order);
    EXPECT_EQ(order.front(), winner);
    EXPECT_EQ(gPrng1().srand_0(), drawn.srand_0());
    EXPECT_EQ(gPrng1().srand_1(), drawn.srand_1());
    gPrng1() = saved;
    std::filesystem::remove(companionPath(save));
}
TEST_F(WaypointSignalFixture, unsignalledDepartureWaitAgeAndFlagSurviveLoad)
{
    auto* head = vehicle();
    approaching(*head);
    signal(SignalKind::path);
    World::Track::TrackConnections noPost;
    onPathWait(*head, head->getTrackLoc(), noPost);
    setStationDepartureWaiting(enumValue(head->id), true);
    head->status = Vehicles::Status::loading;
    ScenarioManager::setScenarioTicks(100);
    const auto save = std::filesystem::temp_directory_path() / "openloco-priority-departure.SV5";
    ASSERT_TRUE(saveCompanion(save));
    clearAll();
    ASSERT_TRUE(loadCompanion(save));
    EXPECT_TRUE(isStationDepartureWaiting(enumValue(head->id)));
    const auto records = capturePriorities();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records[0].waitedTicks, 100U);
    std::filesystem::remove(companionPath(save));
}
TEST_F(ParallelMergeFixture, committedMergeSurvivesReloadAndPendingInvalidation)
{
    Vehicles::VehicleHead *first, *second;
    setupMerge(SignalKind::path, first, second);
    ASSERT_EQ(decide(*first).status, 0);
    dropPendingPath(enumValue(first->id));
    const auto save = std::filesystem::temp_directory_path() / "openloco-committed-merge.SV5";
    ASSERT_TRUE(saveCompanion(save));
    clearAll();
    ASSERT_TRUE(loadCompanion(save));
    EXPECT_TRUE(isCommittedRoutePiece(enumValue(first->id), canonicalPiece(common, 0)));
    Vehicles::Vehicle(*second).veh1->var_48 |= Vehicles::Flags48::expressMode;
    std::vector<Vehicles::VehicleHead*> order{ first, second };
    prioritiseReservations(order);
    EXPECT_EQ(decide(*second).status, 3);
    EXPECT_EQ(holdAt(canonicalPiece(common, 0))->owner, enumValue(first->id));
    std::filesystem::remove(companionPath(save));
}
TEST_F(WaypointSignalFixture, priorityCompanionRejectsReusedVehicleIdentity)
{
    auto* head = vehicle();
    approaching(*head);
    const auto tc = signal(SignalKind::path);
    onPathWait(*head, start, tc);
    const auto save = std::filesystem::temp_directory_path() / "openloco-priority-identity.SV5";
    ASSERT_TRUE(saveCompanion(save));
    ++Vehicles::Vehicle(*head).veh1->dayCreated;
    EXPECT_FALSE(loadCompanion(save));
    EXPECT_TRUE(capturePriorities().empty());
    EXPECT_EQ(specialSignalCount(), 0U);
    std::filesystem::remove(companionPath(save));
}
TEST_F(WaypointSignalFixture, priorityCompanionRejectsChangedNativeSaveAndPreservesOccupiedHolds)
{
    auto* head = vehicle();
    approaching(*head);
    signal(SignalKind::path);
    const auto piece = canonicalPiece(head->getTrackLoc(), 0);
    holdOccupied(enumValue(head->id), piece, true);
    const auto save = std::filesystem::temp_directory_path() / "openloco-priority-native-match.SV5";
    {
        std::ofstream file(save, std::ios::binary);
        file << "native save bytes";
    }
    ASSERT_TRUE(saveCompanion(save));
    {
        std::ofstream file(save, std::ios::binary);
        file << "different save";
    }
    EXPECT_FALSE(loadCompanion(save));
    EXPECT_TRUE(capturePriorities().empty());
    EXPECT_EQ(specialSignalCount(), 0U);
    ASSERT_TRUE(holdAt(piece));
    EXPECT_EQ(holdAt(piece)->kind, HoldKind::occupied);
    std::filesystem::remove(save);
    std::filesystem::remove(companionPath(save));
}
TEST_F(WaypointSignalFixture, priorityCompanionRejectsTruncatedFooterWithoutPartialAttachment)
{
    auto* head = vehicle();
    approaching(*head);
    const auto tc = signal(SignalKind::path);
    onPathWait(*head, start, tc);
    const auto save = std::filesystem::temp_directory_path() / "openloco-priority-truncated.SV5";
    ASSERT_TRUE(saveCompanion(save));
    std::ifstream input(companionPath(save), std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    input.close();
    bytes.resize(bytes.find("PRIORITIES"));
    {
        std::ofstream output(companionPath(save), std::ios::binary);
        output << bytes;
    }
    EXPECT_FALSE(loadCompanion(save));
    EXPECT_TRUE(capturePriorities().empty());
    EXPECT_EQ(specialSignalCount(), 0U);
    std::filesystem::remove(companionPath(save));
}
TEST_F(WaypointSignalFixture, failedPriorityWritePreservesPreviousCompanion)
{
    auto* head = vehicle();
    approaching(*head);
    signal(SignalKind::path);
    const auto save = std::filesystem::temp_directory_path() / "openloco-priority-atomic.SV5";
    ASSERT_TRUE(saveCompanion(save));
    std::ifstream input(companionPath(save), std::ios::binary);
    const std::string before((std::istreambuf_iterator<char>(input)), {});
    input.close();
    auto temporary = companionPath(save);
    temporary += ".tmp";
    ASSERT_TRUE(std::filesystem::create_directory(temporary));
    EXPECT_FALSE(saveCompanion(save));
    std::ifstream check(companionPath(save), std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(check)), {});
    check.close();
    EXPECT_EQ(after, before);
    std::filesystem::remove(temporary);
    std::filesystem::remove(companionPath(save));
}
TEST(PathSignalTests, legacyVersionThreeStillLoadsWithoutPriorityRecords)
{
    clearAll();
    setHoldValidator(nullptr);
    const auto save = std::filesystem::temp_directory_path() / "openloco-signals-v3.SV5";
    {
        std::ofstream file(companionPath(save), std::ios::binary);
        file << "OSIGNALS 3\nKINDS 1\n320 320 0 0 0 0 2\nTRAINS 1\n7 1 1\n320 320 0 0\n";
    }
    ASSERT_TRUE(loadCompanion(save));
    EXPECT_EQ(getSignalKind({ { 320, 320, 0, 0, 0 }, 0 }), SignalKind::chain);
    ASSERT_NE(pendingPath(7), nullptr);
    EXPECT_EQ(pendingPath(7)->size(), 1U);
    EXPECT_TRUE(capturePriorities().empty());
    std::filesystem::remove(companionPath(save));
    clearAll();
}

TEST_F(WaypointSignalFixture, ordinaryBlockWaitAgesSurviveCompanionLoad)
{
    auto* first = vehicle();
    auto* second = vehicle();
    approaching(*first);
    approaching(*second);
    const auto tc = signal(SignalKind::none);
    first->status = second->status = Vehicles::Status::waitingAtSignal;
    onPathWait(*first, start, tc);
    ScenarioManager::setScenarioTicks(40);
    onPathWait(*second, start, tc);
    ScenarioManager::setScenarioTicks(80);
    const auto save = std::filesystem::temp_directory_path() / "openloco-priority-block-age.SV5";
    ASSERT_TRUE(saveCompanion(save));
    clearAll();
    ASSERT_TRUE(loadCompanion(save));
    const auto restored = capturePriorities();
    ASSERT_EQ(restored.size(), 2U);
    for (const auto& record : restored)
    {
        EXPECT_EQ(record.waitedTicks, record.owner == enumValue(first->id) ? 80U : 40U);
    }
    for (int i = 0; i < 3; ++i)
    {
        std::vector<Vehicles::VehicleHead*> order{ second, first };
        prioritiseReservations(order);
        EXPECT_EQ(order.front(), first);
        ScenarioManager::setScenarioTicks(ScenarioManager::getScenarioTicks() + 16);
    }
    std::filesystem::remove(companionPath(save));
}
TEST_F(StationSignalFixture, departureContinuationCannotYieldBeforePlanningResumes)
{
    auto* head = stationVehicle();
    atPlatform(*head);
    signal(SignalKind::path);
    ASSERT_TRUE(ensureStationDeparture(*head));
    const auto owner = enumValue(head->id);
    const auto* pending = pendingPath(owner);
    ASSERT_NE(pending, nullptr);
    ASSERT_FALSE(pending->empty());
    for (const auto& step : *pending)
    {
        const auto piece = canonicalPiece(step.pos, step.connection);
        EXPECT_TRUE(isCommittedRoutePiece(owner, piece));
        EXPECT_FALSE(canHoldIgnoringPending(piece, owner + 1));
    }
    dropPendingPath(owner);
    EXPECT_FALSE(canHoldIgnoringPending(canonicalPiece(blocked, 0), owner + 1));
}
TEST_F(WaypointSignalFixture, combinedSignalUsesPathClearanceInsteadOfStandaloneChainBlockRule)
{
    auto* head = vehicle();
    approaching(*head);
    const auto tc = signal(SignalKind::path | SignalKind::chain);
    for (auto& element : World::TileManager::get(start))
    {
        if (auto* post = element.as<World::SignalElement>())
        {
            post->getLeft().setIsOccupied(true);
        }
    }
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::chainSignal));
    EXPECT_EQ(decide(*head).status, 0);
}
TEST_F(WaypointSignalFixture, nativeSaveRejectsPriorityRouteWithoutVehicleIdentity)
{
    auto* head = vehicle();
    approaching(*head);
    const auto tc = signal(SignalKind::path);
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    const auto save = std::filesystem::temp_directory_path() / "openloco-priority-zero-identity.SV5";
    {
        std::ofstream native(save, std::ios::binary);
        native << "matching native save";
    }
    ASSERT_TRUE(saveCompanion(save));
    std::ifstream input(companionPath(save), std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    input.close();
    const std::string tag = "\nTRAINS 1\n";
    const auto record = bytes.find(tag);
    ASSERT_NE(record, std::string::npos);
    const auto begin = bytes.find(' ', record + tag.size()) + 1;
    const auto end = bytes.find(' ', begin);
    bytes.replace(begin, end - begin, "0");
    {
        std::ofstream file(companionPath(save), std::ios::binary);
        file << bytes;
    }
    EXPECT_FALSE(loadCompanion(save));
    EXPECT_EQ(pathHoldCount(), 0U);
    EXPECT_EQ(specialSignalCount(), 0U);
    std::filesystem::remove(save);
    std::filesystem::remove(companionPath(save));
}
TEST_F(WaypointSignalFixture, pickedUpOwnerDoesNotWriteAnUnvalidatedNativeRoute)
{
    auto* head = vehicle();
    approaching(*head);
    const auto tc = signal(SignalKind::path);
    ASSERT_TRUE(reserveFrom(*head, start, tc, ReserveStart::pathSignal));
    Vehicles::Vehicle(*head).applyToComponents([](auto& c) { c.tileX = -1; });
    const auto save = std::filesystem::temp_directory_path() / "openloco-priority-picked-up.SV5";
    {
        std::ofstream native(save, std::ios::binary);
        native << "native save";
    }
    ASSERT_TRUE(saveCompanion(save));
    clearAll();
    ASSERT_TRUE(loadCompanion(save));
    EXPECT_EQ(pendingPath(enumValue(head->id)), nullptr);
    EXPECT_EQ(specialSignalCount(), 1U);
    std::filesystem::remove(save);
    std::filesystem::remove(companionPath(save));
}
TEST_F(ParallelMergeFixture, mergeResumesAfterNativeSectionIsCleared)
{
    Vehicles::VehicleHead *first, *second;
    setupMerge(SignalKind::chain, first, second);
    ASSERT_EQ(decide(*first).status, 0);
    EXPECT_EQ(decide(*second).status, 3);
    Vehicles::Vehicle(*first).applyToComponents([](auto& c) { c.tileX = -1; });
    forgetTrain(enumValue(first->id));
    Vehicles::TrackAndDirection::_TrackAndDirection reverse{ 0, 0 };
    reverse._data = 4;
    Vehicles::sub_4A2AD7(common, reverse, second->owner, second->trackType);
    ScenarioManager::setScenarioTicks(16);
    ASSERT_EQ(decide(*second).status, 0);
    EXPECT_EQ(holdAt(canonicalPiece(common, 0))->owner, enumValue(second->id));
    EXPECT_FALSE(isTrainWaiting(enumValue(second->id)));
}

TEST_F(ParallelMergeFixture, normalRedRemainsUntilLastWagonAndTailClear) { checkTailClearance(SignalKind::none); }
TEST_F(ParallelMergeFixture, chainRedRemainsUntilLastWagonAndTailClear) { checkTailClearance(SignalKind::chain); }
TEST_F(ParallelMergeFixture, pathRedRemainsUntilLastWagonAndTailClear) { checkTailClearance(SignalKind::path); }
TEST_F(ParallelMergeFixture, combinedRedRemainsUntilLastWagonAndTailClear) { checkTailClearance(SignalKind::path | SignalKind::chain); }
TEST_F(ParallelMergeFixture, tailProtectionAndRedRemainAfterCompanionReload)
{
    Vehicles::VehicleHead *first, *second;
    prepareTailMerge(SignalKind::path, first, second);
    refreshMergeBlock(*second);
    ASSERT_EQ(decide(*second).status, 3);
    const auto save = std::filesystem::temp_directory_path() / "openloco-tail-clearance.SV5";
    ASSERT_TRUE(saveCompanion(save));
    onGameLoaded();
    ASSERT_TRUE(loadCompanion(save));
    EXPECT_EQ(decide(*second).status, 3);
    EXPECT_TRUE(isSignalBlockedForDisplay(facingSide(parallel, bend)));
    ASSERT_TRUE(holdAt(canonicalPiece(common, 0)));
    EXPECT_EQ(holdAt(canonicalPiece(common, 0))->owner, enumValue(first->id));
    std::filesystem::remove(companionPath(save));
}
TEST_F(WaypointSignalFixture, ordinaryBlockHandoffIncludesOnlyOccupiedProtectedHolds)
{
    auto* first = vehicle();
    auto* incoming = vehicle();
    Vehicles::Vehicle(*first).applyToComponents([&](auto& c) {c.tileX=blocked.x;c.tileY=blocked.y;c.tileBaseZ=4;c.trackAndDirection.track._data=0; });
    Vehicles::RoutingManager::setRouting(first->routingHandle, 0);
    ASSERT_TRUE(reservePath(enumValue(first->id), { { blocked, 0 } }));
    ASSERT_TRUE(consumePendingFront(enumValue(first->id), blocked, 0));
    onPassedBlockSignal(*first);
    ASSERT_EQ(pathHoldCount(), 0U);
    EXPECT_TRUE(blockHasForeignPath(*incoming, start, 0));
    onTailLeavesPiece(enumValue(first->id), blocked, 0);
    EXPECT_FALSE(blockHasForeignPath(*incoming, start, 0));
}
TEST_F(WaypointSignalFixture, nativeBlockClearingKeepsRemoteConflictingWagonOccupied)
{
    auto* owner = vehicle();
    auto* wagon = occupiedWagon(owner->id);
    constexpr uint16_t curve = 11 << 3;
    addPiece(junction, curve);
    wagon->tileX = junction.x;
    wagon->tileY = junction.y;
    wagon->trackAndDirection.track._data = curve;
    const auto movement = World::TrackData::getTrackSubPositon(curve);
    const auto remote = std::find_if(movement.begin(), movement.end(), [&](const auto& sub) { return (junction + sub.loc).x / 32 != junction.x / 32 || (junction + sub.loc).y / 32 != junction.y / 32; });
    ASSERT_NE(remote, movement.end());
    wagon->moveTo(junction + remote->loc);
    Vehicles::TrackAndDirection::_TrackAndDirection tad{ 0, 0 };
    EXPECT_TRUE(isRailPieceOccupied(canonicalPiece(junction, 0)));
    EXPECT_TRUE(Vehicles::isBlockOccupied(junction, tad, owner->owner, 0));
    wagon->tileX = -1;
    wagon->moveTo({ 960, 960, 16 });
    EXPECT_FALSE(Vehicles::isBlockOccupied(junction, tad, owner->owner, 0));
}
TEST(PathSignalTests, occupiedBlockAndChainPostsCannotDisplayAProceedFrame)
{
    for (auto kind : { SignalKind::none, SignalKind::chain })
    {
        for (uint8_t count : { 4, 7, 10 })
        {
            for (uint8_t frame = 0; frame < count; ++frame)
            {
                EXPECT_EQ(postFrameForDisplay(kind, false, frame, count, true), 0);
                EXPECT_EQ(postFrameForDisplay(kind, false, frame, count, false), frame);
            }
        }
    }
    for (auto kind : { SignalKind::path, SignalKind::path | SignalKind::chain })
    {
        EXPECT_EQ(postFrameForDisplay(kind, false, 6, 7, true), 6);
        EXPECT_EQ(postFrameForDisplay(kind, true, 6, 7, true), 0);
    }
}
TEST(PathSignalTests, menuAndMapUseSameTypeColoursForBothDirections)
{
    EXPECT_EQ(signalArrowColour(SignalKind::none, false), Colour::grey);
    EXPECT_EQ(signalArrowColour(SignalKind::none, true), Colour::grey);
    EXPECT_EQ(signalArrowColour(SignalKind::path, false), Colour::yellow);
    EXPECT_EQ(signalArrowColour(SignalKind::path, true), Colour::yellow);
    EXPECT_EQ(signalArrowColour(SignalKind::chain, false), Colour::darkBlue);
    EXPECT_EQ(signalArrowColour(SignalKind::chain, true), Colour::darkBlue);
    EXPECT_EQ(signalArrowColour(SignalKind::path | SignalKind::chain, false), Colour::orange);
    EXPECT_EQ(signalArrowColour(SignalKind::path | SignalKind::chain, true), Colour::orange);
}
TEST(PathSignalTests, signalMenuLegendFitsBelowExistingControlsAndAboveCost)
{
    namespace Construction = Ui::Windows::Construction;
    const auto widgets = Construction::Signal::getWidgets();
    EXPECT_EQ(widgets[Construction::Common::widx::frame].right + 1, 156);
    EXPECT_EQ(widgets[Construction::Common::widx::frame].bottom + 1, 286);
    for (size_t i = 8; i < widgets.size(); ++i)
    {
        EXPECT_LT(widgets[i].bottom, 214);
    }
    EXPECT_LT(230 + 12 + 10, 254);
    EXPECT_LT(254 + 10, 286 - 12 - 9);
}
