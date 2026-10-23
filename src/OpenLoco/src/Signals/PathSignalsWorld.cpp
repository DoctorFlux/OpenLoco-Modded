#include "Signals/PathSignals.h"

#include "Entities/EntityManager.h"
#include "Map/SignalElement.h"
#include "Map/StationElement.h"
#include "Map/TileManager.h"
#include "Map/Track/SubpositionData.h"
#include "Map/Track/Track.h"
#include "Map/Track/TrackData.h"
#include "Map/TrackElement.h"
#include "Objects/ObjectManager.h"
#include "Objects/TrainSignalObject.h"
#include "Random.h"
#include "Rules/Rules.h"
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
#include <numeric>
#include <set>
#include <tuple>

namespace OpenLoco::Signals
{
    using namespace OpenLoco::Literals;
    namespace
    {
        constexpr uint16_t kBasicTaDMask = World::Track::AdditionalTaDFlags::basicTaDMask;
        constexpr uint16_t kHasSignal = World::Track::AdditionalTaDFlags::hasSignal;
        constexpr uint8_t kTrackIdCount = 44;
        constexpr uint8_t kRouteLightHousings = 0xC;
        constexpr uint32_t kRetryTicks = 16;
        constexpr size_t kMaxBlockPieces = 512;

        std::map<uint16_t, uint32_t> _retryTick;
        std::map<uint16_t, uint32_t> _signalWaitSince;
        std::map<uint16_t, std::pair<size_t, size_t>> _priorityRank;
        std::map<uint16_t, uint32_t> _priorityLottery;
        std::set<uint16_t> _restoredWaiting;

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
            if (head == nullptr || head->baseType != EntityBaseType::vehicle || !head->isVehicleHead() || head->mode != TransportMode::rail || head->tileX == -1)
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
                if (isCommittedRoutePiece(hold.owner, piece))
                {
                    return true;
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
            explicit WorldTrackGraph(Vehicles::VehicleHead& head, const std::vector<ReservedStep>* committed = nullptr, bool priorityPreview = false)
                : _owner(enumValue(head.id))
                , _company(head.owner)
                , _trackType(head.trackType)
                , _requiredMods(head.var_53)
                , _committed(committed)
                , _priorityPreview(priorityPreview)
            {
                Vehicles::Vehicle train(head);
                _queryMods = train.veh1->var_49;
                loadTarget(head);
            }

            bool hasTarget() const
            {
                return !_targets.empty();
            }

            bool requiresTarget() const
            {
                return hasTarget();
            }

            StepEdge describeStep(const World::Pos3& pos, uint16_t connection) const
            {
                StepEdge edge;
                edge.step = TrackStep{ pos, static_cast<uint16_t>(connection & kBasicTaDMask) };
                edge.connection = connection;
                edge.entryDirection = World::TrackData::getUnkTrack(edge.step.tad).rotationBegin;
                const auto trackId = static_cast<uint8_t>((edge.step.tad >> 3) & 0x3F);
                edge.cost = std::max<uint32_t>(1, World::TrackData::getTrackMiscData(trackId).unkWeighting);

                // TrackConnections::stationId describes the entire junction
                // query. A different arm on that tile may not be a platform.
                const auto lookup = findTrackElement(canonicalPiece(pos, edge.step.tad));
                if (lookup.track && lookup.track->hasStationElement())
                {
                    const auto* platform = lookup.entry->next()->as<World::StationElement>();
                    if (platform && !platform->isGhost() && !platform->isAiAllocated())
                    {
                        edge.station = enumValue(platform->stationId());
                    }
                }
                edge.isTarget = matchesTarget(edge, 0);
                edge.distanceToTarget = targetDistance(edge, 0);
                return edge;
            }

            StepEdge makeEdge(const World::Pos3& pos, uint16_t connection, StationId) const
            {
                auto edge = describeStep(pos, connection);
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

                const auto piece = canonicalPiece(pos, edge.step.tad);
                edge.blocked = _priorityPreview ? (!canHoldIgnoringPending(piece, _owner) || pieceOccupiedByOther(piece, _owner)) : !canHoldInWorld(piece, _owner);
                // Standalone Chain retains native whole-block occupancy.
                // A self-occupied post is already in this train's committed ring.
                const auto own = holdAt(piece);
                if (isChainSignal(info.kind) && !isPathSignal(info.kind) && info.facingOccupied
                    && !(own && own->owner == _owner && own->kind == HoldKind::occupied))
                {
                    edge.blocked = true;
                }

                return edge;
            }

            void successors(const TrackStep& from, std::vector<StepEdge>& out) override
            {
                const auto [nextPos, nextRotation] = World::Track::getTrackConnectionEnd(from.pos, from.tad);
                const auto tc = World::Track::getTrackConnections(nextPos, nextRotation, _company, _trackType, _requiredMods, _queryMods);
                for (const auto connection : tc.connections)
                {
                    auto edge = makeEdge(nextPos, connection, tc.stationId);
                    if (_committed)
                    {
                        const auto current = std::find_if(_committed->begin(), _committed->end(), [&](const auto& step) { return step.pos == from.pos && (step.connection & kBasicTaDMask) == from.tad; });
                        if (current != _committed->end() && std::next(current) != _committed->end())
                        {
                            const auto& required = *std::next(current);
                            if (nextPos != required.pos || edge.step.tad != (required.connection & kBasicTaDMask))
                            {
                                continue;
                            }
                            // The planning head already crossed these posts;
                            // departure must protect the whole committed prefix.
                            if (edge.signal != EntrySignal::blockedFromBehind)
                            {
                                edge.signal = EntrySignal::none;
                            }
                        }
                    }
                    out.push_back(edge);
                }
            }

            uint16_t targetCount() const override { return static_cast<uint16_t>(_targets.size()); }

            bool matchesTarget(const StepEdge& edge, uint16_t index) const override
            {
                if (index >= _targets.size())
                {
                    return false;
                }
                const auto& target = _targets[index];
                if (target.station != StationId::null)
                {
                    return edge.station == enumValue(target.station);
                }
                return (edge.step.pos == target.pos && edge.step.tad == target.tad)
                    || (edge.step.pos == target.reversePos && edge.step.tad == target.reverseTad);
            }

            uint32_t targetDistance(const StepEdge& edge, uint16_t index) const override
            {
                return index < _targets.size() ? distanceBetween(edge.step.pos, _targets[index].pos) : 0;
            }

            bool sameTarget(uint16_t first, uint16_t second) const override
            {
                return first < _targets.size() && second < _targets.size()
                    && _targets[first].station != StationId::null && _targets[first].station == _targets[second].station;
            }

        private:
            struct Target
            {
                StationId station = StationId::null;
                World::Pos3 pos{}, reversePos{};
                uint16_t tad = 0, reverseTad = 0;
            };

            void appendTarget(const Vehicles::Order& order)
            {
                Target target;
                if (const auto* stationOrder = order.as<Vehicles::OrderStation>())
                {
                    const auto* station = StationManager::get(stationOrder->getStation());
                    if (!station || station->empty())
                    {
                        return;
                    }
                    target.station = stationOrder->getStation();
                    target.pos = { station->x, station->y, station->z };
                }
                else if (const auto* waypoint = order.as<Vehicles::OrderRouteWaypoint>())
                {
                    target.pos = waypoint->getWaypoint();
                    target.tad = static_cast<uint16_t>((waypoint->getTrackId() << 3) | waypoint->getDirection());
                    const auto& size = World::TrackData::getUnkTrack(target.tad);
                    target.reversePos = target.pos + size.pos;
                    if (size.rotationEnd < 12)
                    {
                        target.reversePos -= World::Pos3{ World::kRotationOffset[size.rotationEnd], 0 };
                    }
                    target.reverseTad = target.tad ^ (1U << 2);
                }
                else
                {
                    return;
                }
                _targets.push_back(target);
            }

            void loadTarget(Vehicles::VehicleHead& head)
            {
                if (head.sizeOfOrderTable <= 1)
                {
                    return;
                }
                const auto* program = Rules::find(Rules::ownerForVehicle(_owner));
                if (program && !program->blocks.empty())
                {
                    const auto preview = Rules::previewVehicleRoute(_owner);
                    for (const auto key : preview.orders)
                    {
                        const auto row = std::find_if(program->rows.begin(), program->rows.end(), [key](const auto& entry) { return entry.key == key; });
                        if (row != program->rows.end())
                        {
                            appendTarget(*reinterpret_cast<const Vehicles::Order*>(&row->raw));
                            if (!_targets.empty() && _targets.back().station == StationId::null)
                            {
                                break;
                            }
                        }
                    }
                    return;
                }
                for (const auto& order : head.getCurrentOrders())
                {
                    appendTarget(order);
                    // Waypoint progression drops unused lookahead and replans
                    // the following leg; protect through it without predicting
                    // another journey or reversal beyond the safe boundary.
                    if (!_targets.empty() && _targets.back().station == StationId::null)
                    {
                        break;
                    }
                    if (_targets.size() >= 64)
                    {
                        break;
                    }
                }
            }

            uint16_t _owner;
            CompanyId _company;
            uint8_t _trackType;
            uint8_t _requiredMods;
            uint8_t _queryMods = 0;
            const std::vector<ReservedStep>* _committed = nullptr;
            bool _priorityPreview = false;
            std::vector<Target> _targets;
        };

