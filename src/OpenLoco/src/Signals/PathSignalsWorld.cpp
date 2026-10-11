#include "Signals/PathSignals.h"

#include "Entities/EntityManager.h"
#include "Map/SignalElement.h"
#include "Objects/ObjectManager.h"
#include "Objects/TrainSignalObject.h"
#include "Map/TileManager.h"
#include "Map/Track/Track.h"
#include "Map/Track/TrackData.h"
#include "Map/TrackElement.h"
#include "Scenario/ScenarioManager.h"
#include "Signals/PathSearch.h"
#include "Vehicles/OrderManager.h"
#include "Vehicles/Orders.h"
#include "Vehicles/RoutingManager.h"
#include "Vehicles/Vehicle.h"
#include "Vehicles/Vehicle1.h"
#include "Vehicles/Vehicle2.h"
#include "Vehicles/VehicleHead.h"
#include "Vehicles/VehicleManager.h"
#include "Vehicles/VehicleTail.h"
#include "ViewportManager.h"
#include "World/Station.h"
#include "World/StationManager.h"
#include "ZoomLevel.hpp"
#include <OpenLoco/Math/Vector.hpp>
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <map>
#include <set>

namespace OpenLoco::Signals
{
    namespace
    {
        constexpr uint16_t kBasicTaDMask = World::Track::AdditionalTaDFlags::basicTaDMask;
        constexpr uint16_t kHasSignal = World::Track::AdditionalTaDFlags::hasSignal;
        constexpr uint8_t kTrackIdCount = 44;
        constexpr uint16_t kNoOwner = 0xFFFF;
        constexpr uint8_t kRouteLightHousings = 0xC;
        constexpr uint32_t kRetryTicks = 16;
        constexpr size_t kMaxBlockPieces = 512;

        std::map<uint16_t, uint32_t> _retryTick;

        struct TrackLookup
        {
            World::TileElementEntry* entry = nullptr;
            World::TrackElement* track = nullptr;
        };

        TrackLookup findTrackElement(const PieceKey& piece)
        {
            if (piece.trackId >= kTrackIdCount)
            {
                return {};
            }
            const auto& part = World::TrackData::getTrackPiece(piece.trackId)[0];
            const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, piece.rotation);
            const World::Pos3 pos{ static_cast<int16_t>(piece.x + offset.x), static_cast<int16_t>(piece.y + offset.y), static_cast<int16_t>(piece.z + part.z) };
            if (!World::validCoords(World::Pos2{ pos.x, pos.y }))
            {
                return {};
            }
            auto tile = World::TileManager::get(World::Pos2{ pos.x, pos.y });
            for (auto& el : tile)
            {
                auto* track = el.as<World::TrackElement>();
                if (track == nullptr)
                {
                    continue;
                }
                if (track->baseHeight() != pos.z || track->rotation() != piece.rotation || track->sequenceIndex() != part.index || track->trackId() != piece.trackId)
                {
                    continue;
                }
                if (track->isGhost() || track->isAiAllocated())
                {
                    continue;
                }
                return TrackLookup{ &el, track };
            }
            return {};
        }

        World::SignalElement* signalAfter(const TrackLookup& lookup)
        {
            if (lookup.track == nullptr || !lookup.track->hasSignal())
            {
                return nullptr;
            }
            return lookup.entry->next()->as<World::SignalElement>();
        }

        bool sideHasRealSignal(const World::SignalElement& signal, uint8_t side)
        {
            if (signal.isGhost() || signal.isAiAllocated())
            {
                return false;
            }
            if (side == 0)
            {
                return signal.getLeft().hasSignal() && !signal.isLeftGhost();
            }
            return signal.getRight().hasSignal() && !signal.isRightGhost();
        }

        const World::SignalElement::Side& sideOf(const World::SignalElement& signal, uint8_t side)
        {
            return side == 0 ? signal.getLeft() : signal.getRight();
        }

        Vehicles::VehicleHead* findHead(uint16_t owner)
        {
            auto* head = EntityManager::get<Vehicles::VehicleHead>(EntityId(owner));
            if (head == nullptr || !head->isVehicleHead() || head->mode != TransportMode::rail || head->tileX == -1)
            {
                return nullptr;
            }
            return head;
        }

