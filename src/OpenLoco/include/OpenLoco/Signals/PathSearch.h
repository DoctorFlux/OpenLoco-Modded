#pragma once

#include <OpenLoco/Engine/World.hpp>
#include <cstdint>
#include <vector>

namespace OpenLoco::Signals
{
    // A track piece entered in one direction: entrance position plus basic track-and-direction.
    struct TrackStep
    {
        World::Pos3 pos{};
        uint16_t tad = 0;

        bool operator==(const TrackStep& rhs) const
        {
            return pos == rhs.pos && tad == rhs.tad;
        }
    };

    // The signal a train meets when it enters a piece in its direction of travel.
    enum class EntrySignal : uint8_t
    {
        none,
        block,
        path,
        oneWayPath,
        chain,
        blockedFromBehind,
    };

    struct StepEdge
    {
        TrackStep step{};
        uint16_t connection = 0;
        uint32_t cost = 32;
        EntrySignal signal = EntrySignal::none;
        bool exitOccupied = false;
        bool blocked = false;
        bool isTarget = false;
        uint32_t distanceToTarget = 0;
        uint8_t entryDirection = 0; // Incoming graph junction, independent of the outgoing piece.
    };

    class TrackGraph
    {
    public:
        virtual ~TrackGraph() = default;
        virtual void successors(const TrackStep& from, std::vector<StepEdge>& out) = 0;
    };

    enum class SearchMode : uint8_t
    {
        pathSignal,
        chainSignal,
        extend,
    };

    struct SearchRequest
    {
        std::vector<StepEdge> starts;
        SearchMode mode = SearchMode::pathSignal;
        bool hasTarget = false;
        bool requireTarget = false; // Waypoints cannot use a fallback that has not proved a route to them.
        uint32_t maxExpansions = 4096;
        uint32_t maxReservedPieces = 64;
    };

    struct SearchResult
    {
        bool found = false;
        bool reachedTarget = false;
        std::vector<StepEdge> reservation;
    };

    SearchResult findReservation(TrackGraph& graph, const SearchRequest& request);
}