        struct PriorityCandidate
        {
            Vehicles::VehicleHead* head;
            std::vector<ReservedStep> steps;
            bool express;
            bool waiting;
            uint32_t waited;
            uint64_t distance;
            std::vector<PieceKey> pieces;
            World::Pos2 minimum{ INT16_MAX, INT16_MAX }, maximum{ INT16_MIN, INT16_MIN };

            auto key() const { return std::tuple{ !express, !waiting, UINT32_MAX - waited, distance }; }
        };

        bool routesConflict(const PriorityCandidate& a, const PriorityCandidate& b)
        {
            if (a.maximum.x < b.minimum.x || b.maximum.x < a.minimum.x || a.maximum.y < b.minimum.y || b.maximum.y < a.minimum.y)
            {
                return false;
            }
            return std::any_of(a.pieces.begin(), a.pieces.end(), [&](const auto& first) {
                return std::any_of(b.pieces.begin(), b.pieces.end(), [&](const auto& second) { return piecesConflict(first, second); });
            });
        }

        uint64_t remainingPieceCost(const Vehicles::Vehicle2& front, uint16_t routing, uint32_t weight)
        {
            const auto count = World::TrackData::getTrackSubPositon(routing & kBasicTaDMask).size();
            const uint64_t full = uint64_t{ weight } << 16;
            return count == 0 ? full : full * (count - std::min<size_t>(front.subPosition, count - 1)) / count;
        }