        template<typename Function>
        void forEachRingPiece(Vehicles::VehicleHead& head, Function&& function)
        {
            Vehicles::Vehicle train(head);
            auto* tail = train.tail;
            if (tail == nullptr || tail->tileX == -1)
            {
                return;
            }
            World::Pos3 pos{ tail->tileX, tail->tileY, static_cast<int16_t>(tail->tileBaseZ * World::kSmallZStep) };
            for (const auto handle : Vehicles::RoutingManager::RingView(tail->routingHandle))
            {
                const auto routing = Vehicles::RoutingManager::getRouting(handle);
                const uint16_t tad = routing & kBasicTaDMask;
                if (((tad >> 3) & 0x3F) >= kTrackIdCount)
                {
                    break;
                }
                if (function(canonicalPiece(pos, tad)))
                {
                    return;
                }
                pos += World::TrackData::getUnkTrack(tad).pos;
            }
        }

        bool validateHold(const PieceKey& piece, const Hold& hold)
        {
            auto* head = findHead(hold.owner);
            if (head == nullptr)
            {
                return false;
            }
            if (hold.kind == HoldKind::path)
            {
                const auto* pending = pendingPath(hold.owner);
                if (pending == nullptr)
                {
                    return false;
                }
                return std::any_of(pending->begin(), pending->end(), [&piece](const ReservedStep& step) { return canonicalPiece(step.pos, step.connection) == piece; });
            }
            bool found = false;
            forEachRingPiece(*head, [&](const PieceKey& ringPiece) {
                found = ringPiece == piece;
                return found;
            });
            return found;
        }

        bool ownerExists(const PieceKey&, const Hold& hold)
        {
            return findHead(hold.owner) != nullptr;
        }

