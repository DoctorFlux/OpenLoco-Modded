#pragma once
#include "CargoDist.h"
#include <iosfwd>
namespace OpenLoco::CargoDist
{
    uint64_t checksum(const std::string& bytes);
    bool writeState(std::ostream& output, const State& state, uint64_t saveHash);
    bool readState(std::istream& input, State& state, uint64_t saveHash, const std::map<Holder, std::pair<uint64_t, uint32_t>>& quantities);
}
