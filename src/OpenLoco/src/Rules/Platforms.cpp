#include "Rules/Platforms.h"
#include "Map/StationElement.h"
#include "Map/TileManager.h"
#include "Map/Track/TrackData.h"
#include "Map/TrackElement.h"
#include "World/Station.h"
#include "World/StationManager.h"
#include <algorithm>
#include <set>

namespace OpenLoco::Rules
{
    uint64_t platformAnchor(const Signals::PieceKey& p)
    {
        return uint64_t(uint16_t(p.x)) | (uint64_t(uint16_t(p.y)) << 16) | (uint64_t(uint16_t(p.z)) << 32)
            | (uint64_t(p.rotation) << 48) | (uint64_t(p.trackId) << 56);
    }
    bool platformIsFree(const Platform& platform, uint16_t vehicle)
    {
        for (const auto& piece : platform.pieces)
        {
            const auto hold = Signals::holdAt(piece);
            if (hold && hold->kind == Signals::HoldKind::occupied && hold->owner != vehicle)
            {
                return false;
            }
        }
        return true;
    }
    std::vector<Platform> railPlatforms(uint16_t stationId)
    {
        const auto* station = StationManager::get(static_cast<StationId>(stationId));
        if (station == nullptr || station->empty())
        {
            return {};
        }
        std::set<Signals::PieceKey> remaining;
        for (uint16_t i = 0; i < station->stationTileSize; ++i)
        {
            const auto pos = station->stationTiles[i];
            auto tile = World::TileManager::get(World::Pos2{ pos.x, pos.y });
            for (auto& el : tile)
            {
                const auto* track = el.as<World::TrackElement>();
                if (track == nullptr || !track->hasStationElement() || track->isGhost() || track->isAiAllocated())
                {
                    continue;
                }
                bool belongs = false;
                for (auto& other : tile)
                {
                    const auto* element = other.as<World::StationElement>();
                    if (element != nullptr && element->stationId() == static_cast<StationId>(stationId) && element->baseZ() == track->baseZ())
                    {
                        belongs = true;
                    }
                }
                if (belongs)
                {
                    remaining.insert(Signals::pieceFromElement({ pos.x, pos.y, static_cast<int16_t>(track->baseZ() * World::kSmallZStep) }, track->rotation(), track->trackId(), track->sequenceIndex()));
                }
            }
        }
        std::vector<Platform> result;
        while (!remaining.empty())
        {
            Platform platform;
            auto first = *remaining.begin();
            remaining.erase(remaining.begin());
            platform.anchor = platformAnchor(first);
            platform.pieces.push_back(first);
            for (size_t i = 0; i < platform.pieces.size(); ++i)
            {
                const auto piece = platform.pieces[i];
                // Connect the two real ends of each station track piece.
                for (uint8_t reversed = 0; reversed < 2; ++reversed)
                {
                    const auto tad = static_cast<uint16_t>((piece.trackId << 3) | piece.rotation | (reversed << 2));
                    const auto entrance = Signals::entranceOf({ piece, reversed });
                    const auto& data = World::TrackData::getUnkTrack(tad);
                    auto exit = entrance.pos + data.pos;
                    if (data.rotationEnd < 12)
                    {
                        exit -= World::Pos3{ World::kRotationOffset[data.rotationEnd], 0 };
                    }
                    for (auto it = remaining.begin(); it != remaining.end();)
                    {
                        bool connected = false;
                        for (uint8_t side = 0; side < 2; ++side)
                        {
                            const auto otherEntrance = Signals::entranceOf({ *it, side });
                            if (otherEntrance.pos == exit && (otherEntrance.connection & 3) == (data.rotationEnd & 3))
                            {
                                connected = true;
                            }
                        }
                        if (connected)
                        {
                            platform.pieces.push_back(*it);
                            it = remaining.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }
                }
            }
            result.push_back(std::move(platform));
        }
        return result;
    }
}