        bool pieceOccupiedByOther(const PieceKey& piece, uint16_t owner)
        {
            if (piece.trackId >= kTrackIdCount)
            {
                return false;
            }
            for (const auto& part : World::TrackData::getTrackPiece(piece.trackId))
            {
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, piece.rotation);
                const World::Pos2 tilePos{ static_cast<int16_t>(piece.x + offset.x), static_cast<int16_t>(piece.y + offset.y) };
                if (!World::validCoords(tilePos))
                {
                    continue;
                }
                for (auto* entity : EntityManager::EntityTileList(tilePos))
                {
                    auto* vehicle = entity->asBase<Vehicles::VehicleBase>();
                    if (vehicle == nullptr || vehicle->mode != TransportMode::rail || vehicle->tileX == -1)
                    {
                        continue;
                    }
                    if (vehicle->has38Flags(Vehicles::Flags38::unk_0 | Vehicles::Flags38::unk_2))
                    {
                        continue;
                    }
                    if (enumValue(vehicle->head) == owner)
                    {
                        continue;
                    }
                    const auto vehiclePiece = canonicalPiece(vehicle->getTrackLoc(), vehicle->getTrackAndDirection().track._data);
                    if (vehiclePiece == piece || piecesConflict(vehiclePiece, piece))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        bool canHoldInWorld(const PieceKey& piece, uint16_t owner)
        {
            return canHold(piece, owner) && !pieceOccupiedByOther(piece, owner);
        }

        bool pieceBusy(const PieceKey& piece)
        {
            // A reserved path is not a train in the block. Block signals stay green
            // until a train's route occupies the piece, and special posts follow that.
            if (const auto hold = holdAt(piece); hold.has_value() && hold->kind == HoldKind::occupied)
            {
                return true;
            }
            return pieceOccupiedByOther(piece, kNoOwner);
        }

        template<typename Function>
        void forEachPieceSignal(const PieceKey& piece, Function&& function)
        {
            if (piece.trackId >= kTrackIdCount)
            {
                return;
            }
            for (const auto& part : World::TrackData::getTrackPiece(piece.trackId))
            {
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, piece.rotation);
                const World::Pos3 pos{
                    static_cast<int16_t>(piece.x + offset.x),
                    static_cast<int16_t>(piece.y + offset.y),
                    static_cast<int16_t>(piece.z + part.z),
                };
                if (!World::validCoords(World::Pos2{ pos.x, pos.y }))
                {
                    continue;
                }
                auto tile = World::TileManager::get(World::Pos2{ pos.x, pos.y });
                for (auto& el : tile)
                {
                    auto* track = el.as<World::TrackElement>();
                    if (track == nullptr || track->baseHeight() != pos.z || track->rotation() != piece.rotation || track->sequenceIndex() != part.index || track->trackId() != piece.trackId)
                    {
                        continue;
                    }
                    if (track->isGhost() || track->isAiAllocated() || !track->hasSignal())
                    {
                        continue;
                    }
                    auto* signal = el.next()->as<World::SignalElement>();
                    if (signal == nullptr || signal->isGhost() || signal->isAiAllocated())
                    {
                        continue;
                    }
                    function(*signal, pos, *track);
                }
            }
        }

        World::SignalElement::Side& sideOf(World::SignalElement& signal, uint8_t side)
        {
            return side == 0 ? signal.getLeft() : signal.getRight();
        }

        // Side 0 is drawn on the first tile of the piece and side 1 on the last.
        World::SignalElement::Side* drawnSide(const SideKey& side)
        {
            World::SignalElement::Side* drawn = nullptr;
            forEachPieceSignal(side.piece, [&](World::SignalElement& signal, const World::Pos3&, const World::TrackElement& track) {
                if (!sideHasRealSignal(signal, side.side))
                {
                    return;
                }
                if (side.side == 0 ? track.sequenceIndex() == 0 : track.isFlag6())
                {
                    drawn = &sideOf(signal, side.side);
                }
            });
            return drawn;
        }

        void setSideAspect(const SideKey& side, DisplayState state)
        {
            const auto lookup = findTrackElement(side.piece);
            const auto* drawn = drawnSide(side);
            if (lookup.track == nullptr || drawn == nullptr)
            {
                return;
            }
            const auto* signalObj = ObjectManager::get<TrainSignalObject>(drawn->signalObjectId());
            if (signalObj == nullptr)
            {
                return;
            }
            const auto target = static_cast<uint8_t>(postStateFor(state, static_cast<uint8_t>((signalObj->numFrames + 2) / 3)));
            const auto lastFrame = static_cast<uint8_t>(signalObj->numFrames == 0 ? 0 : signalObj->numFrames - 1);
            if (drawn->getUnk4() == target && drawn->frame() == std::min<uint8_t>(static_cast<uint8_t>(target * 3), lastFrame))
            {
                return;
            }

            // Vanilla clears the lit route lights on state 0; the post must not change them.
            std::vector<std::pair<World::SignalElement::Side*, uint8_t>> lights;
            forEachPieceSignal(side.piece, [&](World::SignalElement& signal, const World::Pos3&, const World::TrackElement&) {
                if (sideHasRealSignal(signal, side.side))
                {
                    auto& signalSide = sideOf(signal, side.side);
                    lights.emplace_back(&signalSide, signalSide.allLights());
                }
            });

            const Vehicles::TrackAndDirection::_TrackAndDirection tad(side.piece.trackId, side.piece.rotation);
            uint32_t flags = target;
            if (side.side != 0)
            {
                flags |= 1U << 31;
            }
            Vehicles::setSignalState(World::Pos3{ side.piece.x, side.piece.y, side.piece.z }, tad, lookup.track->trackObjectId(), flags);
            for (const auto& [signalSide, saved] : lights)
            {
                signalSide->setAllLights(saved);
            }
        }

        uint32_t distanceBetween(const World::Pos3& a, const World::Pos3& b)
        {
            const uint32_t dx = static_cast<uint32_t>(std::abs(a.x - b.x));
            const uint32_t dy = static_cast<uint32_t>(std::abs(a.y - b.y));
            const uint32_t dz = static_cast<uint32_t>(std::abs(a.z - b.z));
            return std::min(dx, dy) / 4 + std::max(dx, dy) + dz;
        }

        EntrySignal toEntrySignal(SignalKind kind)
        {
            if (isChainSignal(kind))
            {
                return EntrySignal::chain;
            }
            if (isPathSignal(kind))
            {
                return EntrySignal::path;
            }
            return EntrySignal::block;
        }

        class WorldTrackGraph final : public TrackGraph
        {
        public:
            explicit WorldTrackGraph(Vehicles::VehicleHead& head)
                : _owner(enumValue(head.id))
                , _company(head.owner)
                , _trackType(head.trackType)
                , _requiredMods(head.var_53)
            {
                Vehicles::Vehicle train(head);
                _queryMods = train.veh1->var_49;
                loadTarget(head);
            }

            bool hasTarget() const
            {
                return _hasTarget;
            }

            StepEdge makeEdge(const World::Pos3& pos, uint16_t connection, StationId tileStation) const
            {
                StepEdge edge;
                edge.step = TrackStep{ pos, static_cast<uint16_t>(connection & kBasicTaDMask) };
                edge.connection = connection;
                const auto trackId = static_cast<uint8_t>((edge.step.tad >> 3) & 0x3F);
                edge.cost = std::max<uint32_t>(1, World::TrackData::getTrackMiscData(trackId).unkWeighting);

                const auto info = classifyEntry(pos, connection);
                if (info.blockedFromBehind)
                {
                    edge.signal = EntrySignal::blockedFromBehind;
                }
                else if (info.hasFacingSignal)
                {
                    edge.signal = toEntrySignal(info.kind);
                    edge.exitOccupied = info.kind == SignalKind::none && info.facingOccupied;
                }

                edge.blocked = !canHoldInWorld(canonicalPiece(pos, edge.step.tad), _owner);

                if (_hasTarget)
                {
                    if (_targetStation != StationId::null)
                    {
                        edge.isTarget = tileStation == _targetStation;
                    }
                    else
                    {
                        edge.isTarget = (pos == _targetPos && edge.step.tad == _targetTad) || (pos == _targetReversePos && edge.step.tad == _targetReverseTad);
                    }
                    edge.distanceToTarget = distanceBetween(pos, _targetPos);
                }
                return edge;
            }

            void successors(const TrackStep& from, std::vector<StepEdge>& out) override
            {
                const auto [nextPos, nextRotation] = World::Track::getTrackConnectionEnd(from.pos, from.tad);
                const auto tc = World::Track::getTrackConnections(nextPos, nextRotation, _company, _trackType, _requiredMods, _queryMods);
                for (const auto connection : tc.connections)
                {
                    out.push_back(makeEdge(nextPos, connection, tc.stationId));
                }
            }

        private:
            void loadTarget(Vehicles::VehicleHead& head)
            {
                if (head.sizeOfOrderTable <= 1)
                {
                    return;
                }
                auto orders = head.getCurrentOrders();
                auto current = orders.begin();
                if (const auto* stationOrder = current->as<Vehicles::OrderStation>())
                {
                    const auto* station = StationManager::get(stationOrder->getStation());
                    if (station == nullptr)
                    {
                        return;
                    }
                    _hasTarget = true;
                    _targetStation = stationOrder->getStation();
                    _targetPos = World::Pos3{ station->x, station->y, station->z };
                    return;
                }
                if (const auto* waypoint = current->as<Vehicles::OrderRouteWaypoint>())
                {
                    _hasTarget = true;
                    _targetPos = waypoint->getWaypoint();
                    _targetTad = static_cast<uint16_t>((waypoint->getTrackId() << 3) | waypoint->getDirection());
                    const auto& trackSize = World::TrackData::getUnkTrack(_targetTad);
                    _targetReversePos = _targetPos + trackSize.pos;
                    if (trackSize.rotationEnd < 12)
                    {
                        _targetReversePos -= World::Pos3{ World::kRotationOffset[trackSize.rotationEnd], 0 };
                    }
                    _targetReverseTad = _targetTad ^ (1U << 2);
                }
            }

            uint16_t _owner;
            CompanyId _company;
            uint8_t _trackType;
            uint8_t _requiredMods;
            uint8_t _queryMods = 0;
            bool _hasTarget = false;
            StationId _targetStation = StationId::null;
            World::Pos3 _targetPos{};
            uint16_t _targetTad = 0;
            World::Pos3 _targetReversePos{};
            uint16_t _targetReverseTad = 0;
        };

        bool _vanillaAspectsRestored = false;

        void armVanillaAspectRepair()
        {
            _vanillaAspectsRestored = false;
        }

        // Older builds painted path and chain posts red, then yellow as a train approached.
        // Put them back to a clear block signal once, then leave the colour to vanilla.
        void restoreVanillaAspectsOnce()
        {
            if (_vanillaAspectsRestored)
            {
                return;
            }
            _vanillaAspectsRestored = true;
            for (const auto& [side, kind] : allSignalKinds())
            {
                if (kind == SignalKind::none)
                {
                    continue;
                }
                setSideAspect(side, pieceBusy(side.piece) ? DisplayState::red : DisplayState::green);
                setChainAspect(side, ChainAspect::green);
            }
        }

        void markWaitingRed(uint16_t owner, const World::Pos3& nextPos, const World::Track::TrackConnections& tc)
        {
            std::vector<SideKey> sides;
            for (const auto connection : tc.connections)
            {
                if (!(connection & kHasSignal))
                {
                    continue;
                }
                const auto side = facingSide(nextPos, connection);
                if (getSignalKind(side) == SignalKind::none)
                {
                    continue;
                }
                sides.push_back(side);
            }
            setWaitingSignals(owner, sides);
        }
    }

