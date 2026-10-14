#include "Signals/PathSignals.h"

#include "Entities/EntityManager.h"
#include "Map/SignalElement.h"
#include "Objects/ObjectManager.h"
#include "Objects/TrainSignalObject.h"
#include "Map/TileManager.h"
#include "Map/Track/Track.h"
#include "Map/Track/TrackData.h"
#include "Map/Track/SubpositionData.h"
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
#include <array>
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

        const std::vector<World::Pos2>& physicalTiles(const PieceKey& piece)
        {
            // Some native movement endpoints sit outside the track element's
            // tiles. Include both traversal directions, computed once from the
            // actual movement tables, rather than guessing a neighbourhood.
            static const auto offsets = [] {
                std::array<std::vector<World::Pos2>, kTrackIdCount * 4> result;
                for (uint16_t id = 0; id < kTrackIdCount; ++id)
                {
                    for (uint8_t rotation = 0; rotation < 4; ++rotation)
                    {
                        auto& tiles = result[id * 4 + rotation];
                        const auto add = [&](World::Pos2 pos) {
                            pos.x = static_cast<int16_t>(pos.x & ~31);
                            pos.y = static_cast<int16_t>(pos.y & ~31);
                            if (std::find(tiles.begin(), tiles.end(), pos) == tiles.end())
                            {
                                tiles.push_back(pos);
                            }
                        };
                        for (const auto& part : World::TrackData::getTrackPiece(id))
                        {
                            add(Math::Vector::rotate(World::Pos2{ part.x, part.y }, rotation));
                        }
                        for (const uint16_t reversed : { uint16_t{ 0 }, uint16_t{ 4 } })
                        {
                            const uint16_t tad = (id << 3) | rotation | reversed;
                            World::Pos3 entrance{};
                            if (reversed)
                            {
                                const auto& delta = World::TrackData::getUnkTrack(tad);
                                entrance -= delta.pos;
                                if (delta.rotationEnd < 12)
                                {
                                    entrance += World::Pos3{ World::kRotationOffset[delta.rotationEnd], 0 };
                                }
                            }
                            for (const auto& step : World::TrackData::getTrackSubPositon(tad))
                            {
                                const auto pos = entrance + step.loc;
                                add({ pos.x, pos.y });
                            }
                        }
                    }
                }
                return result;
            }();
            return offsets[piece.trackId * 4 + piece.rotation];
        }

        bool pieceOccupiedByOther(const PieceKey& piece, uint16_t owner)
        {
            if (piece.trackId >= kTrackIdCount)
            {
                return false;
            }
            std::vector<PieceKey> candidates{ piece };
            // A wagon may be on another tile of a long curve or switch whose
            // footprint conflicts here. Find those physical pieces locally,
            // then inspect their complete movement footprints as well.
            for (const auto& part : World::TrackData::getTrackPiece(piece.trackId))
            {
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, piece.rotation);
                const World::Pos2 tilePos{ static_cast<int16_t>(piece.x + offset.x), static_cast<int16_t>(piece.y + offset.y) };
                if (!World::validCoords(tilePos))
                {
                    continue;
                }
                for (const auto& element : World::TileManager::get(tilePos))
                {
                    const auto* track = element.as<World::TrackElement>();
                    if (track == nullptr || track->isGhost() || track->isAiAllocated() || track->trackId() >= kTrackIdCount)
                    {
                        continue;
                    }
                    const auto parts = World::TrackData::getTrackPiece(track->trackId());
                    if (track->sequenceIndex() >= parts.size())
                    {
                        continue;
                    }
                    const auto& trackPart = parts[track->sequenceIndex()];
                    const auto originOffset = Math::Vector::rotate(World::Pos2{ trackPart.x, trackPart.y }, track->rotation());
                    const PieceKey candidate{
                        static_cast<int16_t>(tilePos.x - originOffset.x),
                        static_cast<int16_t>(tilePos.y - originOffset.y),
                        static_cast<int16_t>(track->baseHeight() - trackPart.z),
                        track->rotation(),
                        track->trackId()
                    };
                    if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end() && piecesConflict(candidate, piece))
                    {
                        candidates.push_back(candidate);
                    }
                }
            }
            for (const auto& candidate : candidates)
            {
                for (const auto& offset : physicalTiles(candidate))
                {
                    const World::Pos2 tilePos{ static_cast<int16_t>(candidate.x + offset.x), static_cast<int16_t>(candidate.y + offset.y) };
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
                        if (vehicle->has38Flags(Vehicles::Flags38::unk_0 | Vehicles::Flags38::unk_2 | Vehicles::Flags38::isGhost))
                        {
                            continue;
                        }
                        if (enumValue(vehicle->head) == owner)
                        {
                            continue;
                        }
                        if (((vehicle->getTrackAndDirection().track._data & kBasicTaDMask) >> 3) >= kTrackIdCount)
                        {
                            // Invalid real vehicle track data cannot authorize entry.
                            return true;
                        }
                        const auto vehiclePiece = canonicalPiece(vehicle->getTrackLoc(), vehicle->getTrackAndDirection().track._data);
                        if (vehiclePiece == piece || piecesConflict(vehiclePiece, piece))
                        {
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        bool canHoldInWorld(const PieceKey& piece, uint16_t owner)
        {
            return canHold(piece, owner) && !pieceOccupiedByOther(piece, owner);
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

            bool requiresTarget() const
            {
                return _hasTarget && _targetStation == StationId::null;
            }

            StepEdge makeEdge(const World::Pos3& pos, uint16_t connection, StationId tileStation) const
            {
                StepEdge edge;
                edge.step = TrackStep{ pos, static_cast<uint16_t>(connection & kBasicTaDMask) };
                edge.connection = connection;
                edge.entryDirection = World::TrackData::getUnkTrack(edge.step.tad).rotationBegin;
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

        // Repair older special-post colour writes to the native resting danger
        // state once. Subsequent approach/passing colours remain native.
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
                setSideAspect(side, DisplayState::red);
                setChainAspect(side, ChainAspect::green);
            }
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

    bool validatePendingPath(Vehicles::VehicleHead& head, const World::Pos3& nextPos, const World::Track::TrackConnections& tc)
    {
        const auto owner = enumValue(head.id);
        const auto* pending = pendingPath(owner);
        if (pending != nullptr)
        {
            for (const auto& step : *pending)
            {
                // Pending holds are a plan, not proof that no wagon has been
                // placed or moved onto it since the reservation was granted.
                if (!canHoldInWorld(canonicalPiece(step.pos, step.connection), owner))
                {
                    dropPendingPath(owner);
                    _retryTick[owner] = ScenarioManager::getScenarioTicks() + kRetryTicks;
                    onPathWait(head, nextPos, tc);
                    return false;
                }
            }
        }
        return true;
    }

    bool physicalEntryIsClear(Vehicles::VehicleHead& head, const World::Pos3& nextPos, uint16_t connection, const World::Track::TrackConnections& tc)
    {
        const auto owner = enumValue(head.id);
        const auto piece = canonicalPiece(nextPos, connection);
        const auto hold = holdAt(piece);
        const bool ownedProtectedPiece = hold && hold->owner == owner && hold->isProtected;
        const bool physicalClear = !pieceOccupiedByOther(piece, owner);
        const bool claimsClear = isInPathMode(owner) || ownedProtectedPiece ? canHold(piece, owner) : !isHeldByProtectedOther(piece, owner);
        if (physicalClear && claimsClear)
        {
            return true;
        }
        onPathWait(head, nextPos, tc);
        return false;
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

    std::optional<ReserveStart> reservationStartAtJunction(const Vehicles::VehicleHead& head, const World::Pos3& nextPos, const World::Track::TrackConnections& tc)
    {
        if (isInPathMode(enumValue(head.id)))
        {
            return ReserveStart::extend;
        }
        for (const auto connection : tc.connections)
        {
            const auto entry = classifyEntry(nextPos, connection);
            if (entry.hasFacingSignal && entry.kind != SignalKind::none)
            {
                // The chosen start's signal determines Path versus Chain in
                // the search; one chain arm must not govern every other arm.
                return ReserveStart::pathSignal;
            }
        }
        return std::nullopt;
    }

    void refreshApproachSignals(Vehicles::VehicleHead& head)
    {
        const auto owner = enumValue(head.id);
        if (head.mode != TransportMode::rail || head.tileX == -1 || head.has38Flags(Vehicles::Flags38::isGhost) || specialSignalCount() == 0)
        {
            clearApproachingSignals(owner);
            return;
        }
        // The normal reservation retry already proves an actual wait. Keep its
        // red marker without running a duplicate route search on this tick.
        if (isTrainWaiting(owner))
        {
            return;
        }
        if (!beginApproachProbe(owner, ScenarioManager::getScenarioTicks()))
        {
            return;
        }
        WorldTrackGraph graph(head);
        if (!graph.requiresTarget())
        {
            setApproachingSignals(owner, {});
            return;
        }
        Vehicles::Vehicle train(head);
        auto pos = train.veh2->getTrackLoc();
        auto tad = train.veh2->trackAndDirection.track._data;
        auto handle = train.veh2->routingHandle;
        // Follow the committed physical approach, then unambiguous track only.
        // Junction selection remains with the normal reservation path.
        for (size_t i = 0; i < 8 && ((tad >> 3) & 0x3F) < kTrackIdCount; ++i)
        {
            const auto end = World::Track::getTrackConnectionEnd(pos, tad);
            const auto tc = World::Track::getTrackConnections(end.nextPos, end.nextRotation, head.owner, head.trackType, head.var_53, train.veh1->var_49);
            handle.setIndex(handle.getIndex() + 1);
            const auto routing = Vehicles::RoutingManager::getRouting(handle);
            std::optional<uint16_t> connection;
            if (routing != Vehicles::RoutingManager::kAllocatedButFreeRouting && routing != Vehicles::RoutingManager::kRoutingNull)
            {
                for (const auto next : tc.connections)
                {
                    if ((next & kBasicTaDMask) == (routing & kBasicTaDMask))
                    {
                        connection = next;
                        break;
                    }
                }
            }
            else if (tc.connections.size() == 1)
            {
                connection = tc.connections.front();
            }
            if (!connection)
            {
                // A post can be on a junction arm before any route is committed.
                // An unrelated free arm must not clear the intended waypoint's
                // signal. Probe all starts once, with their individual kinds.
                SearchRequest request;
                request.hasTarget = request.requireTarget = true;
                request.mode = SearchMode::pathSignal;
                std::vector<SideKey> sides;
                for (const auto next : tc.connections)
                {
                    request.starts.push_back(graph.makeEdge(end.nextPos, next, tc.stationId));
                    const auto entry = classifyEntry(end.nextPos, next);
                    if (entry.hasFacingSignal && entry.kind != SignalKind::none)
                    {
                        sides.push_back(facingSide(end.nextPos, next));
                    }
                }
                if (!sides.empty())
                {
                    const auto result = findReservation(graph, request);
                    setApproachingSignals(owner, result.found ? std::vector<SideKey>{} : sides);
                    return;
                }
                break;
            }
            const auto entry = classifyEntry(end.nextPos, *connection);
            if (entry.hasFacingSignal)
            {
                if (entry.kind != SignalKind::none)
                {
                    SearchRequest request;
                    request.hasTarget = request.requireTarget = true;
                    request.mode = isChainSignal(entry.kind) ? SearchMode::chainSignal : SearchMode::pathSignal;
                    request.starts.push_back(graph.makeEdge(end.nextPos, *connection, tc.stationId));
                    const auto result = findReservation(graph, request);
                    setApproachingSignals(owner, result.found ? std::vector<SideKey>{} : std::vector<SideKey>{ facingSide(end.nextPos, *connection) });
                    return;
                }
                break;
            }
            if (entry.blockedFromBehind)
            {
                break;
            }
            pos = end.nextPos;
            tad = *connection & kBasicTaDMask;
        }
        setApproachingSignals(owner, {});
    }

    void onPathWait(const Vehicles::VehicleHead& head, const World::Pos3& nextPos, const World::Track::TrackConnections& tc)
    {
        std::vector<SideKey> sides;
        for (const auto connection : tc.connections)
        {
            const auto entry = classifyEntry(nextPos, connection);
            if (entry.hasFacingSignal && entry.kind != SignalKind::none)
            {
                sides.push_back(facingSide(nextPos, connection));
            }
        }

        // The planner can already be beyond the signal the physical train is
        // approaching. Keep that governing post red too, even when the failed
        // decision is an unsignalled junction. Never cross a facing block post.
        auto pos = World::Pos3{ head.tileX, head.tileY, static_cast<int16_t>(head.tileBaseZ * World::kSmallZStep) };
        auto handle = head.routingHandle;
        for (size_t i = 0; i < Limits::kMaxRoutingsPerVehicle; ++i)
        {
            const auto routing = Vehicles::RoutingManager::getRouting(handle);
            const auto tad = routing & kBasicTaDMask;
            if (routing == Vehicles::RoutingManager::kAllocatedButFreeRouting || routing == Vehicles::RoutingManager::kRoutingNull || ((tad >> 3) & 0x3F) >= kTrackIdCount)
            {
                break;
            }
            if (i != 0)
            {
                pos -= World::TrackData::getUnkTrack(tad).pos;
            }
            const auto entry = classifyEntry(pos, routing);
            if (entry.hasFacingSignal)
            {
                if (entry.kind != SignalKind::none)
                {
                    sides.push_back(facingSide(pos, routing));
                }
                break;
            }
            handle.setIndex(static_cast<uint8_t>(handle.getIndex() - 1));
        }
        setWaitingSignals(enumValue(head.id), sides);
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
        request.requireTarget = graph.requiresTarget();
        for (const auto connection : tc.connections)
        {
            request.starts.push_back(graph.makeEdge(nextPos, connection, tc.stationId));
        }

        const auto result = findReservation(graph, request);
        if (!result.found)
        {
            _retryTick[owner] = now + kRetryTicks;
            onPathWait(head, nextPos, tc);
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
                onPathWait(head, nextPos, tc);
                return std::nullopt;
            }
        }
        if (!reservePath(owner, steps))
        {
            _retryTick[owner] = now + kRetryTicks;
            onPathWait(head, nextPos, tc);
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
        if (isTrainWaiting(owner))
        {
            clearWaitingSignals(owner);
            _retryTick.erase(owner);
        }
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
        clearApproachingSignals(owner);
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
