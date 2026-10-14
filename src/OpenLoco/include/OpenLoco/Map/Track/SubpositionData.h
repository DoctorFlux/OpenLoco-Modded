#pragma once

#include "Types.hpp"
#include <OpenLoco/Engine/World.hpp>
#include <optional>
#include <span>

namespace OpenLoco
{
    enum class Pitch : uint8_t;
}

namespace OpenLoco::World::TrackData
{
    struct MoveInfo
    {
        World::Pos3 loc; // 0x00
        uint8_t yaw;     // 0x06
        Pitch pitch;     // 0x07
    };
    static_assert(sizeof(MoveInfo) == 0x8);

    std::span<const MoveInfo> getTrackSubPositon(const uint16_t trackAndDirection);
    std::optional<uint16_t> getReversedTrackSubPosition(uint16_t trackAndDirection, uint16_t subPosition);
    std::span<const MoveInfo> getRoadSubPositon(const uint16_t trackAndDirection);
    std::span<const MoveInfo> getRoadPlacementSubPositon(const uint16_t trackAndDirection);
    struct ReversedRoadPosition
    {
        uint16_t trackAndDirection;
        uint16_t subPosition;
    };
    std::optional<ReversedRoadPosition> getReversedRoadSubPosition(uint16_t trackAndDirection, uint16_t subPosition);
    std::optional<uint16_t> getRoadPlacementMovementPosition(uint16_t trackAndDirection, uint16_t placementPosition);
}