    bool hasSignalSide(const PieceKey& piece, uint8_t side)
    {
        bool present = false;
        forEachPieceSignal(piece, [&](World::SignalElement& signal, const World::Pos3&, const World::TrackElement&) {
            if (sideHasRealSignal(signal, side))
            {
                present = true;
            }
        });
        return present;
    }

    EntryInfo classifyEntry(const World::Pos3& entrance, uint16_t connection)
    {
        EntryInfo info;
        if (!(connection & kHasSignal))
        {
            return info;
        }
        const uint16_t tad = connection & kBasicTaDMask;
        const auto piece = canonicalPiece(entrance, tad);
        const auto* signal = signalAfter(findTrackElement(piece));
        if (signal == nullptr)
        {
            return info;
        }
        const uint8_t facing = (tad & (1U << 2)) ? 1 : 0;
        const uint8_t behind = facing == 0 ? 1 : 0;
        if (sideHasRealSignal(*signal, facing))
        {
            info.hasFacingSignal = true;
            info.kind = getSignalKind(SideKey{ piece, facing });
            info.facingOccupied = sideOf(*signal, facing).isOccupied();
            return info;
        }
        if (sideHasRealSignal(*signal, behind))
        {
            info.blockedFromBehind = true;
        }
        return info;
    }

    bool isRailLogicActive()
    {
        return specialSignalCount() > 0 || pathHoldCount() > 0;
    }

