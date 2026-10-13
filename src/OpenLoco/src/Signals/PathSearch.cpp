#include "Signals/PathSearch.h"

#include <algorithm>
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

        auto considerCandidate = [&](int32_t index) {
            const auto& node = nodes[index];
            const uint32_t distance = request.hasTarget ? node.edge.distanceToTarget : 0;
            if (best.node < 0 || distance < best.distance || (distance == best.distance && node.cost < best.cost))
            {
                best = Candidate{ index, distance, node.cost };
            }
        };

        auto push = [&](const Node& node) {
            const auto key = keyOf(node);
            const auto found = bestCost.find(key);
            if (found != bestCost.end() && found->second <= node.cost)
            {
                return;
            }
            bestCost[key] = node.cost;
            nodes.push_back(node);
            const uint64_t priority = static_cast<uint64_t>(node.cost) + (request.hasTarget ? node.edge.distanceToTarget : 0);
            open.push({ priority, static_cast<int32_t>(nodes.size() - 1) });
        };

        for (const auto& start : request.starts)
        {
            if (start.blocked || start.signal == EntrySignal::blockedFromBehind)
            {
                continue;
            }
            push(Node{ start, -1, initialStage, start.cost, 1 });
        }

        uint32_t expansions = 0;
        std::vector<StepEdge> successors;
        while (!open.empty() && expansions < request.maxExpansions)
        {
            const auto index = open.top().second;
            open.pop();
            const Node node = nodes[index];
            const auto stale = bestCost.find(keyOf(node));
            if (stale != bestCost.end() && stale->second < node.cost)
            {
                continue;
            }
            ++expansions;

            if (node.edge.isTarget)
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
                result.reachedTarget = true;
                result.reservation = buildReservation(nodes, end);
                result.found = !result.reservation.empty();
                return result;
            }

            successors.clear();
            graph.successors(node.edge.step, successors);

            const bool reserveLimit = node.stage != Stage::released && node.reservedPieces >= request.maxReservedPieces;
            if (node.stage != Stage::released)
            {
                const bool deadEnd = std::none_of(successors.begin(), successors.end(), [](const StepEdge& edge) { return edge.signal != EntrySignal::blockedFromBehind; });
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
