#pragma once
#include "Signals/PathSignals.h"
#include <optional>

namespace OpenLoco::Rules
{
    struct Platform
    {
        uint64_t anchor = 0;
        std::vector<Signals::PieceKey> pieces;
    };
    std::vector<Platform> railPlatforms(uint16_t station);
    uint64_t platformAnchor(const Signals::PieceKey& piece);
    bool platformIsFree(const Platform& platform, uint16_t vehicle);
}