    bool isSpecialFacingSide(const World::Pos3& entrance, uint16_t trackAndDirection)
    {
        if (specialSignalCount() == 0)
        {
            return false;
        }
        return getSignalKind(facingSide(entrance, trackAndDirection)) != SignalKind::none;
    }

    std::optional<uint16_t> reservedConnectionAt(Vehicles::VehicleHead& head, const World::Pos3& nextPos, const World::Track::TrackConnections& tc)
    {
        const auto owner = enumValue(head.id);
        const auto* pending = pendingPath(owner);
        if (pending == nullptr || pending->empty())
        {
            return std::nullopt;
        }
        const auto& front = pending->front();
        if (front.pos == nextPos)
        {
            for (const auto connection : tc.connections)
            {
                if ((connection & kBasicTaDMask) == (front.connection & kBasicTaDMask))
                {
                    return connection;
                }
            }
        }
        dropPendingPath(owner);
        return std::nullopt;
    }

    std::optional<uint16_t> reserveFrom(Vehicles::VehicleHead& head, const World::Pos3& nextPos, const World::Track::TrackConnections& tc, ReserveStart start)
    {
        const auto owner = enumValue(head.id);
        const auto now = ScenarioManager::getScenarioTicks();
        const auto retry = _retryTick.find(owner);
        if (retry != _retryTick.end() && now < retry->second)
        {
            return std::nullopt;
        }

        WorldTrackGraph graph(head);
        SearchRequest request;
        request.mode = start == ReserveStart::chainSignal ? SearchMode::chainSignal : (start == ReserveStart::pathSignal ? SearchMode::pathSignal : SearchMode::extend);
        request.hasTarget = graph.hasTarget();
        for (const auto connection : tc.connections)
        {
            request.starts.push_back(graph.makeEdge(nextPos, connection, tc.stationId));
        }

        const auto result = findReservation(graph, request);
        if (!result.found)
        {
            _retryTick[owner] = now + kRetryTicks;
            markWaitingRed(owner, nextPos, tc);
            return std::nullopt;
        }

        std::vector<ReservedStep> steps;
        for (const auto& edge : result.reservation)
        {
            steps.push_back(ReservedStep{ edge.step.pos, edge.connection });
        }
        dropPendingPath(owner);
        for (const auto& step : steps)
        {
            if (pieceOccupiedByOther(canonicalPiece(step.pos, step.connection), owner))
            {
                _retryTick[owner] = now + kRetryTicks;
                markWaitingRed(owner, nextPos, tc);
                return std::nullopt;
            }
        }
        if (!reservePath(owner, steps))
        {
            _retryTick[owner] = now + kRetryTicks;
            markWaitingRed(owner, nextPos, tc);
            return std::nullopt;
        }
        _retryTick.erase(owner);
        clearWaitingSignals(owner);
        return result.reservation.front().connection;
    }