        uint64_t approachCost(Vehicles::VehicleHead& head, const ReservedStep& first)
        {
            Vehicles::Vehicle train(head);
            auto pos = train.veh2->getTrackLoc();
            uint64_t cost = 0;
            for (const auto handle : Vehicles::RoutingManager::RingView(train.veh2->routingHandle))
            {
                const auto routing = Vehicles::RoutingManager::getRouting(handle);
                if (pos == first.pos)
                {
                    break;
                }
                if (routing == Vehicles::RoutingManager::kAllocatedButFreeRouting || routing == Vehicles::RoutingManager::kRoutingNull)
                {
                    break;
                }
                const auto id = (routing >> 3) & 0x3F;
                if (id >= kTrackIdCount)
                {
                    break;
                }
                const auto weight = std::max<uint32_t>(1, World::TrackData::getTrackMiscData(id).unkWeighting);
                cost += handle == train.veh2->routingHandle ? remainingPieceCost(*train.veh2, routing, weight) : uint64_t{ weight } << 16;
                pos += World::TrackData::getUnkTrack(routing & kBasicTaDMask).pos;
                if (handle == head.routingHandle)
                {
                    break;
                }
            }
            return cost;
        }

        // All checks precede yielding. Occupied holds (including the planning
        // ring) are never released, even for an Express train.
        bool reserveWithPriority(uint16_t owner, const std::vector<ReservedStep>& steps)
        {
            if (reservePath(owner, steps))
            {
                return true;
            }
            const auto rank = _priorityRank.find(owner);
            if (rank == _priorityRank.end())
            {
                return false;
            }
            for (const auto& step : steps)
            {
                const auto piece = canonicalPiece(step.pos, step.connection);
                if (!canHoldIgnoringPending(piece, owner) || pieceOccupiedByOther(piece, owner))
                {
                    return false;
                }
            }
            const auto blockers = conflictingPendingOwners(owner, steps);
            for (const auto other : blockers)
            {
                const auto otherRank = _priorityRank.find(other);
                // An absent owner might already be travelling through the
                // section. Its reservation cannot be displaced by a preview.
                if (otherRank == _priorityRank.end() || otherRank->second.first != rank->second.first || otherRank->second.second <= rank->second.second)
                {
                    return false;
                }
            }
            for (const auto other : blockers)
            {
                const auto dropped = dropPendingPath(other);
                _retryTick.erase(other);
                clearApproachingSignals(other);
                if (!dropped.empty())
                {
                    if (auto* head = findHead(other))
                    {
                        World::Track::TrackConnections tc;
                        tc.connections.push_back(dropped.front().connection);
                        onPathWait(*head, dropped.front().pos, tc);
                    }
                }
            }
            return reservePath(owner, steps);
        }

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
        return specialSignalCount() > 0 || pathHoldCount() > 0 || hasCommittedRoutes();
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
                const auto piece = canonicalPiece(step.pos, step.connection);
                const auto info = classifyEntry(step.pos, step.connection);
                const auto own = holdAt(piece);
                const bool chainBusy = isChainSignal(info.kind) && !isPathSignal(info.kind) && info.facingOccupied
                    && !(own && own->owner == owner && own->kind == HoldKind::occupied);
                if (chainBusy || !canHoldInWorld(piece, owner))
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

