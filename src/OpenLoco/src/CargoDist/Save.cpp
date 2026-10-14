#include "CargoDist/Save.h"
#include "CargoDist/Game.h"
#include "Entities/EntityManager.h"
#include "Vehicles/VehicleHead.h"
#include "World/StationManager.h"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <locale>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace OpenLoco::CargoDist
{
    namespace
    {
        constexpr size_t kMaxBytes = 64 * 1024 * 1024;
        struct Reader
        {
            std::istream& in;
            bool ok = true;
            template<typename T>
            void number(T& destination, uint64_t maximum = UINT64_MAX)
            {
                std::string word;
                uint64_t value = 0;
                if (!ok || !(in >> word) || word.size() > 20)
                {
                    ok = false;
                    return;
                }
                const auto parsed = std::from_chars(word.data(), word.data() + word.size(), value);
                if (parsed.ec != std::errc{} || parsed.ptr != word.data() + word.size() || value > maximum)
                {
                    ok = false;
                    return;
                }
                destination = static_cast<T>(value);
            }
            void signedNumber(int64_t& destination)
            {
                std::string word;
                if (!ok || !(in >> word) || word.size() > 21)
                {
                    ok = false;
                    return;
                }
                const auto parsed = std::from_chars(word.data(), word.data() + word.size(), destination);
                ok = parsed.ec == std::errc{} && parsed.ptr == word.data() + word.size();
            }
            void coord(int16_t& destination)
            {
                int64_t value = 0;
                signedNumber(value);
                if (value < INT16_MIN || value > INT16_MAX)
                {
                    ok = false;
                }
                destination = static_cast<int16_t>(value);
            }
        };
        void writeLink(std::ostream& out, const Link& link)
        {
            out << unsigned(link.company) << ' ' << unsigned(link.cargo) << ' ' << link.from << ' ' << link.to << ' ' << link.capacity << ' ' << link.travel << ' ' << link.usage << '\n';
        }
        Link readLink(Reader& reader)
        {
            Link link;
            reader.number(link.company, 14);
            reader.number(link.cargo, 31);
            reader.number(link.from, 1023);
            reader.number(link.to, 1023);
            reader.number(link.capacity, UINT32_MAX);
            reader.number(link.travel, UINT32_MAX);
            reader.number(link.usage, UINT32_MAX);
            if (link.from == link.to || link.capacity == 0 || link.travel == 0 || link.usage > link.capacity)
            {
                reader.ok = false;
            }
            return link;
        }
        void writeRoute(std::ostream& out, const Route& route)
        {
            out << route.next << ' ' << route.distance << ' ' << route.capacity << ' ' << route.assigned << ' ';
        }
        Route readRoute(Reader& reader)
        {
            Route route;
            reader.number(route.next, 1023);
            reader.number(route.distance, UINT32_MAX);
            reader.number(route.capacity, UINT32_MAX);
            reader.number(route.assigned, UINT32_MAX);
            if (!route.capacity || !route.distance)
            {
                reader.ok = false;
            }
            return route;
        }
        void writeRoutes(std::ostream& out, const std::map<RouteKey, std::vector<Route>>& routes)
        {
            out << routes.size() << '\n';
            for (const auto& [key, candidates] : routes)
            {
                out << unsigned(key.company) << ' ' << unsigned(key.cargo) << ' ' << key.from << ' ' << key.to << ' ' << candidates.size() << ' ';
                for (const auto& route : candidates)
                {
                    writeRoute(out, route);
                }
                out << '\n';
            }
        }
        std::map<RouteKey, std::vector<Route>> readRoutes(Reader& reader)
        {
            std::map<RouteKey, std::vector<Route>> routes;
            size_t count = 0;
            reader.number(count, kMaxRoutes);
            for (size_t i = 0; i < count && reader.ok; ++i)
            {
                RouteKey key;
                size_t candidates = 0;
                reader.number(key.company, 14);
                reader.number(key.cargo, 31);
                reader.number(key.from, 1023);
                reader.number(key.to, 1023);
                reader.number(candidates, 4);
                if (!candidates || routes.contains(key) || key.from == key.to)
                {
                    reader.ok = false;
                    break;
                }
                std::set<uint16_t> hops;
                auto& entries = routes[key];
                for (size_t j = 0; j < candidates && reader.ok; ++j)
                {
                    auto route = readRoute(reader);
                    if (!hops.insert(route.next).second)
                    {
                        reader.ok = false;
                    }
                    entries.push_back(route);
                }
            }
            return routes;
        }
        std::optional<uint64_t> hashFile(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                return std::nullopt;
            }
            uint64_t hash = 14695981039346656037ULL;
            std::array<char, 65536> buffer;
            while (stream)
            {
                stream.read(buffer.data(), buffer.size());
                for (std::streamsize i = 0; i < stream.gcount(); ++i)
                {
                    hash ^= static_cast<unsigned char>(buffer[i]);
                    hash *= 1099511628211ULL;
                }
            }
            return stream.bad() ? std::nullopt : std::optional{ hash };
        }
    }
    uint64_t checksum(const std::string& bytes)
    {
        uint64_t hash = 14695981039346656037ULL;
        for (unsigned char byte : bytes)
        {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        return hash;
    }
    bool writeState(std::ostream& output, const State& state, uint64_t saveHash)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        for (auto mode : state.settings.modes)
        {
            out << unsigned(mode) << ' ';
        }
        out << state.settings.distanceEffect << ' ' << unsigned(state.settings.returning) << ' ' << unsigned(state.settings.accuracy) << '\n';
        out << state.revision << ' ' << state.dirty << ' ' << state.limited << '\n';
        size_t holdings = 0;
        for (const auto& [holder, holding] : state.holdings)
        {
            if (!holding.packets.empty() || holding.allocationCursor)
            {
                ++holdings;
            }
        }
        out << holdings << '\n';
        for (const auto& [holder, holding] : state.holdings)
        {
            if (holding.packets.empty() && !holding.allocationCursor)
            {
                continue;
            }
            out << holder.vehicle << ' ' << holder.id << ' ' << unsigned(holder.cargo) << ' ' << unsigned(holder.slot) << ' ' << holding.identity << ' ' << holding.allocationCursor << ' ' << holding.packets.size() << '\n';
            for (const auto& packet : holding.packets)
            {
                out << packet.quantity << ' ' << packet.origin << ' ' << packet.destination << ' ' << unsigned(packet.age) << ' ' << packet.credit << ' ' << packet.originX << ' ' << packet.originY << ' ' << packet.originRemoved << '\n';
            }
        }
        out << state.stations.size() << '\n';
        for (const auto& [id, station] : state.stations)
        {
            out << id << ' ' << unsigned(station.company) << ' ' << station.x << ' ' << station.y << ' ' << station.accepts << ' ';
            for (auto supply : station.supply)
            {
                out << supply << ' ';
            }
            out << '\n';
        }
        out << state.services.size() << '\n';
        for (const auto& [vehicle, links] : state.services)
        {
            out << vehicle << ' ' << links.size() << '\n';
            for (const auto& link : links)
            {
                writeLink(out, link);
            }
        }
        writeRoutes(out, state.routes);
        out << state.lastVisits.size() << '\n';
        for (const auto& [head, visit] : state.lastVisits)
        {
            out << head << ' ' << visit.first << ' ' << visit.second << '\n';
        }
        out << state.incomeAdjustments.size() << '\n';
        for (const auto& [head, value] : state.incomeAdjustments)
        {
            out << head << ' ' << value << '\n';
        }
        out << state.job.has_value() << '\n';
        if (state.job)
        {
            const auto& job = *state.job;
            out << unsigned(job.phase) << ' ' << job.gatherVehicle << ' ' << job.gatherEdge << ' ' << job.publishAfter << ' ' << job.aggregate.size() << '\n';
            for (const auto& [key, link] : job.aggregate)
            {
                writeLink(out, link);
            }
            out << job.links.size() << ' ' << job.task << ' ' << job.active << ' ' << job.edge << ' ' << job.initialised << '\n';
            for (const auto& link : job.links)
            {
                writeLink(out, link);
            }
            out << job.distances.size() << '\n';
            for (const auto& [id, route] : job.distances)
            {
                out << id << ' ';
                writeRoute(out, route);
                out << '\n';
            }
            out << job.queue.size() << '\n';
            for (auto [distance, id] : job.queue)
            {
                out << distance << ' ' << id << '\n';
            }
            writeRoutes(out, job.result);
        }
        const auto payload = out.str();
        if (payload.size() > kMaxBytes || !out || state.packetCount() > kMaxPackets)
        {
            return false;
        }
        output.imbue(std::locale::classic());
        output << "OCARGODIST 1 " << saveHash << ' ' << checksum(payload) << ' ' << payload.size() << '\n'
               << payload;
        return output.good();
    }
    bool readState(std::istream& input, State& result, uint64_t saveHash, const std::map<Holder, std::pair<uint64_t, uint32_t>>& native)
    {
        std::string header;
        if (!std::getline(input, header) || header.size() > 128)
        {
            return false;
        }
        std::istringstream headerStream(header);
        std::string magic;
        headerStream >> magic;
        Reader headerReader{ headerStream };
        uint64_t version = 0, hash = 0, payloadHash = 0, bytes = 0;
        headerReader.number(version, 1);
        headerReader.number(hash);
        headerReader.number(payloadHash);
        headerReader.number(bytes, kMaxBytes);
        headerStream >> std::ws;
        if (!headerReader.ok || !headerStream.eof() || magic != "OCARGODIST" || version != 1 || hash != saveHash)
        {
            return false;
        }
        std::string payload(static_cast<size_t>(bytes), '\0');
        input.read(payload.data(), static_cast<std::streamsize>(payload.size()));
        if (input.gcount() != static_cast<std::streamsize>(payload.size()) || input.peek() != std::char_traits<char>::eof() || checksum(payload) != payloadHash)
        {
            return false;
        }
        std::istringstream in(payload);
        in.imbue(std::locale::classic());
        Reader reader{ in };
        State state;
        for (auto& mode : state.settings.modes)
        {
            reader.number(mode, 2);
        }
        reader.number(state.settings.distanceEffect, 255);
        reader.number(state.settings.returning, 100);
        reader.number(state.settings.accuracy, 64);
        if (state.settings.accuracy < 2)
        {
            return false;
        }
        reader.number(state.revision, UINT32_MAX);
        reader.number(state.dirty, 1);
        reader.number(state.limited, 1);
        size_t count = 0, packets = 0;
        reader.number(count, 64000);
        for (size_t i = 0; i < count && reader.ok; ++i)
        {
            Holder holder;
            Holding holding;
            size_t size = 0;
            reader.number(holder.vehicle, 1);
            reader.number(holder.id, 19799);
            reader.number(holder.cargo, 31);
            reader.number(holder.slot, 1);
            reader.number(holding.identity);
            reader.number(holding.allocationCursor, UINT32_MAX);
            reader.number(size, kMaxPackets);
            packets += size;
            if ((!size && !holding.allocationCursor) || packets > kMaxPackets || state.holdings.contains(holder) || !native.contains(holder) || native.at(holder).first != holding.identity)
            {
                return false;
            }
            uint64_t total = 0;
            for (size_t j = 0; j < size && reader.ok; ++j)
            {
                Packet packet;
                reader.number(packet.quantity, UINT16_MAX);
                reader.number(packet.origin, UINT16_MAX);
                reader.number(packet.destination, UINT16_MAX);
                reader.number(packet.age, 255);
                reader.signedNumber(packet.credit);
                reader.coord(packet.originX);
                reader.coord(packet.originY);
                reader.number(packet.originRemoved, 1);
                if (!packet.quantity || packet.credit < 0 || packet.credit > INT32_MAX || (packet.origin != kUnknownStation && packet.origin >= 1024)
                    || (packet.destination != kUnknownStation && packet.destination >= 1024))
                {
                    return false;
                }
                total += packet.quantity;
                holding.packets.push_back(packet);
            }
            if (total != native.at(holder).second)
            {
                return false;
            }
            state.holdings.emplace(holder, std::move(holding));
        }
        for (const auto& [holder, quantity] : native)
        {
            if (quantity.second && !state.holdings.contains(holder))
            {
                return false;
            }
        }
        reader.number(count, 1024);
        for (size_t i = 0; i < count && reader.ok; ++i)
        {
            Station station;
            reader.number(station.id, 1023);
            reader.number(station.company, 14);
            reader.coord(station.x);
            reader.coord(station.y);
            reader.number(station.accepts, UINT32_MAX);
            for (auto& supply : station.supply)
            {
                reader.number(supply, UINT16_MAX);
            }
            if (!state.stations.emplace(station.id, station).second)
            {
                return false;
            }
        }
        reader.number(count, 1000);
        size_t edges = 0;
        for (size_t i = 0; i < count && reader.ok; ++i)
        {
            uint16_t vehicle = 0;
            size_t size = 0;
            reader.number(vehicle, 19799);
            reader.number(size, kMaxRoutes);
            edges += size;
            if (edges > kMaxRoutes || state.services.contains(vehicle))
            {
                return false;
            }
            auto& links = state.services[vehicle];
            for (size_t j = 0; j < size && reader.ok; ++j)
            {
                links.push_back(readLink(reader));
            }
        }
        state.routes = readRoutes(reader);
        reader.number(count, 1000);
        for (size_t i = 0; i < count && reader.ok; ++i)
        {
            uint16_t head = 0, station = 0;
            uint32_t tick = 0;
            reader.number(head, 19799);
            reader.number(station, 1023);
            reader.number(tick, UINT32_MAX);
            if (!state.lastVisits.emplace(head, std::pair{ station, tick }).second)
            {
                return false;
            }
        }
        reader.number(count, 1000);
        for (size_t i = 0; i < count && reader.ok; ++i)
        {
            uint16_t head = 0;
            int64_t value = 0;
            reader.number(head, 19799);
            reader.signedNumber(value);
            if (!state.incomeAdjustments.emplace(head, value).second)
            {
                return false;
            }
        }
        bool pending = false;
        reader.number(pending, 1);
        if (pending)
        {
            Job job;
            size_t size = 0;
            reader.number(job.phase, 2);
            reader.number(job.gatherVehicle, 19800);
            reader.number(job.gatherEdge, kMaxRoutes);
            reader.number(job.publishAfter, 1024);
            reader.number(size, kMaxRoutes);
            for (size_t i = 0; i < size && reader.ok; ++i)
            {
                const auto link = readLink(reader);
                if (!job.aggregate.emplace(RouteKey{ link.company, link.cargo, link.from, link.to }, link).second)
                {
                    return false;
                }
            }
            reader.number(size, kMaxRoutes);
            reader.number(job.task, size);
            reader.number(job.active, UINT16_MAX);
            reader.number(job.edge, size);
            reader.number(job.initialised, 1);
            for (size_t i = 0; i < size && reader.ok; ++i)
            {
                job.links.push_back(readLink(reader));
            }
            if (!std::is_sorted(job.links.begin(), job.links.end()) || (job.active != kUnknownStation && job.active >= 1024)
                || (job.phase == 2 && !job.aggregate.empty()) || (job.phase < 2 && (job.task || job.initialised)))
            {
                return false;
            }
            reader.number(size, 1024);
            for (size_t i = 0; i < size && reader.ok; ++i)
            {
                uint16_t id = 0;
                reader.number(id, 1023);
                auto route = readRoute(reader);
                if (!job.distances.emplace(id, route).second)
                {
                    return false;
                }
            }
            if (job.active != kUnknownStation && !job.distances.contains(job.active))
            {
                return false;
            }
            reader.number(size, kMaxRoutes);
            for (size_t i = 0; i < size && reader.ok; ++i)
            {
                uint32_t distance = 0;
                uint16_t id = 0;
                reader.number(distance, UINT32_MAX);
                reader.number(id, 1023);
                if (!job.distances.contains(id) || !job.queue.emplace(distance, id).second)
                {
                    return false;
                }
            }
            job.result = readRoutes(reader);
            state.job = std::move(job);
        }
        in >> std::ws;
        if (!reader.ok || !in.eof())
        {
            return false;
        }
        auto validLink = [&](const Link& link) { return state.stations.contains(link.from) && state.stations.contains(link.to); };
        for (const auto& [vehicle, links] : state.services)
        {
            if (!std::all_of(links.begin(), links.end(), validLink))
            {
                return false;
            }
        }
        for (const auto& [holder, holding] : state.holdings)
        {
            for (const auto& packet : holding.packets)
            {
                if (packet.destination != kUnknownStation && !state.stations.contains(packet.destination))
                {
                    return false;
                }
            }
        }
        auto validRoutes = [&](const auto& routes) {
            for (const auto& [key, candidates] : routes)
            {
                if (!state.stations.contains(key.from) || !state.stations.contains(key.to))
                {
                    return false;
                }
                for (const auto& candidate : candidates)
                {
                    if (!state.stations.contains(candidate.next))
                    {
                        return false;
                    }
                }
            }
            return true;
        };
        if (!validRoutes(state.routes) || (state.job && (!validRoutes(state.job->result) || !std::all_of(state.job->links.begin(), state.job->links.end(), validLink) || !std::all_of(state.job->aggregate.begin(), state.job->aggregate.end(), [&](const auto& entry) { return validLink(entry.second); }))))
        {
            return false;
        }
        for (const auto& [holder, value] : native)
        {
            state.holdings.try_emplace(holder, Holding{ value.first, {}, 0 });
        }
        state.totalPackets = state.packetCount();
        result = std::move(state);
        return true;
    }
    std::filesystem::path companionPath(const std::filesystem::path& save)
    {
        auto path = save;
        path += ".ocargodist";
        return path;
    }
    bool saveCompanion(const std::filesystem::path& save)
    {
        const auto hash = hashFile(save);
        if (!hash)
        {
            return false;
        }
        const auto native = nativeQuantities();
        auto& live = gameState();
        live.totalPackets = live.packetCount();
        std::erase_if(live.holdings, [&](const auto& entry) { return !native.contains(entry.first); });
        for (const auto& [holder, value] : native)
        {
            if (live.quantity(holder) != value.second || (live.holdings.contains(holder) && live.holdings.at(holder).identity != value.first))
            {
                live.reconcile(holder, value.first, value.second, kUnknownStation, 0);
            }
        }
        live.totalPackets = live.packetCount();
        auto path = companionPath(save), temporary = path;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!writeState(output, live, *hash))
            {
                return false;
            }
            output.close();
            if (output.fail())
            {
                return false;
            }
        }