    bool pieceHeldByProtectedOther(const Vehicles::VehicleHead& head, const World::Pos3& entrance, uint16_t connection)
    {
        return isHeldByProtectedOther(canonicalPiece(entrance, connection), enumValue(head.id));
    }

    bool blockHasForeignPath(const Vehicles::VehicleHead& head, const World::Pos3& entrance, uint16_t connection)
    {
        if (pathHoldCount() == 0)
        {
            return false;
        }
        const auto owner = enumValue(head.id);
        std::deque<ReservedStep> queue{ ReservedStep{ entrance, connection } };
        std::set<PieceKey> visited;
        while (!queue.empty() && visited.size() < kMaxBlockPieces)
        {
            const auto step = queue.front();
            queue.pop_front();
            const auto piece = canonicalPiece(step.pos, step.connection);
            if (!visited.insert(piece).second)
            {
                continue;
            }
            if (isHeldByProtectedOther(piece, owner))
            {
                return true;
            }
            const auto [nextPos, nextRotation] = World::Track::getTrackConnectionEnd(step.pos, step.connection & kBasicTaDMask);
            const auto tc = World::Track::getTrackConnections(nextPos, nextRotation, head.owner, head.trackType, 0, 0);
            for (const auto next : tc.connections)
            {
                if (next & kHasSignal)
                {
                    continue;
                }
                queue.push_back(ReservedStep{ nextPos, next });
            }
        }
        return false;
    }

    void onHeadEntersPiece(Vehicles::VehicleHead& head, const World::Pos3& entrance, uint16_t connection)
    {
        const auto owner = enumValue(head.id);
        const uint16_t tad = connection & kBasicTaDMask;
        consumePendingFront(owner, entrance, connection);
        holdOccupied(owner, canonicalPiece(entrance, tad), isInPathMode(owner));
    }

    void onTailLeavesPiece(uint16_t owner, const World::Pos3& entrance, uint16_t routing)
    {
        releaseHold(owner, canonicalPiece(entrance, routing & kBasicTaDMask));
    }

    void clearRouteLights(const World::Pos3& entrance, uint16_t trackAndDirection)
    {
        const auto side = facingSide(entrance, trackAndDirection & kBasicTaDMask);
        forEachPieceSignal(side.piece, [&](World::SignalElement& signal, const World::Pos3& pos, const World::TrackElement&) {
            if (!sideHasRealSignal(signal, side.side))
            {
                return;
            }
            auto& signalSide = sideOf(signal, side.side);
            const auto kept = static_cast<uint8_t>(signalSide.allLights() & kRouteLightHousings);
            if (signalSide.allLights() != kept)
            {
                signalSide.setAllLights(kept);
                Ui::ViewportManager::invalidate(World::Pos2{ pos.x, pos.y }, pos.z, static_cast<coord_t>(pos.z + 24), ZoomLevel::half);
            }
        });
    }

    void onCompanionLoaded()
    {
        if (!companionNeedsLightRepair())
        {
            return;
        }
        clearCompanionLightRepair();
        for (const auto& entry : allSignalKinds())
        {
            const auto& side = entry.first;
            forEachPieceSignal(side.piece, [&](World::SignalElement& signal, const World::Pos3& pos, const World::TrackElement&) {
                if (!sideHasRealSignal(signal, side.side))
                {
                    return;
                }
                auto& signalSide = sideOf(signal, side.side);
                if (signalSide.allLights() != 0)
                {
                    signalSide.setAllLights(0);
                    Ui::ViewportManager::invalidate(World::Pos2{ pos.x, pos.y }, pos.z, static_cast<coord_t>(pos.z + 24), ZoomLevel::half);
                }
            });
        }
    }