    bool isRailPieceOccupied(const PieceKey& piece)
    {
        // No native vehicle can own the reservation-view sentinel.
        return pieceOccupiedByOther(piece, kNoReservationView);
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

    namespace
    {
        ReservedStep reversedStep(const ReservedStep& step)
        {
            const auto tad = step.connection & kBasicTaDMask;
            const auto& size = World::TrackData::getUnkTrack(tad);
            auto pos = step.pos + size.pos;
            if (size.rotationEnd < 12)
            {
                pos -= World::Pos3{ World::kRotationOffset[size.rotationEnd], 0 };
            }
            return { pos, static_cast<uint16_t>(tad ^ (1U << 2)) };
        }

        bool stationUsesPathSignals(const Vehicles::VehicleHead& head, const ReservedStep& origin)
        {
            if (isInPathMode(enumValue(head.id)))
            {
                return true;
            }
            if (specialSignalCount() == 0)
            {
                return false;
            }
            // Inspect only this unsignalled section, stopping at its boundary
            // posts. A special post in an unrelated block has no effect.
            std::deque<ReservedStep> queue{ origin, reversedStep(origin) };
            std::set<std::pair<PieceKey, uint8_t>> visited;
            while (!queue.empty() && visited.size() < kMaxBlockPieces)
            {
                const auto step = queue.front();
                queue.pop_front();
                const auto side = facingSide(step.pos, step.connection);
                if (!visited.emplace(side.piece, side.side).second)
                {
                    continue;
                }
                bool boundary = false;
                for (uint8_t index = 0; index < 2; ++index)
                {
                    if (!hasSignalSide(side.piece, index))
                    {
                        continue;
                    }
                    if (getSignalKind({ side.piece, index }) != SignalKind::none)
                    {
                        return true;
                    }
                    boundary = true;
                }
                if (boundary)
                {
                    continue;
                }
                const auto end = World::Track::getTrackConnectionEnd(step.pos, step.connection & kBasicTaDMask);
                const auto tc = World::Track::getTrackConnections(end.nextPos, end.nextRotation, head.owner, head.trackType, head.var_53, 0);
                for (const auto next : tc.connections)
                {
                    queue.push_back({ end.nextPos, next });
                }
            }
            return false;
        }
    }

    bool stationDepartureNeedsReverse(Vehicles::VehicleHead& head)
    {
        if (head.mode != TransportMode::rail || head.tileX == -1 || !isRailLogicActive())
        {
            return false;
        }
        const auto owner = enumValue(head.id);
        if (const auto retry = _retryTick.find(owner); retry != _retryTick.end() && ScenarioManager::getScenarioTicks() < retry->second)
        {
            return false;
        }
        Vehicles::Vehicle train(head);
        const auto end = World::Track::getTrackConnectionEnd(head.getTrackLoc(), head.trackAndDirection.track._data);
        const auto tc = World::Track::getTrackConnections(end.nextPos, end.nextRotation, head.owner, head.trackType, head.var_53, train.veh1->var_49);
        if (!tc.connections.empty())
        {
            return false;
        }
        const ReservedStep origin{ train.veh2->getTrackLoc(), train.veh2->trackAndDirection.track._data };
        if (!stationUsesPathSignals(head, origin))
        {
            return false;
        }
        const auto reversed = reversedStep(origin);
        WorldTrackGraph graph(head);
        SearchRequest request;
        request.mode = SearchMode::extend;
        request.hasTarget = graph.hasTarget();
        request.requireTarget = true;
        auto first = graph.makeEdge(reversed.pos, reversed.connection, StationId::null);
        first.signal = EntrySignal::none;
        request.starts.push_back(first);
        return findReservation(graph, request).found;
    }

    bool ensureStationDeparture(Vehicles::VehicleHead& head)
    {
        const auto owner = enumValue(head.id);
        setStationDepartureWaiting(owner, false);
        if (head.mode != TransportMode::rail || head.tileX == -1 || !isRailLogicActive())
        {
            return true;
        }
        Vehicles::Vehicle train(head);
        auto& front = *train.veh2;
        const auto pos = front.getTrackLoc();
        const auto tad = front.trackAndDirection.track._data;
        if (((tad >> 3) & 0x3F) >= kTrackIdCount)
        {
            return false;
        }
        const auto tile = World::TileManager::get(pos);
        const auto* station = tile.trainStation((tad >> 3) & 0x3F, tad & 3, front.tileBaseZ);
        if ((!station || station->isGhost() || station->isAiAllocated()) && head.stationId == StationId::null && !isInPathMode(owner))
        {
            return true;
        }
        const ReservedStep origin{ pos, tad };
        if (!stationUsesPathSignals(head, origin))
        {
            return true;
        }
        const auto now = ScenarioManager::getScenarioTicks();
        if (const auto retry = _retryTick.find(owner); retry != _retryTick.end() && now < retry->second)
        {
            setStationDepartureWaiting(owner, true);
            return false;
        }

        std::vector<ReservedStep> committed;
        auto at = pos;
        for (const auto handle : Vehicles::RoutingManager::RingView(front.routingHandle))
        {
            const auto routing = Vehicles::RoutingManager::getRouting(handle);
            if (routing == Vehicles::RoutingManager::kAllocatedButFreeRouting || routing == Vehicles::RoutingManager::kRoutingNull)
            {
                break;
            }
            if (((routing >> 3) & 0x3F) >= kTrackIdCount)
            {
                break;
            }
            committed.push_back({ at, routing });
            at += World::TrackData::getUnkTrack(routing & kBasicTaDMask).pos;
            if (handle == head.routingHandle)
            {
                break;
            }
        }
        if (committed.empty())
        {
            committed.push_back(origin);
        }

        WorldTrackGraph graph(head, &committed);
        SearchRequest request;
        request.mode = SearchMode::extend;
        request.hasTarget = graph.hasTarget();
        request.requireTarget = graph.requiresTarget();
        auto first = graph.makeEdge(pos, tad, station ? station->stationId() : StationId::null);
        first.signal = EntrySignal::none; // Already standing on this piece.
        request.starts.push_back(first);
        auto result = findReservation(graph, request);
        if (!result.found && _priorityRank.contains(owner))
        {
            WorldTrackGraph preview(head, &committed, true);
            request.starts.front() = preview.makeEdge(pos, tad, StationId::null);
            request.starts.front().signal = EntrySignal::none;
            result = findReservation(preview, request);
        }
        const auto refuse = [&] {
            setStationDepartureWaiting(owner, true);
            _retryTick[owner] = now + kRetryTicks;
            World::Track::TrackConnections tc;
            tc.connections.push_back(tad);
            onPathWait(head, pos, tc);
            return false;
        };
        if (!result.found || !result.safeEndpoint || result.reservation.size() < committed.size())
        {
            return refuse();
        }
        std::vector<ReservedStep> continuation;
        for (size_t i = 0; i < result.reservation.size(); ++i)
        {
            const auto& edge = result.reservation[i];
            const auto piece = canonicalPiece(edge.step.pos, edge.connection);
            if (!canHoldIgnoringPending(piece, owner) || pieceOccupiedByOther(piece, owner))
            {
                return refuse();
            }
            // Routing slots up to the planning head are already occupied holds;
            // pending consumption starts at the first uncommitted slot.
            if (i < committed.size())
            {
                continue;
            }
            continuation.push_back({ edge.step.pos, edge.connection });
        }
        if (!reserveWithPriority(owner, continuation))
        {
            return refuse();
        }
        // The train is already inside this section. Its unused continuation
        // must not yield while acceleration/planning resumes after loading.
        commitPendingSection(owner);
        for (const auto& step : committed)
        {
            holdOccupied(owner, canonicalPiece(step.pos, step.connection), true);
        }
        _retryTick.erase(owner);
        clearWaitingSignals(owner);
        clearApproachingSignals(owner);
        return true;
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
        if (!graph.hasTarget())
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
                request.hasTarget = graph.hasTarget();
                request.requireTarget = graph.requiresTarget();
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
                    request.hasTarget = graph.hasTarget();
                    request.requireTarget = graph.requiresTarget();
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
        const auto owner = enumValue(head.id);
        if (Vehicles::Vehicle(head).veh2->currentSpeed == 0.0_mph)
        {
            _signalWaitSince.try_emplace(owner, ScenarioManager::getScenarioTicks());
        }
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

        auto result = findReservation(graph, request);
        if (!result.found && _priorityRank.contains(owner))
        {
            WorldTrackGraph preview(head, nullptr, true);
            request.starts.clear();
            for (const auto connection : tc.connections)
            {
                request.starts.push_back(preview.makeEdge(nextPos, connection, tc.stationId));
            }
            result = findReservation(preview, request);
        }
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
        for (const auto& step : steps)
        {
            if (pieceOccupiedByOther(canonicalPiece(step.pos, step.connection), owner))
            {
                _retryTick[owner] = now + kRetryTicks;
                onPathWait(head, nextPos, tc);
                return std::nullopt;
            }
        }
        if (!reserveWithPriority(owner, steps))
        {
            _retryTick[owner] = now + kRetryTicks;
            onPathWait(head, nextPos, tc);
            return std::nullopt;
        }
        _retryTick.erase(owner);
        clearWaitingSignals(owner);
        clearApproachingSignals(owner);
        return result.reservation.front().connection;
    }

    bool pieceHeldByProtectedOther(const Vehicles::VehicleHead& head, const World::Pos3& entrance, uint16_t connection)
    {
        return isHeldByProtectedOther(canonicalPiece(entrance, connection), enumValue(head.id));
    }

    bool blockHasForeignPath(const Vehicles::VehicleHead& head, const World::Pos3& entrance, uint16_t connection)
    {
        if (holdCount() == 0)
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
        _signalWaitSince.erase(owner);
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
        setStationDepartureWaiting(owner, false);
        clearTrainPriority(owner);
        _retryTick.erase(owner);
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
        _signalWaitSince.clear();
        _priorityRank.clear();
        _priorityLottery.clear();
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

    uint32_t priorityTick() { return ScenarioManager::getScenarioTicks(); }

    std::optional<uint64_t> trainPriorityIdentity(uint16_t owner)
    {
        if (EntityManager::getListCount(EntityManager::EntityListType::vehicleHead) == 0)
        {
            return std::nullopt;
        }
        auto* head = findHead(owner);
        if (!head)
        {
            return std::nullopt;
        }
        Vehicles::Vehicle train(*head);
        uint64_t hash = 14695981039346656037ULL;
        const auto add = [&](uint64_t value) {
            for (int i = 0; i < 8; ++i)
            {
                hash ^= static_cast<uint8_t>(value);
                hash *= 1099511628211ULL;
                value >>= 8;
            }
        };
        add(owner);
        add(enumValue(head->owner));
        add(train.veh1->dayCreated);
        add(head->ordinalNumber);
        add(head->name);
        add(head->routingHandle._data);
        add(head->tileX);
        add(head->tileY);
        add(head->tileBaseZ);
        add(head->trackAndDirection.track._data);
        add(train.veh2->tileX);
        add(train.veh2->tileY);
        add(train.veh2->tileBaseZ);
        add(head->currentOrder);
        add(head->sizeOfOrderTable);
        for (const auto& order : head->getCurrentOrders())
        {
            add(order.getRaw());
        }
        return hash == 0 ? 1 : hash;
    }

    std::vector<SavedPriority> capturePriorities()
    {
        std::vector<SavedPriority> result;
        if (EntityManager::getListCount(EntityManager::EntityListType::vehicleHead) == 0)
        {
            return result;
        }
        const auto now = priorityTick();
        for (auto* head : VehicleManager::VehicleList())
        {
            const auto owner = enumValue(head->id);
            const auto identity = trainPriorityIdentity(owner);
            if (!identity)
            {
                continue;
            }
            const auto wait = _signalWaitSince.find(owner), lottery = _priorityLottery.find(owner);
            const bool departing = isStationDepartureWaiting(owner);
            if (wait == _signalWaitSince.end() && lottery == _priorityLottery.end() && !departing)
            {
                continue;
            }
            result.push_back({ owner, *identity, wait != _signalWaitSince.end(), wait == _signalWaitSince.end() ? 0U : now - wait->second, lottery != _priorityLottery.end(), lottery == _priorityLottery.end() ? 0U : lottery->second, departing });
        }
        return result;
    }

    bool restorePriorities(const std::vector<SavedPriority>& records)
    {
        for (const auto& record : records)
        {
            const auto identity = trainPriorityIdentity(record.owner);
            if (!identity || *identity != record.identity)
            {
                return false;
            }
        }
        clearReservationPriorities();
        const auto now = priorityTick();
        for (const auto& record : records)
        {
            if (record.waiting)
            {
                _signalWaitSince[record.owner] = now - record.waitedTicks;
                _restoredWaiting.insert(record.owner);
            }
            if (record.hasLottery)
            {
                _priorityLottery[record.owner] = record.lottery;
            }
            setStationDepartureWaiting(record.owner, record.departureWaiting);
            if (record.waiting)
            {
                auto* head = findHead(record.owner);
                Vehicles::Vehicle train(*head);
                const auto tad = head->trackAndDirection.track._data & kBasicTaDMask;
                if ((tad >> 3) < kTrackIdCount)
                {
                    const auto end = World::Track::getTrackConnectionEnd(head->getTrackLoc(), tad);
                    const auto tc = World::Track::getTrackConnections(end.nextPos, end.nextRotation, head->owner, head->trackType, head->var_53, train.veh1->var_49);
                    onPathWait(*head, end.nextPos, tc);
                }
            }
        }
        return true;
    }

    void clearReservationPriorities()
    {
        _signalWaitSince.clear();
        _priorityRank.clear();
        _priorityLottery.clear();
        _restoredWaiting.clear();
    }

    void clearTrainPriority(uint16_t owner)
    {
        _signalWaitSince.erase(owner);
        _priorityLottery.erase(owner);
        _restoredWaiting.erase(owner);
        _priorityRank.erase(owner);
        _retryTick.erase(owner);
    }

    void prioritiseReservations(std::vector<Vehicles::VehicleHead*>& vehicles)
    {
        _priorityRank.clear();
        const auto now = ScenarioManager::getScenarioTicks();
        std::set<uint16_t> live;
        std::vector<PriorityCandidate> candidates;
        for (auto* head : vehicles)
        {
            const auto owner = enumValue(head->id);
            if (head->mode != TransportMode::rail || !head->isPlaced() || head->hasVehicleFlags(Vehicles::VehicleFlags::commandStop)
                || head->status == Vehicles::Status::crashed || head->status == Vehicles::Status::brokenDown)
            {
                continue;
            }
            live.insert(owner);
            Vehicles::Vehicle train(*head);
            const auto* pending = pendingPath(owner);
            const bool departing = isStationDepartureWaiting(owner);
            // Keep a just-granted stopped departure in its queue position until
            // it starts moving/committing; otherwise two platforms can swap
            // unused grants every tick without either train getting underway.
            const bool waiting = train.veh2->currentSpeed == 0.0_mph && (departing || _restoredWaiting.contains(owner) || head->status == Vehicles::Status::waitingAtSignal || isTrainWaiting(owner) || (_signalWaitSince.contains(owner) && pending && !pending->empty()));
            if (!waiting)
            {
                _signalWaitSince.erase(owner);
            }
            else
            {
                _signalWaitSince.try_emplace(owner, now);
            }
            // A loading/full-load/timetable stop is not an active contender.
            if ((head->status == Vehicles::Status::loading || head->status == Vehicles::Status::unloading) && !departing && (!pending || pending->empty()))
            {
                continue;
            }
            World::Track::TrackConnections tc;
            World::Pos3 pos;
            SearchRequest request;
            request.mode = SearchMode::pathSignal;
            std::vector<ReservedStep> committed;
            if (departing)
            {
                pos = train.veh2->getTrackLoc();
                auto at = pos;
                for (const auto handle : Vehicles::RoutingManager::RingView(train.veh2->routingHandle))
                {
                    const auto routing = Vehicles::RoutingManager::getRouting(handle);
                    if (routing == Vehicles::RoutingManager::kAllocatedButFreeRouting || routing == Vehicles::RoutingManager::kRoutingNull || ((routing >> 3) & 0x3F) >= kTrackIdCount)
                    {
                        break;
                    }
                    committed.push_back({ at, routing });
                    at += World::TrackData::getUnkTrack(routing & kBasicTaDMask).pos;
                    if (handle == head->routingHandle)
                    {
                        break;
                    }
                }
                tc.connections.push_back(train.veh2->trackAndDirection.track._data);
                request.mode = SearchMode::extend;
            }
            else if (pending && !pending->empty())
            {
                pos = pending->front().pos;
                tc.connections.push_back(pending->front().connection);
                request.mode = SearchMode::extend;
            }
            else
            {
                const auto tad = head->trackAndDirection.track._data & kBasicTaDMask;
                if ((tad >> 3) >= kTrackIdCount)
                {
                    continue;
                }
                const auto end = World::Track::getTrackConnectionEnd(head->getTrackLoc(), tad);
                pos = end.nextPos;
                tc = World::Track::getTrackConnections(pos, end.nextRotation, head->owner, head->trackType, head->var_53, train.veh1->var_49);
                const auto mode = reservationStartAtJunction(*head, pos, tc);
                if (!mode)
                {
                    // Only schedule a normal signal request here. Admission
                    // still uses native whole-block occupancy in VehicleHead.
                    const bool nativeBlock = std::any_of(tc.connections.begin(), tc.connections.end(), [&](auto connection) {
                        const auto info = classifyEntry(pos, connection);
                        return info.hasFacingSignal && info.kind == SignalKind::none;
                    });
                    if (!nativeBlock)
                    {
                        continue;
                    }
                }
                request.mode = mode && *mode == ReserveStart::extend ? SearchMode::extend : SearchMode::pathSignal;
            }
            WorldTrackGraph graph(*head, departing ? &committed : nullptr, true);
            request.hasTarget = graph.hasTarget();
            request.requireTarget = graph.requiresTarget();
            SearchResult result;
            if (!departing && pending && !pending->empty())
            {
                // This is only a contention footprint, never permission to move.
                // Reuse the already chosen pending route instead of searching it
                // again every tick; actual entry/acquisition still revalidates
                // physical occupation, committed holds and the required orders.
                result.found = result.safeEndpoint = true;
                for (const auto& step : *pending)
                {
                    result.reservation.push_back(graph.describeStep(step.pos, step.connection));
                }
            }
            else
            {
                for (auto connection : tc.connections)
                {
                    request.starts.push_back(graph.makeEdge(pos, connection, StationId::null));
                }
                if (departing && !request.starts.empty())
                {
                    request.starts.front().signal = EntrySignal::none;
                }
                result = findReservation(graph, request);
            }
            if (!result.found || !result.safeEndpoint)
            {
                continue;
            }
            PriorityCandidate candidate{ head, {}, (train.veh1->var_48 & Vehicles::Flags48::expressMode) != Vehicles::Flags48::none, waiting, waiting ? now - _signalWaitSince.at(owner) : 0, 0, {}, { INT16_MAX, INT16_MAX }, { INT16_MIN, INT16_MIN } };
            bool foundTarget = false;
            for (const auto& edge : result.reservation)
            {
                candidate.steps.push_back({ edge.step.pos, edge.connection });
                const auto piece = canonicalPiece(edge.step.pos, edge.connection);
                candidate.pieces.push_back(piece);
                for (const auto& part : World::TrackData::getTrackPiece(piece.trackId))
                {
                    const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, piece.rotation);
                    const World::Pos2 tile{ static_cast<int16_t>(piece.x + offset.x), static_cast<int16_t>(piece.y + offset.y) };
                    candidate.minimum.x = std::min(candidate.minimum.x, tile.x);
                    candidate.minimum.y = std::min(candidate.minimum.y, tile.y);
                    candidate.maximum.x = std::max(candidate.maximum.x, tile.x);
                    candidate.maximum.y = std::max(candidate.maximum.y, tile.y);
                }
                if (!foundTarget)
                {
                    candidate.distance += edge.step.pos == train.veh2->getTrackLoc() && edge.step.tad == (train.veh2->trackAndDirection.track._data & kBasicTaDMask)
                        ? remainingPieceCost(*train.veh2, edge.step.tad, edge.cost)
                        : uint64_t{ edge.cost } << 16;
                }
                foundTarget |= edge.isTarget;
            }
            if (!departing)
            {
                candidate.distance += approachCost(*head, candidate.steps.front());
            }
            candidates.push_back(std::move(candidate));
        }
        _restoredWaiting.clear();
        std::erase_if(_signalWaitSince, [&](const auto& item) { return !live.contains(item.first); });
        // Components let unrelated road/rail services retain their update slots.
        std::vector<size_t> component(candidates.size());
        std::iota(component.begin(), component.end(), size_t{ 0 });
        auto root = [&](size_t i) { while (component[i] != i){ i = component[i];
} return i; };
        std::set<uint16_t> tied;
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            for (size_t j = i + 1; j < candidates.size(); ++j)
            {
                if (!routesConflict(candidates[i], candidates[j]))
                {
                    continue;
                }
                component[root(j)] = root(i);
                if (candidates[i].key() == candidates[j].key())
                {
                    tied.insert(enumValue(candidates[i].head->id));
                    tied.insert(enumValue(candidates[j].head->id));
                }
            }
        }
        std::erase_if(_priorityLottery, [&](const auto& item) { return !tied.contains(item.first); });
        // Draw in stable owner order only for an exact, actual conflict. Retain
        // the lottery through retries so equal trains cannot alternate winners.
        for (auto owner : tied)
        {
            if (!_priorityLottery.contains(owner))
            {
                _priorityLottery.emplace(owner, gPrng1().randNext());
            }
        }
        std::map<size_t, std::vector<size_t>> groups;
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            groups[root(i)].push_back(i);
        }
        for (auto& [group, indices] : groups)
        {
            (void)group;
            if (indices.size() < 2)
            {
                continue;
            }
            std::sort(indices.begin(), indices.end(), [&](auto a, auto b) {
                const auto& x = candidates[a];
                const auto& y = candidates[b];
                const auto xo = enumValue(x.head->id), yo = enumValue(y.head->id);
                const auto lottery = [&](uint16_t id) { const auto it = _priorityLottery.find(id); return it == _priorityLottery.end() ? 0U : it->second; };
                return std::tuple{ x.key(), lottery(xo), xo } < std::tuple{ y.key(), lottery(yo), yo };
            });
            _retryTick.erase(enumValue(candidates[indices.front()].head->id));
            std::vector<size_t> slots;
            for (auto i : indices)
            {
                const auto found = std::find(vehicles.begin(), vehicles.end(), candidates[i].head);
                slots.push_back(static_cast<size_t>(found - vehicles.begin()));
            }
            std::sort(slots.begin(), slots.end());
            for (size_t i = 0; i < indices.size(); ++i)
            {
                auto* head = candidates[indices[i]].head;
                _priorityRank[enumValue(head->id)] = { group, i };
                vehicles[slots[i]] = head;
            }
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
