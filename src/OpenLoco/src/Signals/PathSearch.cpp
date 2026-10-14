#include "Signals/PathSearch.h"

#include <algorithm>
#include <array>
#include <map>
#include <queue>
#include <tuple>
#include <utility>

namespace OpenLoco::Signals
{
    namespace
    {
        enum class Stage : uint8_t
        {
            chainArea,
            exitSegment,
            released,
        };

        struct Node
        {
            StepEdge edge;
            int32_t parent = -1;
            Stage stage = Stage::exitSegment;
            uint32_t cost = 0;
            uint32_t reservedPieces = 0;
            std::array<uint64_t, 4> visited{};
            bool reachedTarget = false;
        };

        struct StateKey
        {
            int16_t x;
            int16_t y;
            int16_t z;
            uint16_t tad;
            uint8_t stage;

            bool operator<(const StateKey& rhs) const
            {
                return std::tie(x, y, z, tad, stage) < std::tie(rhs.x, rhs.y, rhs.z, rhs.tad, rhs.stage);
            }
        };

        constexpr uint32_t kBlockedPenalty = 2000;
        constexpr uint32_t kMaxPlatformPieces = 32;

        uint8_t junctionHash(const StepEdge& edge)
        {
            uint64_t hash = static_cast<uint16_t>(edge.step.pos.x) | (uint64_t(static_cast<uint16_t>(edge.step.pos.y)) << 16)
                | (uint64_t(static_cast<uint16_t>(edge.step.pos.z)) << 32) | (uint64_t(edge.entryDirection) << 48);
            hash ^= hash >> 33;
            hash *= 0xff51afd7ed558ccdULL;
            hash ^= hash >> 33;
            return static_cast<uint8_t>(hash);
        }

        bool revisitsJunction(const std::vector<Node>& nodes, int32_t parent, const StepEdge& edge)
        {
            const auto hash = junctionHash(edge);
            if ((nodes[parent].visited[hash / 64] & (uint64_t{ 1 } << (hash % 64))) == 0)
            {
                return false;
            }
            // The bitset only rejects impossible matches cheaply. Hash collisions
            // are checked against the exact ancestry and never reject a valid route.
            for (auto i = parent; i >= 0; i = nodes[i].parent)
            {
                if (nodes[i].edge.step.pos == edge.step.pos && nodes[i].edge.entryDirection == edge.entryDirection)
                {
                    return true;
                }
            }
            return false;
        }

        StateKey keyOf(const Node& node)
        {
            return StateKey{ node.edge.step.pos.x, node.edge.step.pos.y, node.edge.step.pos.z, node.edge.step.tad, static_cast<uint8_t>(node.stage) };
        }

        Stage nextStage(Stage current, const StepEdge& edge, bool& allowed)
        {
            allowed = true;
            if (current == Stage::chainArea)
            {
                switch (edge.signal)
                {
                    case EntrySignal::path:
                    case EntrySignal::oneWayPath:
                        return Stage::exitSegment;
                    case EntrySignal::block:
                        if (edge.exitOccupied)
                        {
                            allowed = false;
                        }
                        return Stage::exitSegment;
                    default:
                        return Stage::chainArea;
                }
            }
            if (current == Stage::exitSegment && edge.signal != EntrySignal::none)
            {
                return Stage::released;
            }
            return current;
        }

