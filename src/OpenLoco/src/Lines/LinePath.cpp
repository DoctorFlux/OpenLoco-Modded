#include "Entities/EntityManager.h"
#include "Lines/Line.h"
#include "Rules/Rules.h"
#include "Vehicles/VehicleHead.h"

#include "Graphics/Colour.h"
#include "Graphics/DrawingContext.h"
#include "Graphics/Gfx.h"
#include "Graphics/ImageIds.h"
#include "Graphics/TextRenderer.h"
#include "Localisation/Formatting.h"
#include "Map/RoadElement.h"
#include "Map/SurfaceElement.h"
#include "Map/TileManager.h"
#include "Map/Track/SubpositionData.h"
#include "Map/Track/Track.h"
#include "Map/Track/TrackData.h"
#include "Map/TrackElement.h"
#include "Objects/CargoObject.h"
#include "Objects/ObjectManager.h"
#include "Objects/RoadObject.h"
#include "Vehicles/Orders.h"
#include "Vehicles/Vehicle.h"
#include "Viewport.hpp"
#include "World/CompanyManager.h"
#include "World/Station.h"
#include "World/StationManager.h"
#include <OpenLoco/Math/Vector.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <map>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace OpenLoco::Lines
{
    size_t loopLegs(size_t stopCount, RouteLeg* legs, size_t capacity)
    {
        if (legs == nullptr || stopCount < 2 || capacity == 0)
        {
            return 0;
        }
        size_t count = 0;
        for (size_t i = 0; i < stopCount && count < capacity; ++i)
        {
            legs[count++] = { i, (i + 1) % stopCount };
        }
        return count;
    }

    uint8_t chosenOverlayColour(const OverlayClaim* claims, size_t count, uint16_t selectedLine)
    {
        uint8_t fallback = 0;
        bool have = false;
        if (claims == nullptr)
        {
            return 0;
        }
        for (size_t i = 0; i < count; ++i)
        {
            if (claims[i].lineId == selectedLine)
            {
                return claims[i].colour;
            }
            if (!have)
            {
                fallback = claims[i].colour;
                have = true;
            }
        }
        return fallback;
    }

    namespace
    {
        constexpr size_t kMaxSearchNodes = 4096;
        constexpr size_t kMaxTrackPieces = 44;
        constexpr size_t kMaxRoadPieces = 10;
        constexpr uint8_t kKindTrack = 0;
        constexpr uint8_t kKindRoad = 1;
        constexpr uint8_t kKindSurface = 2;

        struct Overlay
        {
            uint8_t colour = 0;
            bool selected = false;
        };

        struct PieceKey
        {
            int32_t x = 0;
            int32_t y = 0;
            uint8_t baseZ = 0;
            uint8_t pieceId = 0;
            uint8_t rotation = 0;
            uint8_t kind = 0;
        };

        uint64_t packKey(const PieceKey& key)
        {
            return static_cast<uint64_t>(static_cast<uint16_t>(key.x))
                | (static_cast<uint64_t>(static_cast<uint16_t>(key.y)) << 16)
                | (static_cast<uint64_t>(key.baseZ) << 32)
                | (static_cast<uint64_t>(key.pieceId) << 40)
                | (static_cast<uint64_t>(key.rotation) << 48)
                | (static_cast<uint64_t>(key.kind) << 56);
        }

        uint64_t packSurface(int32_t x, int32_t y)
        {
            const auto tileX = x & ~31;
            const auto tileY = y & ~31;
            return packKey({ tileX, tileY, 0, 0, 0, kKindSurface });
        }

        struct RoutePoint
        {
            int32_t x = 0;
            int32_t y = 0;
            int32_t z = 0;
            int32_t tileX = 0;
            int32_t tileY = 0;
            bool breakBefore = false;
            uint64_t piece = 0;
        };

        struct Traversal
        {
            uint16_t line = 0;
            int16_t x = 0;
            int16_t y = 0;
            int16_t z = 0;
            uint16_t tad = 0;

            bool operator==(const Traversal& other) const
            {
                return line == other.line && x == other.x && y == other.y && z == other.z && tad == other.tad;
            }
        };

        struct TraversalHash
        {
            size_t operator()(const Traversal& traversal) const
            {
                return static_cast<size_t>(traversal.line) ^ (static_cast<size_t>(static_cast<uint16_t>(traversal.x)) << 16) ^ (static_cast<size_t>(static_cast<uint16_t>(traversal.y)) << 1) ^ (static_cast<size_t>(traversal.tad) << 8) ^ static_cast<size_t>(static_cast<uint16_t>(traversal.z));
            }
        };

        struct ShownRoute
        {
            uint16_t lineId = 0;
            uint8_t colour = 0;
            std::vector<RoutePoint> points;
        };

        struct StopMarker
        {
            int32_t x = 0;
            int32_t y = 0;
            int32_t z = 0;
            uint8_t number = 0;
            uint8_t lineNumber = 0;
            std::string label;
        };

        std::unordered_map<uint64_t, Overlay> _overlay;
        std::vector<ShownRoute> _routes;
        std::vector<StopMarker> _markers;
        std::vector<RoutePoint>* _recording = nullptr;
        uint16_t _recordingLine = 0;
        bool _breakNext = false;
        std::unordered_set<Traversal, TraversalHash> _usedTraversals;
        uint16_t _selectedLine = 0;
        bool _showSelected = false;
        bool _showAll = false;
        bool _dirty = true;
        uint16_t _previewVehicle = UINT16_MAX;
        uint32_t _viewUpdates = 0;
        uint16_t _pollCursor = 0, _buildCursor = 0;
        constexpr size_t kMaxCachedLegs = 128, kMaxCachedUnits = 131072;
        constexpr size_t kMaxLegPoints = 32768;
        struct LegKey
        {
            uint16_t line = 0;
            uint64_t from = 0, to = 0;
            auto operator<=>(const LegKey&) const = default;
        };
        struct CachedLeg
        {
            std::vector<RoutePoint> points;
            std::unordered_set<uint64_t> pieces;
            uint32_t used = 0;
            bool limited = false;
        };
        struct LivePlan
        {
            struct Published
            {
                std::vector<LegKey> legs;
                ShownRoute route;
                std::unordered_set<uint64_t> pieces;
                size_t legCount = 0;
                bool limited = false;
            };
            uint16_t vehicle = UINT16_MAX;
            std::vector<LegKey> legs;
            Rules::RoutePreview preview;
            bool refresh = true;
            std::optional<Published> published;
            bool capacityLimited = false;
            bool finishLimited = false;
        };
        std::map<LegKey, CachedLeg> _legCache;
        std::map<uint16_t, LivePlan> _livePlans;
        std::unordered_map<uint64_t, Overlay> _liveOverlay;
        std::vector<ShownRoute> _liveRoutes;
        std::vector<uint16_t> _routeLineIds;
        std::unordered_set<uint64_t>* _recordingPieces = nullptr;
        uint64_t _pointPiece = 0;
        uint32_t _cacheClock = 0;
        size_t _cachedUnits = 0;
        RouteViewStats _viewStats;

        bool numberedStop(Vehicles::OrderType type)
        {
            return type == Vehicles::OrderType::StopAt || type == Vehicles::OrderType::RouteThrough || type == Vehicles::OrderType::RouteWaypoint;
        }

        struct StopMark
        {
            World::Pos2 centre{};
            int32_t height = 16;
            std::vector<World::Pos2> tiles;
            bool station = false;
            uint16_t stationId = 0;
            LineKind kinds = LineKind::none;
        };

        void addRoutePoint(int32_t x, int32_t y, int32_t z)
        {
            if (_recording == nullptr)
            {
                return;
            }
            if (_recordingPieces && _recording->size() >= kMaxLegPoints)
            {
                return;
            }
            RoutePoint point{ x, y, z, x & ~31, y & ~31, _breakNext, _pointPiece };
            _breakNext = false;
            if (!_recording->empty() && !point.breakBefore)
            {
                const auto& last = _recording->back();
                if (last.x == point.x && last.y == point.y)
                {
                    return;
                }
            }
            _recording->push_back(point);
        }

        void remember(const PieceKey& key, uint8_t colour, bool selected)
        {
            const auto packed = packKey(key);
            if (_recordingPieces)
            {
                if (_recordingPieces->size() < kMaxLegPoints)
                {
                    _recordingPieces->insert(packed);
                }
                return;
            }
            const auto found = _overlay.find(packed);
            if (found == _overlay.end() || (selected && !found->second.selected))
            {
                _overlay[packed] = { colour, selected };
            }
        }

        uint16_t plainTad(uint16_t tad, bool road)
        {
            return tad & (road ? World::Track::AdditionalTaDFlags::basicRaDMask : World::Track::AdditionalTaDFlags::basicTaDMask);
        }

        bool knownPiece(uint16_t tad, bool road)
        {
            const auto id = static_cast<size_t>((plainTad(tad, road) >> 3) & (road ? 0xF : 0x3F));
            return road ? id < kMaxRoadPieces : id < kMaxTrackPieces;
        }

        World::Track::ConnectionEnd pieceEnd(const World::Pos3& pos, uint16_t tad, bool road)
        {
            const auto plain = plainTad(tad, road);
            if (!knownPiece(plain, road))
            {
                return { pos, 0 };
            }
            return road ? World::Track::getRoadConnectionEnd(pos, plain) : World::Track::getTrackConnectionEnd(pos, plain);
        }

        bool findPlacementOrigin(const World::Pos3& entry, uint8_t pieceId, uint8_t rotation, bool road, World::Pos3& origin)
        {
            origin = entry;
            auto tile = World::TileManager::get(entry);
            if (tile.isNull())
            {
                return false;
            }
            const auto parts = road ? World::TrackData::getRoadPiece(pieceId) : World::TrackData::getTrackPiece(pieceId);
            for (auto& element : tile)
            {
                uint8_t sequence = 0;
                if (road)
                {
                    auto* roadElement = element.as<World::RoadElement>();
                    if (roadElement == nullptr || roadElement->isGhost() || roadElement->roadId() != pieceId || roadElement->rotation() != rotation)
                    {
                        continue;
                    }
                    sequence = roadElement->sequenceIndex();
                }
                else
                {
                    auto* trackElement = element.as<World::TrackElement>();
                    if (trackElement == nullptr || trackElement->isGhost() || trackElement->trackId() != pieceId || trackElement->rotation() != rotation)
                    {
                        continue;
                    }
                    sequence = trackElement->sequenceIndex();
                }
                if (sequence >= parts.size())
                {
                    continue;
                }
                const auto& part = parts[sequence];
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, rotation);
                origin = World::Pos3{
                    static_cast<coord_t>(entry.x - offset.x),
                    static_cast<coord_t>(entry.y - offset.y),
                    static_cast<coord_t>(entry.z - part.z),
                };
                return true;
            }
            return false;
        }

        size_t sampleVehiclePath(const World::Pos3& start, uint16_t plain, bool road, CurveSample* out, size_t capacity)
        {
            if (out == nullptr || capacity == 0)
            {
                return 0;
            }
            const auto moves = road ? World::TrackData::getRoadSubPositon(plain) : World::TrackData::getTrackSubPositon(plain);
            if (moves.empty())
            {
                return 0;
            }
            constexpr size_t kStride = 4;
            const auto last = moves.size() - 1;
            size_t count = 0;
            auto write = [&](size_t index) {
                if (count >= capacity)
                {
                    return;
                }
                const auto& move = moves[index];
                CurveSample sample{ start.x + move.loc.x, start.y + move.loc.y, start.z + move.loc.z };
                if (count > 0 && out[count - 1].x == sample.x && out[count - 1].y == sample.y && out[count - 1].z == sample.z)
                {
                    return;
                }
                out[count++] = sample;
            };
            write(0);
            for (size_t index = kStride; index < last && count + 1 < capacity; index += kStride)
            {
                write(index);
            }
            if (last != 0)
            {
                write(last);
            }
            return count;
        }

        bool wrongWaySignal(const World::Pos3& pos, uint16_t rawTad, uint8_t trackObjectId)
        {
            if (trackObjectId == 0xFF || (rawTad & World::Track::AdditionalTaDFlags::hasSignal) == 0)
            {
                return false;
            }
            const auto plain = rawTad & World::Track::AdditionalTaDFlags::basicTaDMask;
            Vehicles::TrackAndDirection::_TrackAndDirection direction{ static_cast<uint8_t>((plain >> 3) & 0x3F), static_cast<uint8_t>(plain & 0x3) };
            direction.setReversed((plain & (1U << 2)) != 0);
            const auto state = Vehicles::getSignalState(pos, direction, trackObjectId, 0);
            return (state & Vehicles::SignalStateFlags::blockedNoRoute) != Vehicles::SignalStateFlags::none;
        }

        void rememberPiece(const World::Pos3& start, uint16_t tad, bool road, uint8_t colour, bool selected)
        {
            const auto plain = plainTad(tad, road);
            if (!knownPiece(plain, road))
            {
                return;
            }
            const auto pieceId = static_cast<uint8_t>((plain >> 3) & (road ? 0xF : 0x3F));
            const auto rotation = static_cast<uint8_t>(plain & 0x3);
            World::Pos3 origin = start;
            if ((plain & (1U << 2)) != 0)
            {
                findPlacementOrigin(start, pieceId, rotation, road, origin);
            }
            const Traversal traversal{ _recordingLine, origin.x, origin.y, origin.z, static_cast<uint16_t>(plain & ~(1U << 2)) };
            _pointPiece = (uint64_t{ 1 } << 63) | packKey({ origin.x, origin.y, static_cast<uint8_t>(origin.z / World::kSmallZStep), pieceId, rotation, road ? kKindRoad : kKindTrack });
            if (!_usedTraversals.insert(traversal).second)
            {
                _breakNext = true;
                return;
            }
            const auto moves = road ? World::TrackData::getRoadSubPositon(plain) : World::TrackData::getTrackSubPositon(plain);
            if (!moves.empty())
            {
                constexpr size_t kStride = 4;
                const auto last = moves.size() - 1;
                auto addMove = [&](size_t index) {
                    const auto& move = moves[index];
                    addRoutePoint(start.x + move.loc.x, start.y + move.loc.y, start.z + move.loc.z);
                };
                addMove(0);
                for (size_t index = kStride; index < last; index += kStride)
                {
                    addMove(index);
                }
                if (last != 0)
                {
                    addMove(last);
                }
            }
            if ((plain & (1U << 2)) == 0 || origin.x != start.x || origin.y != start.y || origin.z != start.z)
            {
                const auto parts = road ? World::TrackData::getRoadPiece(pieceId) : World::TrackData::getTrackPiece(pieceId);
                for (const auto& part : parts)
                {
                    const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, rotation);
                    const auto z = origin.z + part.z;
                    if (z < 0)
                    {
                        continue;
                    }
                    remember({ origin.x + offset.x, origin.y + offset.y, static_cast<uint8_t>(z / World::kSmallZStep), pieceId, rotation, road ? kKindRoad : kKindTrack }, colour, selected);
                }
            }
        }

        void rememberSurface(const World::Pos2& pos, uint8_t colour, bool selected)
        {
            _pointPiece = 0;
            remember({ pos.x & ~31, pos.y & ~31, 0, 0, 0, kKindSurface }, colour, selected);
            addRoutePoint((pos.x & ~31) + 16, (pos.y & ~31) + 16, 16);
        }

        bool hits(const World::Pos3& start, uint8_t pieceId, uint8_t rotation, bool road, const std::vector<World::Pos2>& tiles)
        {
            if ((road && pieceId >= kMaxRoadPieces) || (!road && pieceId >= kMaxTrackPieces))
            {
                return false;
            }
            const auto parts = road ? World::TrackData::getRoadPiece(pieceId) : World::TrackData::getTrackPiece(pieceId);
            for (const auto& part : parts)
            {
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, rotation);
                const World::Pos2 tile{ static_cast<coord_t>(start.x + offset.x), static_cast<coord_t>(start.y + offset.y) };
                for (const auto& target : tiles)
                {
                    if (tile.x == target.x && tile.y == target.y)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        bool roadPieceAllowed(const World::Pos3& pos, uint16_t tad, LineKind kinds)
        {
            if (kinds == kEveryLineKind || ((kinds & LineKind::road) != LineKind::none && (kinds & LineKind::tram) != LineKind::none) || kinds == LineKind::none)
            {
                return true;
            }
            const auto roadId = static_cast<uint8_t>((tad >> 3) & 0xF);
            const auto rotation = static_cast<uint8_t>(tad & 0x3);
            auto tile = World::TileManager::get(pos);
            if (tile.isNull())
            {
                return true;
            }
            for (auto& entry : tile)
            {
                auto* road = entry.as<World::RoadElement>();
                if (road == nullptr || road->roadId() != roadId || road->rotation() != rotation)
                {
                    continue;
                }
                const auto* object = ObjectManager::get<RoadObject>(road->roadObjectId());
                if (object == nullptr)
                {
                    return true;
                }
                const auto tram = !object->hasFlags(RoadObjectFlags::isRoad);
                if (tram)
                {
                    return (kinds & LineKind::tram) != LineKind::none;
                }
                return (kinds & LineKind::road) != LineKind::none;
            }
            return true;
        }

        bool roadAllowsReverse(const World::Pos3& pos, uint16_t tad)
        {
            const auto roadId = static_cast<uint8_t>((tad >> 3) & 0xF);
            const auto rotation = static_cast<uint8_t>(tad & 0x3);
            auto tile = World::TileManager::get(pos);
            if (tile.isNull())
            {
                return true;
            }
            for (auto& entry : tile)
            {
                auto* roadElement = entry.as<World::RoadElement>();
                if (roadElement == nullptr || roadElement->roadId() != roadId || roadElement->rotation() != rotation)
                {
                    continue;
                }
                const auto* object = ObjectManager::get<RoadObject>(roadElement->roadObjectId());
                return object == nullptr || !object->hasFlags(RoadObjectFlags::isOneWay);
            }
            return true;
        }

        struct SearchStep
        {
            World::Pos3 pos{};
            uint16_t tad = 0;
            int parent = -1;
            int32_t cost = 0;
        };

        uint64_t stepKey(const World::Pos3& pos, uint16_t tad)
        {
            return static_cast<uint64_t>(static_cast<uint16_t>(pos.x))
                | (static_cast<uint64_t>(static_cast<uint16_t>(pos.y)) << 16)
                | (static_cast<uint64_t>(static_cast<uint16_t>(pos.z)) << 32)
                | (static_cast<uint64_t>(tad) << 48);
        }

        void collectConnections(const World::Pos3& pos, uint8_t rotation, bool road, CompanyId company, uint8_t trackObjectId, std::vector<uint16_t>& out)
        {
            out.clear();
            auto append = [&](const auto& list) {
                for (const auto tad : list)
                {
                    const auto plain = plainTad(tad, road);
                    if (knownPiece(plain, road))
                    {
                        out.push_back(plain);
                    }
                }
            };
            if (road)
            {
                append(World::Track::getRoadConnectionsOneWay(pos, rotation, company, 0xFF, 0, 0).connections);
            }
            else if (trackObjectId != 0xFF)
            {
                const auto found = World::Track::getTrackConnections(pos, rotation, company, trackObjectId, 0, 0);
                for (const auto raw : found.connections)
                {
                    if (!wrongWaySignal(pos, raw, trackObjectId))
                    {
                        append(std::array<uint16_t, 1>{ raw });
                    }
                }
            }
            if (out.empty())
            {
                auto tile = World::TileManager::get(pos);
                if (!tile.isNull())
                {
                    for (auto& entry : tile)
                    {
                        if (road)
                        {
                            auto* element = entry.as<World::RoadElement>();
                            if (element == nullptr || element->isGhost() || element->isAiAllocated())
                            {
                                continue;
                            }
                            append(World::Track::getRoadConnectionsOneWay(pos, rotation, element->owner(), element->roadObjectId(), 0, 0).connections);
                        }
                        else
                        {
                            auto* element = entry.as<World::TrackElement>();
                            if (element == nullptr || element->isGhost() || element->isAiAllocated())
                            {
                                continue;
                            }
                            const auto found = World::Track::getTrackConnections(pos, rotation, element->owner(), element->trackObjectId(), 0, 0);
                            for (const auto raw : found.connections)
                            {
                                if (!wrongWaySignal(pos, raw, element->trackObjectId()))
                                {
                                    append(std::array<uint16_t, 1>{ raw });
                                }
                            }
                        }
                    }
                }
            }
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
        }

        bool pieceTouches(const World::Pos3& entry, uint16_t tad, bool road, const std::vector<World::Pos2>& tiles)
        {
            const auto plain = plainTad(tad, road);
            if (!knownPiece(plain, road) || tiles.empty())
            {
                return false;
            }
            const auto pieceId = static_cast<uint8_t>((plain >> 3) & (road ? 0xF : 0x3F));
            const auto rotation = static_cast<uint8_t>(plain & 0x3);
            World::Pos3 origin = entry;
            if ((plain & (1U << 2)) == 0 || findPlacementOrigin(entry, pieceId, rotation, road, origin))
            {
                if (hits(origin, pieceId, rotation, road, tiles))
                {
                    return true;
                }
            }
            const auto end = pieceEnd(entry, plain, road);
            const World::Pos2 entryTile{ static_cast<coord_t>(entry.x & ~31), static_cast<coord_t>(entry.y & ~31) };
            const World::Pos2 exitTile{ static_cast<coord_t>(end.nextPos.x & ~31), static_cast<coord_t>(end.nextPos.y & ~31) };
            for (const auto& tile : tiles)
            {
                if ((tile.x == entryTile.x && tile.y == entryTile.y) || (tile.x == exitTile.x && tile.y == exitTile.y))
                {
                    return true;
                }
            }
            return false;
        }

        bool searchTrack(const StopMark& from, const StopMark& to, CompanyId company, uint8_t trackObjectId, bool road, LineKind kinds, uint8_t colour, bool selected)
        {
            if (from.tiles.empty() || to.tiles.empty() || (!road && trackObjectId == 0xFF))
            {
                return false;
            }
            std::vector<SearchStep> steps;
            std::priority_queue<std::pair<int32_t, int>, std::vector<std::pair<int32_t, int>>, std::greater<>> open;
            std::unordered_map<uint64_t, int32_t> bestCost;
            steps.reserve(256);

            auto push = [&](const World::Pos3& pos, uint16_t tad, int parent, int32_t cost) {
                if (steps.size() >= kMaxSearchNodes)
                {
                    return;
                }
                if (road && !roadPieceAllowed(pos, tad, kinds))
                {
                    return;
                }
                const auto key = stepKey(pos, tad);
                const auto found = bestCost.find(key);
                if (found != bestCost.end() && found->second <= cost)
                {
                    return;
                }
                bestCost[key] = cost;
                steps.push_back({ pos, tad, parent, cost });
                open.push({ cost, static_cast<int>(steps.size() - 1) });
            };

            auto considerTrack = [&](const World::Pos2& tilePos, World::TrackElement* element) {
                const auto pieceId = element->trackId();
                if (pieceId >= kMaxTrackPieces)
                {
                    return;
                }
                const auto parts = World::TrackData::getTrackPiece(pieceId);
                if (element->sequenceIndex() >= parts.size())
                {
                    return;
                }
                const auto& part = parts[element->sequenceIndex()];
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, element->rotation());
                const World::Pos3 start{
                    static_cast<coord_t>(tilePos.x - offset.x),
                    static_cast<coord_t>(tilePos.y - offset.y),
                    static_cast<coord_t>(element->baseHeight() - part.z),
                };
                const auto forward = static_cast<uint16_t>((pieceId << 3) | element->rotation());
                const auto reverseTad = static_cast<uint16_t>(forward | (1U << 2));
                if (!element->hasSignal() || !wrongWaySignal(start, forward | World::Track::AdditionalTaDFlags::hasSignal, element->trackObjectId()))
                {
                    push(start, forward, -1, 0);
                }
                if (knownPiece(reverseTad, false) && (!element->hasSignal() || !wrongWaySignal(start, reverseTad | World::Track::AdditionalTaDFlags::hasSignal, element->trackObjectId())))
                {
                    push(pieceEnd(start, forward, false).nextPos, reverseTad, -1, 0);
                }
            };
            auto considerRoad = [&](const World::Pos2& tilePos, World::RoadElement* element) {
                const auto pieceId = element->roadId();
                if (pieceId >= kMaxRoadPieces)
                {
                    return;
                }
                const auto parts = World::TrackData::getRoadPiece(pieceId);
                if (element->sequenceIndex() >= parts.size())
                {
                    return;
                }
                const auto& part = parts[element->sequenceIndex()];
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, element->rotation());
                const World::Pos3 start{
                    static_cast<coord_t>(tilePos.x - offset.x),
                    static_cast<coord_t>(tilePos.y - offset.y),
                    static_cast<coord_t>(element->baseHeight() - part.z),
                };
                const auto forward = static_cast<uint16_t>((pieceId << 3) | element->rotation());
                const auto* roadObject = ObjectManager::get<RoadObject>(element->roadObjectId());
                const auto oneWayRoad = roadObject != nullptr && roadObject->hasFlags(RoadObjectFlags::isOneWay);
                push(start, forward, -1, 0);
                const auto reverseTad = static_cast<uint16_t>(forward | (1U << 2));
                if (!oneWayRoad && knownPiece(reverseTad, true))
                {
                    push(pieceEnd(start, forward, true).nextPos, reverseTad, -1, 0);
                }
            };

            for (const auto& tilePos : from.tiles)
            {
                auto tile = World::TileManager::get(tilePos);
                if (tile.isNull())
                {
                    continue;
                }
                for (auto& entry : tile)
                {
                    if (road)
                    {
                        if (auto* element = entry.as<World::RoadElement>(); element != nullptr && !element->isGhost() && !element->isAiAllocated())
                        {
                            considerRoad(tilePos, element);
                        }
                    }
                    else if (auto* element = entry.as<World::TrackElement>(); element != nullptr && !element->isGhost() && !element->isAiAllocated())
                    {
                        considerTrack(tilePos, element);
                    }
                }
            }

            while (!open.empty())
            {
                const auto index = open.top().second;
                open.pop();
                const auto step = steps[static_cast<size_t>(index)];
                if (bestCost[stepKey(step.pos, step.tad)] != step.cost)
                {
                    continue;
                }
                const auto tad = plainTad(step.tad, road);
                if (!knownPiece(tad, road))
                {
                    continue;
                }
                if (pieceTouches(step.pos, tad, road, to.tiles))
                {
                    std::vector<int> chain;
                    for (auto cursor = index; cursor != -1; cursor = steps[static_cast<size_t>(cursor)].parent)
                    {
                        chain.push_back(cursor);
                    }
                    std::reverse(chain.begin(), chain.end());
                    for (const auto cursor : chain)
                    {
                        const auto& piece = steps[static_cast<size_t>(cursor)];
                        rememberPiece(piece.pos, piece.tad, road, colour, selected);
                    }
                    return true;
                }
                const auto end = pieceEnd(step.pos, tad, road);
                std::vector<uint16_t> connections;
                collectConnections(end.nextPos, end.nextRotation, road, company, trackObjectId, connections);
                for (const auto plain : connections)
                {
                    const auto nextEnd = pieceEnd(end.nextPos, plain, road);
                    auto length = std::abs(nextEnd.nextPos.x - end.nextPos.x) + std::abs(nextEnd.nextPos.y - end.nextPos.y);
                    if (length <= 0)
                    {
                        length = 1;
                    }
                    push(end.nextPos, plain, index, step.cost + length);
                }
                if (connections.empty())
                {
                    const auto reverseTad = static_cast<uint16_t>(tad ^ (1U << 2));
                    bool alreadyReversed = false;
                    if (step.parent != -1)
                    {
                        const auto parentTad = plainTad(steps[static_cast<size_t>(step.parent)].tad, road);
                        alreadyReversed = (parentTad ^ tad) == (1U << 2);
                    }
                    if (!alreadyReversed && knownPiece(reverseTad, road))
                    {
                        bool allow = true;
                        if (road)
                        {
                            allow = roadAllowsReverse(step.pos, tad);
                        }
                        else if (trackObjectId != 0xFF)
                        {
                            allow = !wrongWaySignal(step.pos, static_cast<uint16_t>(reverseTad | World::Track::AdditionalTaDFlags::hasSignal), trackObjectId);
                        }
                        if (allow)
                        {
                            const auto turned = pieceEnd(step.pos, tad, road);
                            auto length = std::abs(turned.nextPos.x - step.pos.x) + std::abs(turned.nextPos.y - step.pos.y);
                            if (length <= 0)
                            {
                                length = 1;
                            }
                            push(turned.nextPos, reverseTad, index, step.cost + length);
                        }
                    }
                }
            }
            return false;
        }

        void searchWater(const StopMark& from, const StopMark& to, uint8_t colour, bool selected)
        {
            if (from.tiles.empty() || to.tiles.empty())
            {
                return;
            }
            auto isTarget = [&](const World::Pos2& pos) {
                for (const auto& tile : to.tiles)
                {
                    if (tile.x == pos.x && tile.y == pos.y)
                    {
                        return true;
                    }
                }
                return false;
            };
            auto water = [](const World::Pos2& pos) {
                auto tile = World::TileManager::get(pos);
                if (tile.isNull() || tile.surface() == nullptr)
                {
                    return false;
                }
                return tile.surface()->waterHeight() > 0;
            };
            struct Node
            {
                World::Pos2 pos{};
                int parent = -1;
            };
            std::vector<Node> nodes;
            std::deque<int> queue;
            std::unordered_set<uint32_t> seen;
            auto push = [&](const World::Pos2& pos, int parent) {
                if (nodes.size() >= kMaxSearchNodes || !water(pos))
                {
                    return;
                }
                const auto key = static_cast<uint32_t>(static_cast<uint16_t>(pos.x)) | (static_cast<uint32_t>(static_cast<uint16_t>(pos.y)) << 16);
                if (!seen.insert(key).second)
                {
                    return;
                }
                nodes.push_back({ pos, parent });
                queue.push_back(static_cast<int>(nodes.size() - 1));
            };
            for (const auto& tile : from.tiles)
            {
                push(tile, -1);
            }
            const World::Pos2 steps[] = { { World::kTileSize, 0 }, { static_cast<coord_t>(-World::kTileSize), 0 }, { 0, World::kTileSize }, { 0, static_cast<coord_t>(-World::kTileSize) } };
            while (!queue.empty())
            {
                const auto index = queue.front();
                queue.pop_front();
                const auto node = nodes[static_cast<size_t>(index)];
                if (node.parent != -1 && isTarget(node.pos))
                {
                    std::vector<World::Pos2> path;
                    for (auto cursor = index; cursor != -1; cursor = nodes[static_cast<size_t>(cursor)].parent)
                    {
                        path.push_back(nodes[static_cast<size_t>(cursor)].pos);
                    }
                    std::reverse(path.begin(), path.end());
                    for (const auto& pos : path)
                    {
                        rememberSurface(pos, colour, selected);
                    }
                    return;
                }
                for (const auto& step : steps)
                {
                    push({ static_cast<coord_t>(node.pos.x + step.x), static_cast<coord_t>(node.pos.y + step.y) }, index);
                }
            }
        }

        void searchAir(const StopMark& from, const StopMark& to, uint8_t colour, bool selected)
        {
            const auto dx = to.centre.x - from.centre.x;
            const auto dy = to.centre.y - from.centre.y;
            const auto steps = std::max(std::abs(dx), std::abs(dy)) / World::kTileSize;
            const auto count = std::max<int32_t>(steps, 1);
            for (int32_t i = 0; i <= count; ++i)
            {
                const World::Pos2 pos{
                    static_cast<coord_t>(from.centre.x + dx * i / count),
                    static_cast<coord_t>(from.centre.y + dy * i / count),
                };
                rememberSurface(pos, colour, selected);
            }
        }

        StopMark markStop(const LineOrder& order)
        {
            StopMark mark;
            if (order.type == Vehicles::OrderType::RouteWaypoint)
            {
                mark.centre = { static_cast<coord_t>(order.tileX * World::kTileSize), static_cast<coord_t>(order.tileY * World::kTileSize) };
                mark.height = static_cast<int32_t>(order.baseZ) * World::kSmallZStep;
                mark.tiles.push_back(mark.centre);
                return mark;
            }
            mark.station = true;
            mark.stationId = order.station;
            const auto* station = StationManager::get(static_cast<StationId>(order.station));
            if (station == nullptr || station->empty())
            {
                return mark;
            }
            mark.centre = { station->x, station->y };
            mark.height = station->z;
            mark.kinds = stationKinds(order.station);
            for (uint16_t i = 0; i < station->stationTileSize; ++i)
            {
                const auto pos = station->stationTiles[i];
                mark.tiles.push_back({ pos.x, pos.y });
            }
            if (mark.tiles.empty())
            {
                mark.tiles.push_back(mark.centre);
            }
            return mark;
        }

        std::string orderNumberLabel(uint8_t number, const Line& line, size_t orderIndex)
        {
            std::stringstream stream;
            stream << ControlCodes::inlineSpriteStr;
            const auto imageId = Gfx::recolour(ImageIds::getNumberCircle(number), Colour::white);
            stream.write(reinterpret_cast<const char*>(&imageId), 4);
            if (line.orders[orderIndex].type == Vehicles::OrderType::StopAt)
            {
                stream << ControlCodes::Colour::white;
                for (size_t next = orderIndex + 1; next < line.orders.size(); ++next)
                {
                    const auto& cargoOrder = line.orders[next];
                    const CargoObject* cargo = nullptr;
                    if (cargoOrder.type == Vehicles::OrderType::UnloadAll)
                    {
                        stream << " - ";
                        cargo = ObjectManager::get<CargoObject>(cargoOrder.cargo);
                    }
                    else if (cargoOrder.type == Vehicles::OrderType::WaitFor)
                    {
                        stream << " + ";
                        cargo = ObjectManager::get<CargoObject>(cargoOrder.cargo);
                    }
                    else
                    {
                        break;
                    }
                    if (cargo == nullptr)
                    {
                        continue;
                    }
                    stream << ControlCodes::inlineSpriteStr;
                    stream.write(reinterpret_cast<const char*>(&cargo->unitInlineSprite), 4);
                }
            }
            stream << std::endl;
            return stream.str();
        }

        void recordStopMarkers(const Line& line)
        {
            uint8_t number = 1;
            for (size_t index = 0; index < line.orders.size(); ++index)
            {
                const auto& order = line.orders[index];
                if (!numberedStop(order.type))
                {
                    continue;
                }
                StopMarker marker;
                marker.number = number;
                marker.label = orderNumberLabel(number, line, index);
                if (order.type == Vehicles::OrderType::RouteWaypoint)
                {
                    marker.x = static_cast<int32_t>(order.tileX) * World::kTileSize + 16;
                    marker.y = static_cast<int32_t>(order.tileY) * World::kTileSize + 16;
                    marker.z = static_cast<int32_t>(order.baseZ) * 8 + 8;
                    for (size_t earlier = 0; earlier < index; ++earlier)
                    {
                        const auto& previous = line.orders[earlier];
                        if (previous.type == Vehicles::OrderType::RouteWaypoint && previous.tileX == order.tileX && previous.tileY == order.tileY && previous.baseZ == order.baseZ && previous.trackId == order.trackId && previous.direction == order.direction)
                        {
                            marker.lineNumber++;
                        }
                    }
                }
                else
                {
                    const auto* station = StationManager::get(static_cast<StationId>(order.station));
                    if (station == nullptr || station->empty())
                    {
                        if (number < 63)
                        {
                            number++;
                        }
                        continue;
                    }
                    marker.x = station->x;
                    marker.y = station->y;
                    marker.z = station->z + 30;
                    marker.lineNumber = 1;
                    for (size_t earlier = 0; earlier < index; ++earlier)
                    {
                        const auto& previous = line.orders[earlier];
                        if ((previous.type == Vehicles::OrderType::StopAt || previous.type == Vehicles::OrderType::RouteThrough) && previous.station == order.station)
                        {
                            marker.lineNumber++;
                        }
                    }
                }
                _markers.push_back(std::move(marker));
                if (number == 63)
                {
                    break;
                }
                number++;
            }
        }

        void searchLeg(const Line& line, const StopMark& from, const StopMark& to, bool selected)
        {
            const auto company = from.station ? StationManager::get(static_cast<StationId>(from.stationId)) != nullptr ? StationManager::get(static_cast<StationId>(from.stationId))->owner : CompanyManager::getControllingId() : CompanyManager::getControllingId();
            const auto kinds = line.kinds == kEveryLineKind ? from.kinds : line.kinds;
            const auto air = (kinds & LineKind::air) != LineKind::none && (kinds & (LineKind::rail | LineKind::road | LineKind::tram | LineKind::water)) == LineKind::none;
            const auto water = (kinds & LineKind::water) != LineKind::none && (kinds & (LineKind::rail | LineKind::road | LineKind::tram | LineKind::air)) == LineKind::none;
            const auto untyped = kinds == LineKind::none || kinds == kEveryLineKind;
            if (air || (untyped && line.mode == TransportMode::air))
            {
                searchAir(from, to, line.colour, selected);
            }
            else if (water || (untyped && line.mode == TransportMode::water))
            {
                searchWater(from, to, line.colour, selected);
            }
            else if ((kinds & (LineKind::road | LineKind::tram)) != LineKind::none || (untyped && line.mode == TransportMode::road))
            {
                const auto roadKinds = kinds == LineKind::none ? kEveryLineKind : kinds;
                searchTrack(from, to, company, 0, true, roadKinds, line.colour, selected);
            }
            else
            {
                uint8_t trackObjectId = 0xFF;
                for (const auto& tilePos : from.tiles)
                {
                    auto tile = World::TileManager::get(tilePos);
                    if (tile.isNull())
                    {
                        continue;
                    }
                    for (auto& entry : tile)
                    {
                        if (auto* element = entry.as<World::TrackElement>(); element != nullptr && !element->isGhost())
                        {
                            trackObjectId = element->trackObjectId();
                            break;
                        }
                    }
                    if (trackObjectId != 0xFF)
                    {
                        break;
                    }
                }
                searchTrack(from, to, company, trackObjectId, false, kinds, line.colour, selected);
            }
        }

        void buildLine(const Line& line, bool selected)
        {
            std::vector<StopMark> stops;
            for (const auto& order : line.orders)
            {
                if (!numberedStop(order.type))
                {
                    continue;
                }
                stops.push_back(markStop(order));
            }
            ShownRoute route;
            route.lineId = line.id;
            route.colour = line.colour;
            _recording = &route.points;
            _recordingLine = line.id;
            _breakNext = true;
            RouteLeg legs[64]{};
            const auto legCount = loopLegs(stops.size(), legs, std::size(legs));
            for (size_t i = 0; i < legCount; ++i)
            {
                const auto& from = stops[legs[i].fromStop];
                const auto& to = stops[legs[i].toStop];
                searchLeg(line, from, to, selected);
            }
            _recording = nullptr;
            if (route.points.size() >= 2)
            {
                _routes.push_back(std::move(route));
            }
        }

        uint16_t previewVehicle(const Line& line)
        {
            const auto usable = [&](uint16_t id) { return EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(id)) && lineForVehicle(id) == &line; };
            if (line.id == _selectedLine && usable(_previewVehicle))
            {
                return _previewVehicle;
            }
            for (const auto id : line.vehicles)
            {
                if (usable(id) && EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(id))->isPlaced())
                {
                    return id;
                }
            }
            for (const auto id : line.vehicles)
            {
                if (usable(id))
                {
                    return id;
                }
            }
            return UINT16_MAX;
        }
        StopMark legStop(uint64_t raw)
        {
            return markStop(fromVehicleOrder(*reinterpret_cast<const Vehicles::Order*>(&raw)));
        }
        bool refreshPlan(uint16_t id)
        {
            const auto* line = findLine(id);
            const auto* program = Rules::find({ Rules::OwnerKind::line, id });
            if (!line || !program || program->blocks.empty())
            {
                return false;
            }
            auto& plan = _livePlans[id];
            plan.vehicle = previewVehicle(*line);
            auto preview = Rules::previewLineRoute(id, plan.vehicle, Rules::RoutePreviewMode::currentConditions);
            ++_viewStats.rulePolls;
            std::vector<LegKey> legs;
            std::optional<uint64_t> previous;
            for (const auto key : preview.orders)
            {
                const auto row = Rules::indexOf(*program, key);
                if (row >= program->rows.size())
                {
                    continue;
                }
                const auto raw = program->rows[row].raw;
                if (previous && *previous != raw)
                {
                    const LegKey leg{ id, *previous, raw };
                    if (std::find(legs.begin(), legs.end(), leg) == legs.end())
                    {
                        legs.push_back(leg);
                    }
                }
                previous = raw;
            }
            const bool changed = legs != plan.legs;
            if (changed)
            {
                plan.capacityLimited = false;
                plan.finishLimited = false;
            }
            plan.legs = std::move(legs);
            plan.preview = std::move(preview);
            plan.refresh = false;
            return changed;
        }
        bool pendingPlan(const LivePlan& plan)
        {
            return !plan.published || plan.published->legs.size() != plan.legs.size()
                || !std::all_of(plan.legs.begin(), plan.legs.end(), [&](const LegKey& key) { return std::find(plan.published->legs.begin(), plan.published->legs.end(), key) != plan.published->legs.end(); });
        }
        std::pair<size_t, size_t> publishedSize(uint16_t except = 0)
        {
            size_t legs = 0, units = 0;
            for (const auto& [id, plan] : _livePlans)
            {
                if (id != except && plan.published)
                {
                    legs += plan.published->legCount;
                    units += plan.published->route.points.size() + plan.published->pieces.size();
                }
            }
            return { legs, units };
        }
        bool pinnedLeg(const LegKey& leg)
        {
            const auto plan = _livePlans.find(leg.line);
            return plan != _livePlans.end() && !plan->second.capacityLimited && pendingPlan(plan->second)
                && std::find(plan->second.legs.begin(), plan->second.legs.end(), leg) != plan->second.legs.end();
        }
        bool publishReadyPlans()
        {
            bool changed = false;
            auto totals = publishedSize();
            const auto publish = [&](uint16_t id, LivePlan& plan) {
                if (plan.refresh || !pendingPlan(plan) || (plan.capacityLimited && !plan.finishLimited))
                {
                    return;
                }
                plan.finishLimited = false;
                const auto oldLegs = plan.published ? plan.published->legCount : 0;
                const auto oldUnits = plan.published ? plan.published->route.points.size() + plan.published->pieces.size() : 0;
                const auto size = std::pair{ totals.first - oldLegs, totals.second - oldUnits };
                if (id != _selectedLine && size.first + plan.legs.size() > kMaxCachedLegs)
                {
                    plan.capacityLimited = true;
                    return;
                }
                const auto ready = std::all_of(plan.legs.begin(), plan.legs.end(), [&](const LegKey& key) { return _legCache.contains(key); });
                if (!ready && !plan.capacityLimited)
                {
                    return;
                }
                LivePlan::Published next;
                next.legs = plan.legs;
                next.route.lineId = id;
                std::unordered_set<uint64_t> seenPieces;
                for (const auto& key : plan.legs)
                {
                    const auto it = _legCache.find(key);
                    if (it == _legCache.end())
                    {
                        next.limited = true;
                        break;
                    }
                    const auto& leg = it->second;
                    // One publication owns a stable bounded snapshot. Cache
                    // eviction can never remove pieces from a displayed line.
                    if (next.route.points.size() + next.pieces.size() + leg.points.size() + leg.pieces.size() > kMaxCachedUnits)
                    {
                        next.limited = true;
                        break;
                    }
                    next.pieces.insert(leg.pieces.begin(), leg.pieces.end());
                    ++next.legCount;
                    uint64_t piece = 0;
                    bool duplicate = false, gap = true;
                    for (auto point : leg.points)
                    {
                        if (point.piece && point.piece != piece)
                        {
                            piece = point.piece;
                            duplicate = !seenPieces.insert(piece).second;
                        }
                        if (point.piece && duplicate)
                        {
                            gap = true;
                            continue;
                        }
                        point.breakBefore |= gap;
                        gap = false;
                        next.route.points.push_back(point);
                    }
                    next.limited |= leg.limited;
                }
                if (!ready && next.legCount == 0 && !plan.legs.empty())
                {
                    return;
                }
                next.legs.resize(next.legCount);
                const auto units = next.route.points.size() + next.pieces.size();
                auto total = size;
                if (id == _selectedLine)
                {
                    // A newly selected line may replace an older background
                    // publication once; rejected background lines stay limited
                    // rather than repeatedly evicting each other's display.
                    for (auto& [otherId, other] : _livePlans)
                    {
                        if (total.first + next.legCount <= kMaxCachedLegs && total.second + units <= kMaxCachedUnits)
                        {
                            break;
                        }
                        if (otherId != id && other.published)
                        {
                            total.first -= other.published->legCount;
                            total.second -= other.published->route.points.size() + other.published->pieces.size();
                            totals.first -= other.published->legCount;
                            totals.second -= other.published->route.points.size() + other.published->pieces.size();
                            other.published.reset();
                            other.capacityLimited = true;
                        }
                    }
                }
                if (total.first + next.legCount > kMaxCachedLegs || total.second + units > kMaxCachedUnits)
                {
                    plan.capacityLimited = true;
                    return;
                }
                const auto before = totals;
                plan.published = std::move(next);
                totals.first += plan.published->legCount;
                totals.first -= oldLegs;
                totals.second += units;
                totals.second -= oldUnits;
                plan.capacityLimited = plan.published->limited;
                ++_viewStats.routePublishes;
                changed = true;
                const auto after = totals;
                if (after.first < before.first || after.second < before.second)
                {
                    for (auto& [otherId, other] : _livePlans)
                    {
                        const auto ownLegs = other.published ? other.published->legCount : 0;
                        if (otherId != id && pendingPlan(other) && totals.first - ownLegs + other.legs.size() <= kMaxCachedLegs)
                        {
                            other.capacityLimited = false;
                        }
                    }
                }
            };
            if (auto selected = _livePlans.find(_selectedLine); selected != _livePlans.end())
            {
                publish(selected->first, selected->second);
            }
            for (auto& [id, plan] : _livePlans)
            {
                if (id != _selectedLine)
                {
                    publish(id, plan);
                }
            }
            return changed;
        }
        void composeLiveRoutes()
        {
            _liveOverlay.clear();
            _liveRoutes.clear();
            const auto append = [&](uint16_t id, const LivePlan& plan) {
                const auto* line = findLine(id);
                if (!line || !plan.published)
                {
                    return;
                }
                const bool selected = _showSelected && id == _selectedLine;
                for (const auto piece : plan.published->pieces)
                {
                    _liveOverlay[piece] = { line->colour, selected };
                }
                if (plan.published->route.points.size() >= 2)
                {
                    auto route = plan.published->route;
                    route.colour = line->colour;
                    _liveRoutes.push_back(std::move(route));
                }
            };
            for (const auto& [id, plan] : _livePlans)
            {
                if (id != _selectedLine)
                {
                    append(id, plan);
                }
            }
            if (const auto selected = _livePlans.find(_selectedLine); selected != _livePlans.end())
            {
                append(selected->first, selected->second);
            }
        }
        bool buildMissingLeg(uint16_t id)
        {
            const auto plan = _livePlans.find(id);
            const auto* line = findLine(id);
            if (plan == _livePlans.end() || !line || plan->second.refresh || plan->second.capacityLimited || !pendingPlan(plan->second))
            {
                return false;
            }
            if (id != _selectedLine && publishedSize(id).first + plan->second.legs.size() > kMaxCachedLegs)
            {
                plan->second.capacityLimited = true;
                return false;
            }
            for (const auto& key : plan->second.legs)
            {
                if (_legCache.contains(key))
                {
                    continue;
                }
                CachedLeg leg;
                _recording = &leg.points;
                _recordingPieces = &leg.pieces;
                _recordingLine = id;
                _usedTraversals.clear();
                _breakNext = true;
                _pointPiece = 0;
                searchLeg(*line, legStop(key.from), legStop(key.to), false);
                _recording = nullptr;
                _recordingPieces = nullptr;
                leg.limited = leg.points.size() >= kMaxLegPoints || leg.pieces.size() >= kMaxLegPoints;
                leg.used = ++_cacheClock;
                const auto units = leg.points.size() + leg.pieces.size();
                while (!_legCache.empty() && (_legCache.size() >= kMaxCachedLegs || _cachedUnits + units > kMaxCachedUnits))
                {
                    auto oldest = _legCache.end();
                    for (auto candidate = _legCache.begin(); candidate != _legCache.end(); ++candidate)
                    {
                        const bool canEvict = !pinnedLeg(candidate->first) || (id == _selectedLine && candidate->first.line != id);
                        if (canEvict && (oldest == _legCache.end() || candidate->second.used < oldest->second.used))
                        {
                            oldest = candidate;
                        }
                    }
                    if (oldest == _legCache.end())
                    {
                        plan->second.capacityLimited = true;
                        plan->second.finishLimited = true;
                        ++_viewStats.pathSearches;
                        return true;
                    }
                    if (pinnedLeg(oldest->first))
                    {
                        auto& background = _livePlans.at(oldest->first.line);
                        background.capacityLimited = true;
                        background.finishLimited = true;
                    }
                    _cachedUnits -= oldest->second.points.size() + oldest->second.pieces.size();
                    _legCache.erase(oldest);
                }
                _cachedUnits += units;
                _legCache.emplace(key, std::move(leg));
                ++_viewStats.pathSearches;
                return true;
            }
            return false;
        }

        void rebuild(bool geometryDirty)
        {
            _routeLineIds.clear();
            auto previous = std::move(_livePlans);
            _livePlans.clear();
            _liveOverlay.clear();
            _liveRoutes.clear();
            if (geometryDirty)
            {
                _legCache.clear();
                _cachedUnits = 0;
                previous.clear();
            }
            _viewUpdates = 0;
            _pollCursor = _buildCursor = 0;
            _overlay.clear();
            _routes.clear();
            _markers.clear();
            _usedTraversals.clear();
            _breakNext = false;
            _recording = nullptr;
            if (_selectedLine != 0)
            {
                if (const auto* selectedLine = findLine(_selectedLine))
                {
                    recordStopMarkers(*selectedLine);
                }
            }
            if (!_showSelected && !_showAll)
            {
                return;
            }
            for (const auto& line : allLines())
            {
                const auto selected = _showSelected && line.id == _selectedLine;
                if (!_showAll && !selected)
                {
                    continue;
                }
                if (const auto* rules = Rules::find({ Rules::OwnerKind::line, line.id }); rules && !rules->blocks.empty())
                {
                    if (auto old = previous.find(line.id); old != previous.end())
                    {
                        auto& plan = _livePlans[line.id];
                        plan = std::move(old->second);
                        plan.refresh = true;
                        plan.capacityLimited = false;
                    }
                    else
                    {
                        _livePlans.try_emplace(line.id);
                    }
                }
                else if (!selected)
                {
                    buildLine(line, false);
                }
            }
            if (_showSelected)
            {
                if (const auto* line = findLine(_selectedLine))
                {
                    if (!_livePlans.contains(line->id))
                    {
                        buildLine(*line, true);
                    }
                }
            }
            _routeLineIds.clear();
            for (const auto& route : _routes)
            {
                _routeLineIds.push_back(route.lineId);
            }
            for (const auto& [id, plan] : _livePlans)
            {
                _routeLineIds.push_back(id);
            }
            std::sort(_routeLineIds.begin(), _routeLineIds.end());
            _routeLineIds.erase(std::unique(_routeLineIds.begin(), _routeLineIds.end()), _routeLineIds.end());
            composeLiveRoutes();
        }
    }

    void invalidateRouteCache()
    {
        _dirty = true;
    }

    void setRouteView(uint16_t selectedLine, bool showSelected, bool showAll, uint16_t vehicle)
    {
        if (!_dirty && selectedLine == _selectedLine && showSelected == _showSelected && showAll == _showAll)
        {
            if (_previewVehicle != vehicle)
            {
                _previewVehicle = vehicle;
                invalidateRuleRouteCache(selectedLine);
            }
            return;
        }
        _selectedLine = selectedLine;
        _showSelected = showSelected;
        _showAll = showAll;
        _previewVehicle = vehicle;
        const bool geometryDirty = _dirty;
        _dirty = false;
        rebuild(geometryDirty);
        Gfx::invalidateScreen();
    }

    void invalidateRuleRouteCache(uint16_t line)
    {
        const auto* rules = Rules::find({ Rules::OwnerKind::line, line });
        if (const auto plan = _livePlans.find(line); plan != _livePlans.end() && rules && !rules->blocks.empty())
        {
            plan->second.refresh = true;
        }
        else
        {
            _dirty = true;
        }
    }
    void updateRouteView()
    {
        if (!_showSelected && !_showAll)
        {
            return;
        }
        if (_dirty)
        {
            setRouteView(_selectedLine, _showSelected, _showAll, _previewVehicle);
        }
        if (_livePlans.empty())
        {
            return;
        }
        ++_viewUpdates;
        if (const auto selected = _livePlans.find(_selectedLine); selected != _livePlans.end() && (selected->second.refresh || _viewUpdates % 8 == 0))
        {
            refreshPlan(selected->first);
        }
        // All-lines mode shares a bounded refresh budget instead of evaluating
        // every line on every draw. The selected vehicle receives priority.
        if (_viewUpdates % 2 == 0 || _viewUpdates == 1)
        {
            auto next = _livePlans.upper_bound(_pollCursor);
            for (size_t count = 0; count < _livePlans.size(); ++count)
            {
                if (next == _livePlans.end())
                {
                    next = _livePlans.begin();
                }
                const auto id = next++->first;
                if (id != _selectedLine)
                {
                    _pollCursor = id;
                    refreshPlan(id);
                    break;
                }
            }
        }
        bool built = _viewUpdates % 2 == 0 && buildMissingLeg(_selectedLine);
        if (!built)
        {
            auto next = _livePlans.upper_bound(_buildCursor);
            for (size_t count = 0; count < _livePlans.size(); ++count)
            {
                if (next == _livePlans.end())
                {
                    next = _livePlans.begin();
                }
                const auto id = next++->first;
                if (buildMissingLeg(id))
                {
                    _buildCursor = id;
                    built = true;
                    break;
                }
            }
        }
        if (publishReadyPlans())
        {
            composeLiveRoutes();
            Gfx::invalidateScreen();
        }
    }
    RouteViewStats routeViewStats()
    {
        auto stats = _viewStats;
        stats.cachedLegs = _legCache.size();
        stats.cachedUnits = _cachedUnits;
        const auto published = publishedSize();
        stats.publishedLegs = published.first;
        stats.publishedUnits = published.second;
        for (const auto& [id, plan] : _livePlans)
        {
            for (const auto& leg : plan.legs)
            {
                stats.pendingLegs += pendingPlan(plan) && !_legCache.contains(leg);
            }
        }
        return stats;
    }
    std::string routePreviewStatus(uint16_t line)
    {
        const auto plan = _livePlans.find(line);
        if (plan == _livePlans.end())
        {
            return {};
        }
        if (plan->second.capacityLimited || (plan->second.published && plan->second.published->limited))
        {
            return "Map: preview limit";
        }
        if (plan->second.refresh || pendingPlan(plan->second))
        {
            return plan->second.published ? "Map: updating preview" : "Map: building preview";
        }
        if (_showAll && std::any_of(_livePlans.begin(), _livePlans.end(), [](const auto& entry) { return entry.second.capacityLimited; }))
        {
            return "Map: All preview limit";
        }
        if (plan->second.preview.limited)
        {
            return "Map: preview limit";
        }
        if (plan->second.preview.waiting)
        {
            return "Map: ends at wait";
        }
        if (!plan->second.preview.reason.empty())
        {
            return "Map: " + plan->second.preview.reason;
        }
        return plan->second.vehicle == UINT16_MAX ? "Map: no vehicle; current station checks" : "Map: current conditions";
    }
    int32_t routeLaneOffset(uint16_t line)
    {
        const auto found = std::lower_bound(_routeLineIds.begin(), _routeLineIds.end(), line);
        return found == _routeLineIds.end() || *found != line ? 0 : laneOffsetPixels(static_cast<int>(found - _routeLineIds.begin()), static_cast<int>(_routeLineIds.size()));
    }

    void clearRouteView()
    {
        _showSelected = false;
        _showAll = false;
        _selectedLine = 0;
        _dirty = true;
        _overlay.clear();
        _routes.clear();
        _markers.clear();
        _usedTraversals.clear();
        _livePlans.clear();
        _liveOverlay.clear();
        _liveRoutes.clear();
        _routeLineIds.clear();
        _legCache.clear();
        _cachedUnits = 0;
        _viewStats = {};
        _previewVehicle = UINT16_MAX;
        _breakNext = false;
        _recording = nullptr;
        Gfx::invalidateScreen();
    }

    int32_t laneOffsetPixels(int lane, int laneCount)
    {
        if (laneCount <= 1 || lane < 0 || lane >= laneCount)
        {
            return 0;
        }
        return 4 * (lane * 2 - (laneCount - 1));
    }

    int countDistinctLines(const uint16_t* lineIds, size_t count)
    {
        if (lineIds == nullptr || count == 0)
        {
            return 0;
        }
        std::vector<uint16_t> ordered(lineIds, lineIds + count);
        std::sort(ordered.begin(), ordered.end());
        ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
        return static_cast<int>(ordered.size());
    }

    StrokeEnds shiftStroke(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t pixels)
    {
        const auto dx = x1 - x0;
        const auto dy = y1 - y0;
        const auto length = std::lround(std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy));
        if (length == 0 || pixels == 0)
        {
            return { x0, y0, x1, y1 };
        }
        const auto shiftX = static_cast<int32_t>((-static_cast<int64_t>(dy) * pixels) / length);
        const auto shiftY = static_cast<int32_t>((static_cast<int64_t>(dx) * pixels) / length);
        return { x0 + shiftX, y0 + shiftY, x1 + shiftX, y1 + shiftY };
    }

    bool strokeContinues(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
    {
        const auto dx = static_cast<int64_t>(x1) - x0;
        const auto dy = static_cast<int64_t>(y1) - y0;
        if (dx == 0 && dy == 0)
        {
            return false;
        }
        // One piece, or the join through a station. Not a chord across the map.
        constexpr int64_t kMaxSpan = 32 * 32;
        return dx * dx + dy * dy <= kMaxSpan * kMaxSpan;
    }

    size_t trackPieceCurve(int32_t x, int32_t y, int32_t z, uint16_t trackAndDirection, bool road, CurveSample* out, size_t capacity)
    {
        const auto plain = plainTad(trackAndDirection, road);
        if (!knownPiece(plain, road))
        {
            return 0;
        }
        return sampleVehiclePath(World::Pos3{ static_cast<coord_t>(x), static_cast<coord_t>(y), static_cast<coord_t>(z) }, plain, road, out, capacity);
    }

    bool samePieceSameWay(uint16_t line, int32_t x, int32_t y, int32_t z, uint16_t tad, uint16_t otherLine, int32_t otherX, int32_t otherY, int32_t otherZ, uint16_t otherTad)
    {
        const auto direction = static_cast<uint16_t>(tad & 0x1FF & ~(1U << 2));
        const auto otherDirection = static_cast<uint16_t>(otherTad & 0x1FF & ~(1U << 2));
        return line == otherLine && x == otherX && y == otherY && z == otherZ && direction == otherDirection;
    }

    void drawRouteStrokes(Gfx::DrawingContext& drawingCtx, ZoomLevel zoom, uint8_t rotation)
    {
        auto toScreen = [&](int32_t x, int32_t y, int32_t z) {
            const auto vp = World::gameToScreen(World::Pos3{ static_cast<coord_t>(x), static_cast<coord_t>(y), static_cast<coord_t>(z) }, rotation);
            return Ui::Point(zoom.applyInversedTo(vp.x), zoom.applyInversedTo(vp.y));
        };
        for (const auto* routes : { &_routes, &_liveRoutes })
        {
            for (const auto& route : *routes)
            {
                const auto shift = routeLaneOffset(route.lineId);
                for (size_t i = 1; i < route.points.size(); ++i)
                {
                    const auto& from = route.points[i - 1];
                    const auto& to = route.points[i];
                    if (to.breakBefore || !strokeContinues(from.x, from.y, to.x, to.y))
                    {
                        continue;
                    }
                    const auto start = toScreen(from.x, from.y, from.z);
                    const auto end = toScreen(to.x, to.y, to.z);
                    drawingCtx.drawLine(Ui::Point(start.x, start.y + shift), Ui::Point(end.x, end.y + shift), Colours::getShade(static_cast<Colour>(route.colour), 6));
                }
            }
        }

        auto numbers = Gfx::TextRenderer(drawingCtx);
        numbers.setCurrentFont(Gfx::Font::medium_normal);
        for (const auto& marker : _markers)
        {
            if (marker.label.empty())
            {
                continue;
            }
            const auto screen = World::gameToScreen(World::Pos3{ static_cast<coord_t>(marker.x), static_cast<coord_t>(marker.y), static_cast<coord_t>(marker.z) }, rotation);
            const auto stringWidth = Gfx::TextRenderer::getStringWidth(Gfx::Font::medium_bold, marker.label.c_str());
            const auto uiWidth = stringWidth + 3;
            const auto uiHeight = 11;
            const auto width = zoom.applyTo<int32_t>(uiWidth);
            const auto numberHeight = zoom.applyTo(marker.lineNumber * 10);
            const auto firstLineHeight = zoom.applyTo(uiHeight);
            const auto left = zoom.applyInversedTo(screen.x - width / 2);
            const auto top = zoom.applyInversedTo(screen.y - firstLineHeight / 2 + numberHeight);
            numbers.drawString(Ui::Point(left + 1, top), AdvancedColour(Colour::white).outline(), marker.label.c_str());
        }
    }

    static std::optional<uint8_t> overlayColour(uint64_t key)
    {
        const auto base = _overlay.find(key), live = _liveOverlay.find(key);
        if (live != _liveOverlay.end() && (base == _overlay.end() || live->second.selected || !base->second.selected))
        {
            return live->second.colour;
        }
        return base == _overlay.end() ? std::nullopt : std::optional<uint8_t>{ base->second.colour };
    }
    std::optional<uint8_t> routeOverlayColour(int32_t x, int32_t y, uint8_t baseZ, uint8_t pieceId, uint8_t rotation, uint8_t kind)
    {
        return overlayColour(packKey({ x, y, baseZ, pieceId, rotation, kind }));
    }
    std::optional<uint8_t> surfaceOverlayColour(int32_t x, int32_t y)
    {
        return overlayColour(packSurface(x, y));
    }
}