    void onPassedBlockSignal(Vehicles::VehicleHead& head)
    {
        setPathMode(enumValue(head.id), false);
    }

    void rebuildTrainHolds(Vehicles::VehicleHead& head)
    {
        const auto owner = enumValue(head.id);
        dropPendingPath(owner);
        clearWaitingSignals(owner);
        const bool isProtected = isInPathMode(owner);
        releaseAllHolds(owner);
        if (head.mode != TransportMode::rail || head.tileX == -1)
        {
            return;
        }
        forEachRingPiece(head, [&](const PieceKey& piece) {
            holdOccupied(owner, piece, isProtected);
            return false;
        });
    }

    void onSignalPlaced(const PieceKey& piece, uint16_t sides, SignalKind kind, bool ghost)
    {
        for (uint8_t side = 0; side < 2; ++side)
        {
            const uint16_t bit = side == 0 ? (1U << 15) : (1U << 14);
            if (!(sides & bit))
            {
                continue;
            }
            const SideKey key{ piece, side };
            setSignalKind(key, kind);
            (void)ghost;
        }
    }

    void onSignalSidesRemoved(const PieceKey& piece)
    {
        const auto* signal = signalAfter(findTrackElement(piece));
        for (uint8_t side = 0; side < 2; ++side)
        {
            const bool present = signal != nullptr && sideOf(*signal, side).hasSignal();
            if (!present)
            {
                setSignalKind(SideKey{ piece, side }, SignalKind::none);
            }
        }
    }

    void onTrackRemoved(const PieceKey& piece)
    {
        setSignalKind(SideKey{ piece, 0 }, SignalKind::none);
        setSignalKind(SideKey{ piece, 1 }, SignalKind::none);
        for (const auto owner : trainsWithPendingPiece(piece))
        {
            dropPendingPath(owner);
        }
        releaseHoldsAt(piece);
    }

    namespace
    {
        void invalidateReservationPiece(const PieceKey& piece)
        {
            if (piece.trackId >= kTrackIdCount)
            {
                Ui::ViewportManager::invalidate(World::Pos2{ piece.x, piece.y }, piece.z, static_cast<coord_t>(piece.z + 48), ZoomLevel::half, 32);
                return;
            }
            for (const auto& part : World::TrackData::getTrackPiece(piece.trackId))
            {
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, piece.rotation);
                const auto x = static_cast<int16_t>(piece.x + offset.x);
                const auto y = static_cast<int16_t>(piece.y + offset.y);
                const auto z = static_cast<coord_t>(piece.z + part.z);
                Ui::ViewportManager::invalidate(World::Pos2{ x, y }, z, static_cast<coord_t>(z + part.clearZ + 32), ZoomLevel::half, 40);
            }
        }
    }

    void reset()
    {
        clearAll();
        _retryTick.clear();
        armVanillaAspectRepair();
        setHoldValidator(&validateHold);
        setReservationViewDirty(&invalidateReservationPiece);
    }

    void onGameLoaded()
    {
        reset();
        for (auto* head : VehicleManager::VehicleList())
        {
            if (head->mode == TransportMode::rail && head->tileX != -1)
            {
                rebuildTrainHolds(*head);
            }
        }
    }

    void prepareForSave()
    {
        std::vector<SideKey> missing;
        for (const auto& [side, kind] : allSignalKinds())
        {
            const auto* signal = signalAfter(findTrackElement(side.piece));
            if (signal == nullptr || !sideHasRealSignal(*signal, side.side))
            {
                missing.push_back(side);
            }
        }
        for (const auto& side : missing)
        {
            setSignalKind(side, SignalKind::none);
        }
    }

    void tick()
    {
        refreshReservationView();
        const auto ticks = ScenarioManager::getScenarioTicks();
        if ((ticks & 7) == 0 && specialSignalCount() > 0)
        {
            restoreVanillaAspectsOnce();
        }
        if ((ticks & 511) == 0 && holdCount() > 0)
        {
            pruneHolds(&ownerExists);
        }
    }
}