#ifdef _WIN32
        return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        return !error;
#endif
    }
    bool loadCompanion(const std::filesystem::path& save)
    {
        reset();
        const auto native = nativeQuantities();
        auto path = companionPath(save);
        std::error_code error;
        if (!std::filesystem::exists(path, error))
        {
            return !error;
        }
        const auto hash = hashFile(save);
        if (!hash)
        {
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        State candidate;
        if (!readState(input, candidate, *hash, native))
        {
            return false;
        }
        for (const auto& [id, station] : candidate.stations)
        {
            const auto* current = StationManager::get(static_cast<StationId>(id));
            if (!current || current->empty() || enumValue(current->owner) != station.company || current->x != station.x || current->y != station.y)
            {
                return false;
            }
        }
        const auto validHead = [](uint16_t id) {
            const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(id));
            return head && head->isVehicleHead();
        };
        for (const auto& [id, links] : candidate.services)
        {
            if (!validHead(id))
            {
                return false;
            }
            const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(id));
            if (!std::all_of(links.begin(), links.end(), [&](const Link& link) { return link.company == enumValue(head->owner); }))
            {
                return false;
            }
        }
        for (const auto& [id, value] : candidate.incomeAdjustments)
        {
            if (!validHead(id))
            {
                return false;
            }
        }
        for (const auto& [id, value] : candidate.lastVisits)
        {
            if (!validHead(id) || !candidate.stations.contains(value.first))
            {
                return false;
            }
        }
        replaceState(std::move(candidate));
        return true;
    }
}
