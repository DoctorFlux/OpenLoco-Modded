#include "CargoDist/CargoDist.h"
#include <algorithm>
#include <limits>
#include <numeric>

namespace OpenLoco::CargoDist
{
    namespace
    {
        void merge(std::vector<Packet>& packets, Packet packet)
        {
            if (packet.quantity == 0)
            {
                return;
            }
            for (auto& existing : packets)
            {
                if (existing.origin == packet.origin && existing.destination == packet.destination && existing.age == packet.age
                    && existing.originX == packet.originX && existing.originY == packet.originY && existing.originRemoved == packet.originRemoved
                    && existing.credit == 0 && packet.credit == 0)
                {
                    existing.quantity += packet.quantity;
                    return;
                }
            }
            packets.push_back(packet);
        }
        uint32_t sum(const Holding& holding)
        {
            uint64_t total = 0;
            for (const auto& packet : holding.packets)
            {
                total += packet.quantity;
            }
            return static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, total));
        }
        uint64_t rank(const Route& route)
        {
            return uint64_t(route.distance) * (100 + uint64_t(route.assigned) * 100 / std::max(1U, route.capacity));
        }
    }
    Packet split(Packet& source, uint32_t count)
    {
        count = std::min(count, source.quantity);
        auto result = source;
        result.quantity = count;
        result.credit = source.quantity ? source.credit / source.quantity * count + source.credit % source.quantity * count / source.quantity : 0;
        source.quantity -= count;
        source.credit -= result.credit;
        return result;
    }
    bool State::configure(const Settings& next)
    {
        if (next.distanceEffect > 255 || next.returning > 100 || next.accuracy < 2 || next.accuracy > 64
            || std::any_of(next.modes.begin(), next.modes.end(), [](Mode mode) { return mode > Mode::asymmetric; }))
        {
            return false;
        }
        if (settings == next)
        {
            return true;
        }
        settings = next;
        for (auto& [holder, holding] : holdings)
        {
            if (settings.modes[holder.cargo] == Mode::manual)
            {
                for (auto& packet : holding.packets)
                {
                    packet.destination = kUnknownStation;
                }
            }
        }
        dirty = true;
        ++revision;
        return true;
    }
    void State::setStation(const Station& station)
    {
        auto it = stations.find(station.id);
        if (it == stations.end() || it->second.company != station.company || it->second.accepts != station.accepts
            || it->second.x != station.x || it->second.y != station.y)
        {
            dirty = true;
        }
        stations[station.id] = station;
    }
    void State::removeStation(uint16_t id)
    {
        stations.erase(id);
        for (auto it = holdings.lower_bound({ false, id, 0, 0 }); it != holdings.end() && !it->first.vehicle && it->first.id == id;)
        {
            totalPackets -= it->second.packets.size();
            it = holdings.erase(it);
        }
        for (auto& [holder, holding] : holdings)
        {
            for (auto& packet : holding.packets)
            {
                if (packet.destination == id)
                {
                    packet.destination = kUnknownStation;
                }
            }
        }
        for (auto& [holder, holding] : holdings)
        {
            for (auto& packet : holding.packets)
            {
                if (packet.origin == id)
                {
                    packet.originRemoved = true;
                }
            }
        }
        std::erase_if(lastVisits, [id](const auto& visit) { return visit.second.first == id; });
        for (auto& [vehicle, links] : services)
        {
            std::erase_if(links, [id](const Link& link) { return link.from == id || link.to == id; });
        }
        std::erase_if(routes, [id](auto& entry) { return entry.first.from == id || entry.first.to == id; });
        for (auto& [key, candidates] : routes)
        {
            std::erase_if(candidates, [id](const Route& route) { return route.next == id; });
        }
        std::erase_if(routes, [](const auto& entry) { return entry.second.empty(); });
        job.reset();
        dirty = true;
        ++revision;
    }
    void State::setService(uint16_t vehicle, const std::vector<Link>& links)
    {
        if (services.contains(vehicle) && services.at(vehicle) == links)
        {
            return;
        }
        size_t edges = links.size();
        for (const auto& [id, previous] : services)
        {
            if (id != vehicle)
            {
                edges += previous.size();
            }
        }
        if (edges > kMaxRoutes || (!services.contains(vehicle) && services.size() >= 1000))
        {
            limited = true;
            return;
        }
        services[vehicle] = links;
        dirty = true;
        ++revision;
    }
    void State::removeService(uint16_t vehicle)
    {
        if (services.erase(vehicle))
        {
            dirty = true;
            ++revision;
        }
    }
    void State::forgetComponent(uint16_t component)
    {
        for (auto it = holdings.lower_bound({ true, component, 0, 0 }); it != holdings.end() && it->first.vehicle && it->first.id == component;)
        {
            totalPackets -= it->second.packets.size();
            it = holdings.erase(it);
        }
    }
    uint32_t State::quantity(Holder holder) const
    {
        const auto it = holdings.find(holder);
        return it == holdings.end() ? 0 : sum(it->second);
    }
    uint32_t State::destined(Holder holder, uint16_t destination) const
    {
        uint32_t count = 0;
        if (auto it = holdings.find(holder); it != holdings.end())
        {
            for (const auto& packet : it->second.packets)
            {
                if (packet.destination == destination)
                {
                    count += packet.quantity;
                }
            }
        }
        return count;
    }
    size_t State::packetCount() const
    {
        size_t count = 0;
        for (const auto& [holder, holding] : holdings)
        {
            count += holding.packets.size();
        }
        return count;
    }
    void State::reconcile(Holder holder, uint64_t identity, uint32_t total, uint16_t origin, uint8_t ageValue)
    {
        if (holder.cargo >= 32)
        {
            return;
        }
        auto& holding = holdings[holder];
        const auto previousPackets = holding.packets.size();
        if (holding.identity != identity)
        {
            holding = { identity, {} };
        }
        auto before = sum(holding);
        if (before < total)
        {
            Packet packet{ total - before, origin, kUnknownStation, ageValue, 0, 0, 0 };
            if (stations.contains(origin))
            {
                packet.originX = stations.at(origin).x;
                packet.originY = stations.at(origin).y;
            }
            if (totalPackets >= kMaxPackets - 64000 && !holding.packets.empty())
            {
                const auto sameOrigin = std::find_if(holding.packets.begin(), holding.packets.end(), [&](const Packet& existing) { return existing.origin == packet.origin; });
                if (sameOrigin != holding.packets.end())
                {
                    sameOrigin->quantity += packet.quantity;
                    sameOrigin->age = std::max(sameOrigin->age, packet.age);
                }
                else
                {
                    merge(holding.packets, packet); // Reserved native-holder space preserves provenance.
                }
                limited = true;
            }
            else
            {
                merge(holding.packets, packet);
            }
        }
        else if (before > total)
        {
            auto loss = before - total;
            for (auto& packet : holding.packets)
            {
                auto removed = split(packet, loss);
                loss -= removed.quantity;
                if (!loss)
                {
                    break;
                }
            }
            std::erase_if(holding.packets, [](const Packet& packet) { return packet.quantity == 0; });
        }
        totalPackets = totalPackets - previousPackets + holding.packets.size();
    }
    void State::age(Holder holder)
    {
        if (auto it = holdings.find(holder); it != holdings.end())
        {
            for (auto& packet : it->second.packets)
            {
                if (holder.vehicle || packet.originRemoved || packet.origin != holder.id)
                {
                    packet.age = static_cast<uint8_t>(std::min(255, packet.age + 1));
                }
            }
        }
    }
    void State::calculate(size_t budget)
    {
        if (!job && dirty)
        {
            job.emplace();
            dirty = false;
            limited = false;
        }
        if (!job)
        {
            return;
        }
        auto& work = *job;
        while (budget && work.phase == 0)
        {
            auto service = services.lower_bound(work.gatherVehicle);
            if (service == services.end())
            {
                work.phase = 1;
                break;
            }
            if (service->first != work.gatherVehicle)
            {
                work.gatherVehicle = service->first;
                work.gatherEdge = 0;
            }
            --budget;
            if (work.gatherEdge >= service->second.size())
            {
                work.gatherVehicle = static_cast<uint16_t>(service->first + 1);
                work.gatherEdge = 0;
                continue;
            }
            const auto& link = service->second[work.gatherEdge++];
            if (link.cargo >= 32 || link.from == link.to || !stations.contains(link.from) || !stations.contains(link.to)
                || !link.capacity || settings.modes[link.cargo] == Mode::manual)
            {
                continue;
            }
            const RouteKey key{ link.company, link.cargo, link.from, link.to };
            if (!work.aggregate.contains(key) && work.aggregate.size() >= kMaxRoutes)
            {
                limited = true;
                continue;
            }
            auto [it, added] = work.aggregate.try_emplace(key, link);
            if (!added)
            {
                it->second.capacity = static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, uint64_t(it->second.capacity) + link.capacity));
                it->second.usage = static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, uint64_t(it->second.usage) + link.usage));
                it->second.travel = std::min(it->second.travel, link.travel);
            }
        }
        while (budget && work.phase == 1)
        {
            if (work.aggregate.empty())
            {
                work.phase = 2;
                break;
            }
            work.links.push_back(work.aggregate.begin()->second);
            work.aggregate.erase(work.aggregate.begin());
            --budget;
        }
        if (work.phase != 2)
        {
            return;
        }
        while (budget && work.task < work.links.size())
        {
            const auto& first = work.links[work.task];
            if (!work.initialised)
            {
                work.distances.clear();
                work.queue.clear();
                work.active = kUnknownStation;
                work.edge = 0;
                work.publishAfter = 0;
                work.distances[first.to] = { first.to, std::max(1U, first.travel), first.capacity, 0 };
                work.queue.emplace(std::max(1U, first.travel), first.to);
                work.initialised = true;
            }
            if (work.active == kUnknownStation)
            {
                if (work.queue.empty())
                {
                    auto publication = work.distances.lower_bound(work.publishAfter);
                    while (budget && publication != work.distances.end())
                    {
                        const auto [destination, route] = *publication++;
                        work.publishAfter = static_cast<uint16_t>(destination + 1);
                        --budget;
                        RouteKey key{ first.company, first.cargo, first.from, destination };
                        if (!work.result.contains(key) && work.result.size() >= kMaxRoutes)
                        {
                            limited = true;
                            continue;
                        }
                        auto& candidates = work.result[key];
                        candidates.push_back(route);
                        std::sort(candidates.begin(), candidates.end(), [](const Route& a, const Route& b) { return std::tie(a.distance, a.next) < std::tie(b.distance, b.next); });
                        if (candidates.size() > 4)
                        {
                            candidates.resize(4);
                        }
                    }
                    if (publication != work.distances.end())
                    {
                        break;
                    }
                    ++work.task;
                    work.initialised = false;
                    continue;
                }
                const auto [distance, node] = *work.queue.begin();
                work.queue.erase(work.queue.begin());
                if (work.distances.at(node).distance != distance)
                {
                    --budget;
                    continue;
                }
                work.active = node;
                const Link start{ first.company, first.cargo, node, 0, 0, 0, 0 };
                work.edge = static_cast<size_t>(std::lower_bound(work.links.begin(), work.links.end(), start) - work.links.begin());
            }
            while (budget && work.edge < work.links.size())
            {
                const auto& link = work.links[work.edge++];
                --budget;
                if (link.company != first.company || link.cargo != first.cargo || link.from != work.active)
                {
                    work.active = kUnknownStation;
                    break;
                }
                if (link.to == first.from)
                {
                    continue;
                }
                const auto& previous = work.distances.at(work.active);
                const auto distance = static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX - 1, uint64_t(previous.distance) + std::max(1U, link.travel)));
                auto old = work.distances.find(link.to);
                if (old == work.distances.end() || old->second.distance > distance)
                {
                    work.distances[link.to] = { first.to, distance, std::min(previous.capacity, link.capacity), 0 };
                    work.queue.emplace(distance, link.to);
                }
            }
            if (work.edge == work.links.size())
            {
                work.active = kUnknownStation;
            }
        }
        if (work.task == work.links.size())
        {
            routes = std::move(work.result);
            job.reset();
        }
    }
    uint32_t State::remainingCost(uint8_t company, uint8_t cargo, uint16_t from, uint16_t destination) const
    {
        if (from == destination)
        {
            return 0;
        }
        const auto it = routes.find({ company, cargo, from, destination });
        if (it == routes.end() || it->second.empty())
        {
            return UINT32_MAX;
        }
        return std::min_element(it->second.begin(), it->second.end(), [](const Route& a, const Route& b) { return a.distance < b.distance; })->distance;
    }
    bool State::usefulHop(uint8_t company, uint8_t cargo, uint16_t from, uint16_t next, uint16_t destination) const
    {
        return stations.contains(next) && remainingCost(company, cargo, next, destination) < remainingCost(company, cargo, from, destination);
    }
    uint16_t State::nextHop(uint8_t company, uint8_t cargo, uint16_t from, uint16_t destination) const
    {
        auto it = routes.find({ company, cargo, from, destination });
        if (it == routes.end() || it->second.empty() || !stations.contains(destination) || cargo >= 32)
        {
            return kUnknownStation;
        }
        const Route* selected = nullptr;
        for (const auto& route : it->second)
        {
            if (usefulHop(company, cargo, from, route.next, destination) && (!selected || std::tuple(rank(route), route.next) < std::tuple(rank(*selected), selected->next)))
            {
                selected = &route;
            }
        }
        return selected ? selected->next : kUnknownStation;
    }
    bool State::assign(Holder source)
    {
        if (source.vehicle || source.cargo >= 32 || !stations.contains(source.id) || settings.modes[source.cargo] == Mode::manual)
        {
            return false;
        }
        auto it = holdings.find(source);
        if (it == holdings.end())
        {
            return false;
        }
        const auto& station = stations.at(source.id);
        std::vector<std::pair<uint16_t, uint64_t>> destinations;
        uint64_t weights = 0;
        for (const auto& [id, target] : stations)
        {
            if (id == source.id || !(target.accepts & (1U << source.cargo)) || nextHop(station.company, source.cargo, source.id, id) == kUnknownStation)
            {
                continue;
            }
            const auto distance = (std::abs(int(target.x) - station.x) + std::abs(int(target.y) - station.y)) / 32;
            const auto supply = settings.modes[source.cargo] == Mode::symmetric ? std::max(1U, target.supply[source.cargo] * settings.returning / 100) : 1U;
            auto weight = std::max<uint64_t>(1, uint64_t(supply) * std::max<uint64_t>(1, 100000 / (100 + uint64_t(distance) * settings.distanceEffect)));
            destinations.emplace_back(id, weight);
            weights += weight;
        }
        if (!weights)
        {
            return false;
        }
        std::vector<Packet> assigned;
        auto originals = it->second.packets;
        auto cursor = it->second.allocationCursor;
        size_t budget = static_cast<size_t>(settings.accuracy) * 16;
        std::vector<uint64_t> cumulative;
        uint64_t sumWeights = 0;
        for (const auto& [id, weight] : destinations)
        {
            sumWeights += weight;
            cumulative.push_back(sumWeights);
        }
        auto reversedBits = [](uint32_t value) {
            value = ((value & 0x55555555U) << 1) | ((value >> 1) & 0x55555555U);
            value = ((value & 0x33333333U) << 2) | ((value >> 2) & 0x33333333U);
            value = ((value & 0x0F0F0F0FU) << 4) | ((value >> 4) & 0x0F0F0F0FU);
            value = ((value & 0x00FF00FFU) << 8) | ((value >> 8) & 0x00FF00FFU);
            return (value << 16) | (value >> 16);
        };
        for (auto& packet : originals)
        {
            if (packet.destination != kUnknownStation && stations.contains(packet.destination)
                && (stations.at(packet.destination).accepts & (1U << source.cargo))
                && (job || dirty || nextHop(station.company, source.cargo, source.id, packet.destination) != kUnknownStation))
            {
                merge(assigned, packet);
                continue;
            }
            if (destinations.size() == 1)
            {
                packet.destination = destinations.front().first;
                merge(assigned, packet);
                continue;
            }
            const auto count = std::min<size_t>(packet.quantity, budget);
            std::vector<uint32_t> counts(destinations.size());
            for (size_t i = 0; i < count; ++i)
            {
                const auto fraction = uint64_t(reversedBits(cursor++));
                const auto point = (weights >> 32) * fraction + (((weights & UINT32_MAX) * fraction) >> 32);
                const auto target = static_cast<size_t>(std::upper_bound(cumulative.begin(), cumulative.end(), point) - cumulative.begin());
                ++counts[target];
            }
            budget -= count;
            for (size_t i = 0; i < counts.size(); ++i)
            {
                auto part = split(packet, counts[i]);
                part.destination = destinations[i].first;
                merge(assigned, part);
            }
            packet.destination = kUnknownStation;
            merge(assigned, packet);
        }
        if (totalPackets - it->second.packets.size() + assigned.size() > kMaxPackets - 64000)
        {
            limited = true;
            return false;
        }
        totalPackets = totalPackets - it->second.packets.size() + assigned.size();
        it->second.packets = std::move(assigned);
        it->second.allocationCursor = cursor;
        return true;
    }
    uint32_t State::available(Holder source, const std::set<uint16_t>& stops) const
    {
        if (!stations.contains(source.id))
        {
            return 0;
        }
        auto it = holdings.find(source);
        if (it == holdings.end())
        {
            return 0;
        }
        uint32_t count = 0;
        for (const auto& packet : it->second.packets)
        {
            if (!stations.contains(packet.destination) || !(stations.at(packet.destination).accepts & (1U << source.cargo)))
            {
                continue;
            }
            const auto route = routes.find({ stations.at(source.id).company, source.cargo, source.id, packet.destination });
            if (route != routes.end() && std::any_of(route->second.begin(), route->second.end(), [&](const Route& candidate) { return (stops.contains(packet.destination) && usefulHop(stations.at(source.id).company, source.cargo, source.id, packet.destination, packet.destination)) || (stops.contains(candidate.next) && usefulHop(stations.at(source.id).company, source.cargo, source.id, candidate.next, packet.destination)); }))
            {
                count += packet.quantity;
            }
        }
        return count;
    }
    uint32_t State::load(Holder source, Holder target, uint32_t capacity, const std::set<uint16_t>& stops)
    {
        if (!stations.contains(source.id) || !holdings.contains(source) || target.cargo != source.cargo || !target.vehicle)
        {
            return 0;
        }
        assign(source);
        auto& from = holdings.at(source).packets;
        auto& to = holdings[target].packets;
        if (totalPackets >= kMaxPackets - 64000)
        {
            limited = true;
            return 0;
        }
        const auto previousPackets = from.size() + to.size();
        uint32_t loaded = 0;
        for (auto& packet : from)
        {
            if (!stations.contains(packet.destination) || !(stations.at(packet.destination).accepts & (1U << source.cargo)))
            {
                continue;
            }
            auto key = RouteKey{ stations.at(source.id).company, source.cargo, source.id, packet.destination };
            auto found = routes.find(key);
            if (found == routes.end() || !capacity)
            {
                continue;
            }
            const bool through = stops.contains(packet.destination) && usefulHop(key.company, key.cargo, key.from, key.to, key.to);
            Route* selected = nullptr;
            for (auto& candidate : found->second)
            {
                if ((through || (stops.contains(candidate.next) && usefulHop(key.company, key.cargo, key.from, candidate.next, key.to)))
                    && (!selected || std::tuple(rank(candidate), candidate.next) < std::tuple(rank(*selected), selected->next)))
                {
                    selected = &candidate;
                }
            }
            if (!selected)
            {
                continue;
            }
            auto part = split(packet, capacity);
            capacity -= part.quantity;
            loaded += part.quantity;
            selected->assigned += part.quantity;
            merge(to, part);
        }
        std::erase_if(from, [](const Packet& packet) { return !packet.quantity; });
        totalPackets = totalPackets - previousPackets + from.size() + to.size();
        return loaded;
    }
    std::vector<PlannedFlow> State::pendingFlows(uint8_t cargoFilter, uint8_t companyFilter, uint16_t fromFilter, size_t budget) const
    {
        std::vector<PlannedFlow> result;
        for (const auto& [holder, holding] : holdings)
        {
            if (holder.vehicle || holding.packets.empty() || !stations.contains(holder.id) || settings.modes[holder.cargo] == Mode::manual)
            {
                continue;
            }
            if ((cargoFilter != 32 && holder.cargo != cargoFilter) || (fromFilter != kUnknownStation && holder.id != fromFilter))
            {
                continue;
            }
            if (!budget)
            {
                break;
            }
            --budget;
            const auto& source = stations.at(holder.id);
            if (companyFilter != 255 && source.company != companyFilter)
            {
                continue;
            }
            std::map<uint16_t, uint32_t> demand;
            uint32_t unknown = 0;
            for (const auto& packet : holding.packets)
            {
                if (packet.destination == kUnknownStation)
                {
                    unknown += packet.quantity;
                }
                else
                {
                    demand[packet.destination] += packet.quantity;
                }
            }
            std::vector<std::pair<uint16_t, uint64_t>> targets;
            uint64_t total = 0;
            for (auto it = routes.lower_bound({ source.company, holder.cargo, holder.id, 0 }); it != routes.end()
                 && it->first.company == source.company && it->first.cargo == holder.cargo && it->first.from == holder.id;
                 ++it)
            {
                if (!budget)
                {
                    break;
                }
                --budget;
                const auto& key = it->first;
                if (!stations.contains(key.to) || !(stations.at(key.to).accepts & (1U << holder.cargo)))
                {
                    continue;
                }
                const auto& destination = stations.at(key.to);
                const auto distance = (std::abs(int(source.x) - destination.x) + std::abs(int(source.y) - destination.y)) / 32;
                const auto supply = settings.modes[holder.cargo] == Mode::symmetric ? std::max(1U, destination.supply[holder.cargo] * settings.returning / 100) : 1U;
                const auto weight = uint64_t(supply) * std::max<uint64_t>(1, 100000 / (100 + uint64_t(distance) * settings.distanceEffect));
                targets.emplace_back(key.to, weight);
                total += weight;
            }
            uint64_t cumulative = 0;
            uint32_t assigned = 0;
            for (auto [id, weight] : targets)
            {
                cumulative += weight;
                const auto count = static_cast<uint32_t>((uint64_t(unknown) * cumulative + total / 2) / total);
                demand[id] += count - assigned;
                assigned = count;
            }
            for (auto [destination, count] : demand)
            {
                const RouteKey key{ source.company, holder.cargo, holder.id, destination };
                const auto hop = nextHop(key.company, key.cargo, key.from, key.to);
                if (hop != kUnknownStation && count)
                {
                    result.push_back({ key, hop, count });
                }
                if (result.size() >= kMaxRoutes)
                {
                    return result;
                }
            }
        }
        return result;
    }
    Unloaded State::unload(Holder source, uint16_t stationId, const std::set<uint16_t>& future, uint32_t room, const std::function<int64_t(const Packet&)>& income)
    {
        Unloaded result;
        if (!source.vehicle || !holdings.contains(source) || !stations.contains(stationId))
        {
            return result;
        }
        const auto& station = stations.at(stationId);
        auto& from = holdings.at(source).packets;
        auto& waiting = holdings[{ false, stationId, source.cargo, 0 }].packets;
        const auto previousPackets = from.size() + waiting.size();
        for (auto& packet : from)
        {
            if (packet.destination != kUnknownStation && (!stations.contains(packet.destination) || !(stations.at(packet.destination).accepts & (1U << source.cargo))))
            {
                packet.destination = kUnknownStation;
            }
            if (packet.destination == kUnknownStation && (station.accepts & (1U << source.cargo)))
            {
                packet.destination = stationId;
            }
            if (packet.destination == stationId && (station.accepts & (1U << source.cargo)))
            {
                result.delivered.push_back(split(packet, packet.quantity));
            }
            else if (!future.contains(packet.destination) && room && totalPackets < kMaxPackets - 64000)
            {
                auto part = split(packet, room);
                room -= part.quantity;
                const auto credit = std::max<int64_t>(0, (income(part) - part.credit) * 75 / 100);
                part.credit += credit;
                result.virtualCredit += credit;
                result.transferred += part.quantity;
                if (!stations.contains(part.destination))
                {
                    part.destination = kUnknownStation;
                }
                merge(waiting, part);
            }
        }
        std::erase_if(from, [](const Packet& packet) { return !packet.quantity; });
        totalPackets = totalPackets - previousPackets + from.size() + waiting.size();
        return result;
    }
    uint8_t congestionColour(uint32_t usage, uint32_t capacity)
    {
        if (!capacity)
        {
            return 0;
        }
        auto percent = uint64_t(usage) * 100 / capacity;
        return percent < 50 ? 1 : percent < 80 ? 2
            : percent < 100                    ? 3
                                               : 4;
    }
    std::string modeName(Mode mode)
    {
        switch (mode)
        {
            case Mode::manual: return "Manual";
            case Mode::symmetric: return "Symmetric";
            case Mode::asymmetric: return "Asymmetric";
        }
        return "Unavailable";
    }
}