        std::vector<StepEdge> buildReservation(const std::vector<Node>& nodes, int32_t index)
        {
            std::vector<StepEdge> path;
            for (int32_t i = index; i >= 0; i = nodes[i].parent)
            {
                if (nodes[i].stage != Stage::released)
                {
                    path.push_back(nodes[i].edge);
                }
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        struct Candidate
        {
            int32_t node = -1;
            uint32_t distance = 0;
            uint32_t cost = 0;
        };
    }

    SearchResult findReservation(TrackGraph& graph, const SearchRequest& request)
    {
        SearchResult result;
        if (request.requireTarget && !request.hasTarget)
        {
            return result;
        }
        std::vector<Node> nodes;
        std::map<StateKey, uint32_t> bestCost;
        using QueueItem = std::pair<uint64_t, int32_t>;
        std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<>> open;

        const auto initialStage = request.mode == SearchMode::chainSignal ? Stage::chainArea : Stage::exitSegment;
        Candidate best;

        const auto complete = [&](int32_t index) {
            result.reachedTarget = true;
            result.reservation = buildReservation(nodes, index);
            result.found = !result.reservation.empty();
        };

        auto considerCandidate = [&](int32_t index) {
            const auto& node = nodes[index];
            const uint32_t distance = request.hasTarget ? node.edge.distanceToTarget : 0;
            if (best.node < 0 || distance < best.distance || (distance == best.distance && node.cost < best.cost))
            {
                best = Candidate{ index, distance, node.cost };
            }
        };

        auto push = [&](Node node) {
            node.reachedTarget = node.edge.isTarget || (node.parent >= 0 && nodes[node.parent].reachedTarget);
            if (request.requireTarget)
            {
                if (node.parent >= 0 && revisitsJunction(nodes, node.parent, node.edge))
                {
                    return;
                }
                if (node.parent >= 0)
                {
                    node.visited = nodes[node.parent].visited;
                }
                const auto hash = junctionHash(node.edge);
                node.visited[hash / 64] |= uint64_t{ 1 } << (hash % 64);
            }
            const auto key = keyOf(node);
            const auto found = bestCost.find(key);
            if (!request.requireTarget && found != bestCost.end() && found->second <= node.cost)
            {
                return;
            }
            // Required paths carry different visited junctions. Collapsing them
            // by endpoint alone can discard a valid non-circular alternative.
            if (!request.requireTarget)
            {
                bestCost[key] = node.cost;
            }
            nodes.push_back(node);
            const uint64_t priority = static_cast<uint64_t>(node.cost) + (request.hasTarget ? node.edge.distanceToTarget : 0);
            open.push({ priority, static_cast<int32_t>(nodes.size() - 1) });
        };

        for (const auto& start : request.starts)
        {
            if (start.blocked || start.signal == EntrySignal::blockedFromBehind || (start.signal == EntrySignal::block && start.exitOccupied))
            {
                continue;
            }
            const auto stage = start.signal == EntrySignal::chain ? Stage::chainArea : initialStage;
            push(Node{ start, -1, stage, start.cost, 1 });
        }

        uint32_t expansions = 0;
        std::vector<StepEdge> successors;
        while (!open.empty() && expansions < request.maxExpansions)
        {
            const auto index = open.top().second;
            open.pop();
            const Node node = nodes[index];
            const auto stale = bestCost.find(keyOf(node));
            if (!request.requireTarget && stale != bestCost.end() && stale->second < node.cost)
            {
                continue;
            }
            ++expansions;

            // A route-through waypoint proves the requested direction, but is
            // not a place to stop safely. Keep checking to the signal boundary
            // even when another train stands just beyond the waypoint.
            if (node.edge.isTarget && (!request.requireTarget || node.stage == Stage::released))
            {
                int32_t end = index;
                if (node.stage != Stage::released)
                {
                    for (uint32_t i = 0; i < kMaxPlatformPieces; ++i)
                    {
                        successors.clear();
                        graph.successors(nodes[end].edge.step, successors);
                        const StepEdge* platform = nullptr;
                        for (const auto& edge : successors)
                        {
                            if (edge.isTarget && !edge.blocked && edge.signal == EntrySignal::none)
                            {
                                platform = &edge;
                                break;
                            }
                        }
                        if (platform == nullptr)
                        {
                            break;
                        }
                        nodes.push_back(Node{ *platform, end, nodes[end].stage, nodes[end].cost + platform->cost, nodes[end].reservedPieces + 1 });
                        end = static_cast<int32_t>(nodes.size() - 1);
                    }
                }
                complete(end);
                return result;
            }

            successors.clear();
            graph.successors(node.edge.step, successors);

            const bool reserveLimit = node.stage != Stage::released && node.reservedPieces >= request.maxReservedPieces;
            if (node.stage != Stage::released)
            {
                const bool deadEnd = std::none_of(successors.begin(), successors.end(), [](const StepEdge& edge) { return edge.signal != EntrySignal::blockedFromBehind; });
                if (node.reachedTarget && request.requireTarget && (deadEnd || reserveLimit))
                {
                    complete(index);
                    return result;
                }
                if (deadEnd || (reserveLimit && !request.requireTarget))
                {
                    considerCandidate(index);
                    continue;
                }
            }

            for (const auto& edge : successors)
            {
                if (edge.signal == EntrySignal::blockedFromBehind)
                {
                    continue;
                }
                bool allowed = true;
                // A required waypoint can lie beyond the bounded reservation.
                // Prove connectivity there without adding more occupied pieces.
                const auto stage = reserveLimit ? Stage::released : nextStage(node.stage, edge, allowed);
                if (!allowed)
                {
                    continue;
                }
                uint32_t cost = edge.cost;
                if (stage != Stage::released)
                {
                    if (edge.blocked)
                    {
                        continue;
                    }
                }
                else if (edge.blocked)
                {
                    cost += kBlockedPenalty;
                }

                if (node.stage != Stage::released && stage == Stage::released)
                {
                    if (node.reachedTarget && request.requireTarget)
                    {
                        complete(index);
                        return result;
                    }
                    considerCandidate(index);
                }

                const uint32_t reserved = stage == Stage::released ? node.reservedPieces : node.reservedPieces + 1;
                push(Node{ edge, index, stage, node.cost + cost, reserved });
            }
        }

        if (best.node >= 0 && !request.requireTarget)
        {
            result.reservation = buildReservation(nodes, best.node);
            result.found = !result.reservation.empty();
        }
        return result;
    }
}
