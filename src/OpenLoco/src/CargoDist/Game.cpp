#include "CargoDist/Game.h"
#include "Date.h"
#include "Entities/EntityManager.h"
#include "GameCommands/GameCommands.h"
#include "Lines/Line.h"
#include "Objects/CargoObject.h"
#include "Objects/ObjectManager.h"
#include "Rules/Rules.h"
#include "Scenario/ScenarioManager.h"
#include "Vehicles/OrderManager.h"
#include "Vehicles/Orders.h"
#include "Vehicles/Vehicle1.h"
#include "Vehicles/Vehicle2.h"
#include "Vehicles/VehicleBody.h"
#include "Vehicles/VehicleBogie.h"
#include "Vehicles/VehicleHead.h"
#include "Vehicles/VehicleManager.h"
#include "World/CompanyManager.h"
#include "World/StationManager.h"
#include <OpenLoco/Diagnostics/Logging.h>
#include <OpenLoco/Math/Vector.hpp>
#include <algorithm>
#include <tuple>

namespace OpenLoco::CargoDist
{
    namespace
    {
        State _state;
        uint64_t _generation = 1;
        bool _booted = false, _nativeDelivery = false;
        int64_t _deliveryCredit = 0;
        std::optional<World::Pos2> _deliveryOrigin;
        uint16_t _pollStation = 0;
        EntityId _pollVehicle = EntityId::null;
        std::map<uint16_t, uint64_t> _componentEpochs, _stationEpochs;
        struct ServiceContext
        {
            Rules::Program program;
            std::map<Rules::Key, std::map<uint8_t, std::set<Rules::Key>>> rides;
        };
        using LinkKey = std::tuple<uint8_t, uint8_t, uint16_t, uint16_t>;
        struct ServiceBuild
        {
            ServiceContext context;
            uint64_t signature = 0;
            std::array<uint32_t, 32> capacity{};
            std::vector<Rules::Key> stops;
            std::map<Rules::Key, std::set<Rules::Key>> onward;
            std::map<LinkKey, Link> links;
            size_t source = 0;
            uint8_t cargo = 0;
            bool running = false;
            std::set<std::tuple<uint32_t, Rules::Key, bool>> queue;
            std::map<std::pair<Rules::Key, bool>, uint32_t> best;
            std::optional<std::tuple<uint32_t, Rules::Key, bool>> active;
            Rules::Key edge = 0;
        };
        std::map<uint16_t, ServiceBuild> _serviceBuilds;
        struct ReachedStop
        {
            Rules::Owner owner;
            Rules::Key key = 0;
            uint32_t revision = 0;
            uint64_t signature = 0;
            uint16_t station = kUnknownStation;
        };
        std::map<uint16_t, ServiceContext> _services;
        std::map<uint16_t, ReachedStop> _reached;
        constexpr uint8_t orderSizes[] = { 1, 2, 2, 6, 1, 1 };
        size_t rowAtOffset(const Rules::Program& p, uint16_t offset)
        {
            uint16_t position = 0;
            for (size_t i = 0; i < p.rows.size(); ++i)
            {
                if (position == offset)
                {
                    return i;
                }
                position += orderSizes[p.rows[i].raw & 7];
            }
            return p.rows.size();
        }
        uint16_t stationOf(const Rules::Program& p, Rules::Key key)
        {
            const auto index = Rules::indexOf(p, key);
            if (index >= p.rows.size() || (p.rows[index].raw & 7) != 1)
            {
                return kUnknownStation;
            }
            return enumValue(reinterpret_cast<const Vehicles::OrderStation*>(&p.rows[index].raw)->getStation());
        }
        std::map<uint16_t, uint64_t> _signatures;
        uint64_t identity(const OpenLoco::Station& station)
        {
            return (uint64_t(enumValue(station.owner) + 1) << 48) | (uint64_t(uint16_t(station.x)) << 24) | uint16_t(station.y);
        }
        uint64_t identity(const Vehicles::VehicleBody& body)
        {
            return (uint64_t(body.creationDay + 1) << 32) | (uint64_t(body.objectId) << 16) | enumValue(body.id);
        }
        uint64_t identity(const Vehicles::VehicleBogie& bogie)
        {
            return (uint64_t(bogie.creationDay + 1) << 32) | (uint64_t(bogie.objectId) << 16) | enumValue(bogie.id);
        }
        template<typename Fn>
        void compartments(const Vehicles::VehicleHead& head, Fn&& fn)
        {
            Vehicles::Vehicle train(head);
            for (auto& car : train.cars)
            {
                for (auto& part : car)
                {
                    fn(part.body->primaryCargo, Holder{ true, enumValue(part.body->id), part.body->primaryCargo.type, 0 }, identity(*part.body));
                    fn(part.front->secondaryCargo, Holder{ true, enumValue(part.front->id), part.front->secondaryCargo.type, 1 }, identity(*part.front));
                    fn(part.back->secondaryCargo, Holder{ true, enumValue(part.back->id), part.back->secondaryCargo.type, 1 }, identity(*part.back));
                }
            }
        }
        void syncVehicle(const Vehicles::VehicleHead& head)
        {
            compartments(head, [](auto& cargo, Holder holder, uint64_t stamp) {
                for (auto it = _state.holdings.lower_bound({ true, holder.id, 0, 0 }); it != _state.holdings.end() && it->first.vehicle && it->first.id == holder.id;)
                {
                    if (it->first.cargo != holder.cargo || it->first.slot != holder.slot)
                    {
                        _state.totalPackets -= it->second.packets.size();
                        it = _state.holdings.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
                if (holder.cargo < 32)
                {
                    _state.reconcile(holder, stamp, cargo.qty, enumValue(cargo.townFrom), cargo.numDays);
                }
            });
        }
        std::optional<Holder> holderFor(const Vehicles::VehicleHead& head, const Vehicles::VehicleCargo& cargo)
        {
            std::optional<Holder> found;
            compartments(head, [&](auto& native, Holder holder, uint64_t) { if (&native == &cargo){ found = holder;
} });
            return found;
        }
        void bootstrap()
        {
            if (_booted)
            {
                return;
            }
            _booted = true;
            for (uint8_t type = 0; type < 32; ++type)
            {
                if (const auto* object = ObjectManager::get<CargoObject>(type); object && (object->cargoCategory == CargoCategory::passengers || object->cargoCategory == CargoCategory::mail))
                {
                    _state.settings.modes[type] = Mode::symmetric;
                }
            }
            for (const auto& station : StationManager::stations())
            {
                syncStation(station);
            }
            for (auto& [holder, holding] : _state.holdings)
            {
                for (auto& packet : holding.packets)
                {
                    if (_state.stations.contains(packet.origin))
                    {
                        packet.originX = _state.stations.at(packet.origin).x;
                        packet.originY = _state.stations.at(packet.origin).y;
                    }
                }
            }
            for (auto* head : VehicleManager::VehicleList())
            {
                syncVehicle(*head);
            }
        }
        bool filterAllows(const Vehicles::VehicleHead& head, const Rules::Program& p, Rules::Key key, uint8_t cargo, bool loading)
        {
            const auto index = Rules::indexOf(p, key);
            if (index >= p.rows.size())
            {
                return false;
            }
            if (const auto* line = Lines::lineForVehicle(enumValue(head.id)); line && index < line->orders.size())
            {
                for (const auto& filter : line->orders[index].filters)
                {
                    if (filter.cargoType == cargo && !(loading ? filter.load : filter.unload))
                    {
                        return false;
                    }
                }
            }
            if (loading)
            {
                for (size_t row = index + 1; row < p.rows.size() && ((p.rows[row].raw & 7) == 4 || (p.rows[row].raw & 7) == 5); ++row)
                {
                    const auto* order = reinterpret_cast<const Vehicles::Order*>(&p.rows[row].raw);
                    const auto* unload = order->as<Vehicles::OrderUnloadAll>();
                    // Conditional cargo instructions are enforced by the actual cargo phase.
                    const bool conditional = std::any_of(p.blocks.begin(), p.blocks.end(), [&](const auto& block) {
                        return block.enabled && (block.kind == Rules::Kind::ifBlock || block.kind == Rules::Kind::elseIf || block.kind == Rules::Kind::otherwise) && Rules::indexOf(p, block.first) <= row && row <= Rules::indexOf(p, block.last);
                    });
                    if (unload && unload->getCargo() == cargo && !conditional)
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        Rules::Key reachedKey(const Vehicles::VehicleHead& head, const Rules::Program& p)
        {
            const auto id = enumValue(head.id), station = enumValue(head.stationId);
            const auto owner = Rules::ownerForVehicle(id);
            if (auto it = _reached.find(id); it != _reached.end() && it->second.owner == owner && it->second.revision == p.revision && (_serviceBuilds.contains(id) || _signatures.contains(id)) && it->second.signature == (_serviceBuilds.contains(id) ? _serviceBuilds.at(id).signature : _signatures.at(id)) && it->second.station == station && stationOf(p, it->second.key) == station)
            {
                return it->second.key;
            }
            // Restore saved/native cargo phases without guessing between repeated stops.
            const auto committed = Rules::routeOrderForVehicle(id, &p);
            if (Rules::runtimeForVehicle(id) && stationOf(p, committed) == station)
            {
                return committed;
            }
            auto row = rowAtOffset(p, head.currentOrder);
            if (row < p.rows.size() && stationOf(p, p.rows[row].key) == station)
            {
                auto previous = row;
                for (size_t count = 0; count < p.rows.size(); ++count)
                {
                    previous = (previous + p.rows.size() - 1) % p.rows.size();
                    if ((p.rows[previous].raw & 7) == 4 || (p.rows[previous].raw & 7) == 5)
                    {
                        continue;
                    }
                    if (previous != row && stationOf(p, p.rows[previous].key) == station)
                    {
                        return 0;
                    }
                    break;
                }
                return p.rows[row].key;
            }
            if ((row < p.rows.size() || head.currentOrder == head.sizeOfOrderTable - 1) && !p.rows.empty())
            {
                for (size_t count = 0; count < p.rows.size(); ++count)
                {
                    row = (row + p.rows.size() - 1) % p.rows.size();
                    if ((p.rows[row].raw & 7) == 4 || (p.rows[row].raw & 7) == 5)
                    {
                        continue;
                    }
                    if (stationOf(p, p.rows[row].key) == station)
                    {
                        return p.rows[row].key;
                    }
                    break;
                }
            }
            Rules::Key only = 0;
            for (const auto& stop : p.rows)
            {
                if (stationOf(p, stop.key) != station)
                {
                    continue;
                }
                if (only)
                {
                    return 0;
                }
                only = stop.key;
            }
            return only;
        }
        std::set<uint16_t> rideStops(const Vehicles::VehicleHead& head, uint8_t cargo, bool loading)
        {
            const auto id = enumValue(head.id);
            if (_serviceBuilds.contains(id) || !_services.contains(id))
            {
                return {};
            }
            const auto& service = _services.at(id);
            const auto key = reachedKey(head, service.program);
            if (!key || (loading && !filterAllows(head, service.program, key, cargo, true)))
            {
                return {};
            }
            const auto start = service.rides.find(key);
            if (start == service.rides.end() || !start->second.contains(cargo))
            {
                return {};
            }
            std::set<uint16_t> result;
            for (auto target : start->second.at(cargo))
            {
                result.insert(stationOf(service.program, target));
            }
            return result;
        }
        bool currentFilterAllows(const Vehicles::VehicleHead& head, uint8_t cargo, bool loading)
        {
            const auto id = enumValue(head.id);
            if (!_serviceBuilds.contains(id) && !_services.contains(id))
            {
                return false;
            }
            const auto& p = _serviceBuilds.contains(id) ? _serviceBuilds.at(id).context.program : _services.at(id).program;
            const auto key = reachedKey(head, p);
            return key && filterAllows(head, p, key, cargo, loading);
        }
        bool forceUnload(const Vehicles::VehicleHead& head, uint8_t cargo)
        {
            for (const auto& order : head.getCurrentOrders())
            {
                if (!order.hasFlags(Vehicles::OrderFlags::HasCargo))
                {
                    break;
                }
                if (!Rules::cargoOrderEnabled(enumValue(head.id), static_cast<uint16_t>(order.getOffset() - head.orderTableOffset)))
                {
                    continue;
                }
                auto* unload = order.as<Vehicles::OrderUnloadAll>();
                if (unload && unload->getCargo() == cargo)
                {
                    return true;
                }
            }
            return false;
        }
        bool advanceServiceBuild(const Vehicles::VehicleHead& head, ServiceBuild& work, size_t& budget)
        {
            const auto& program = work.context.program;
            auto nextPair = [&] { work.running = false; work.active.reset(); if (++work.cargo == 32) { work.cargo = 0; ++work.source; } };
            while (work.source < work.stops.size() && budget)
            {
                const auto from = work.stops[work.source];
                const auto fromStation = stationOf(program, from);
                if (!work.running)
                {
                    --budget;
                    if (!work.capacity[work.cargo] || !_state.stations.contains(fromStation))
                    {
                        nextPair();
                        continue;
                    }
                    work.queue = { { 0, from, true } };
                    work.best = { { { from, true }, 0 } };
                    work.running = true;
                }
                if (!work.active)
                {
                    if (work.queue.empty())
                    {
                        nextPair();
                        continue;
                    }
                    if (!budget)
                    {
                        break;
                    }
                    --budget;
                    const auto current = *work.queue.begin();
                    work.queue.erase(work.queue.begin());
                    if (work.best.at({ std::get<1>(current), std::get<2>(current) }) != std::get<0>(current))
                    {
                        continue;
                    }
                    work.active = current;
                    work.edge = 0;
                }
                const auto [distance, at, seed] = *work.active;
                const auto& outgoing = work.onward[at];
                const auto edge = outgoing.upper_bound(work.edge);
                if (edge == outgoing.end())
                {
                    work.active.reset();
                    continue;
                }
                if (!budget)
                {
                    break;
                }
                --budget;
                const auto next = *edge;
                work.edge = next;
                const auto targetStation = stationOf(program, next), previousStation = stationOf(program, at);
                if (!_state.stations.contains(targetStation) || !_state.stations.contains(previousStation))
                {
                    continue;
                }
                const auto& a = _state.stations.at(previousStation);
                const auto& b = _state.stations.at(targetStation);
                const auto travel = distance + static_cast<uint32_t>(std::max(1, (std::abs(int(a.x) - b.x) + std::abs(int(a.y) - b.y)) / 32));
                const bool unloadAllowed = filterAllows(head, program, next, work.cargo, false);
                const bool loadAllowed = filterAllows(head, program, next, work.cargo, true);
                const bool nextSeed = seed && !(unloadAllowed && loadAllowed);
                const auto visit = std::pair{ next, nextSeed };
                if (work.best.contains(visit) && work.best.at(visit) <= travel)
                {
                    continue;
                }
                work.best[visit] = travel;
                if (unloadAllowed && targetStation != fromStation)
                {
                    work.context.rides[from][work.cargo].insert(next);
                    if (seed && filterAllows(head, program, from, work.cargo, true))
                    {
                        const Link link{ enumValue(head.owner), work.cargo, fromStation, targetStation, work.capacity[work.cargo], travel, 0 };
                        auto [it, inserted] = work.links.emplace(LinkKey{ link.company, link.cargo, link.from, link.to }, link);
                        if (!inserted)
                        {
                            it->second.travel = std::min(it->second.travel, travel);
                        }
                    }
                }
                bool forced = false;
                const auto index = Rules::indexOf(program, next);
                for (size_t row = index + 1; row < program.rows.size() && ((program.rows[row].raw & 7) == 4 || (program.rows[row].raw & 7) == 5); ++row)
                {
                    const auto* unload = reinterpret_cast<const Vehicles::Order*>(&program.rows[row].raw)->as<Vehicles::OrderUnloadAll>();
                    const bool conditional = std::any_of(program.blocks.begin(), program.blocks.end(), [&](const auto& block) { return block.enabled && (block.kind == Rules::Kind::ifBlock || block.kind == Rules::Kind::elseIf || block.kind == Rules::Kind::otherwise) && Rules::indexOf(program, block.first) <= row && row <= Rules::indexOf(program, block.last); });
                    forced |= unload && unload->getCargo() == work.cargo && !conditional;
                }
                if (!forced)
                {
                    work.queue.emplace(travel, next, nextSeed);
                }
            }
            return work.source == work.stops.size();
        }
        void publishService(uint16_t id, ServiceBuild& work)
        {
            // Repeated visits are alternatives, never extra physical capacity.
            if (_state.services.contains(id))
            {
                for (const auto& previous : _state.services.at(id))
                {
                    auto it = work.links.find({ previous.company, previous.cargo, previous.from, previous.to });
                    if (it != work.links.end())
                    {
                        it->second.travel = previous.travel;
                        it->second.usage = std::min(it->second.capacity, previous.usage);
                    }
                }
            }
            std::vector<Link> links;
            for (const auto& [key, link] : work.links)
            {
                links.push_back(link);
            }
            _services[id] = std::move(work.context);
            _signatures[id] = work.signature;
            _state.setService(id, links);
        }
    }
    uint64_t componentGeneration(uint16_t component)
    {
        const auto it = _componentEpochs.find(component);
        return it == _componentEpochs.end() ? 0 : it->second;
    }
    uint64_t stationGeneration(uint16_t station)
    {
        const auto it = _stationEpochs.find(station);
        return it == _stationEpochs.end() ? 0 : it->second;
    }
    uint64_t generation() { return _generation; }
    const State& state() { return _state; }
    State& gameState() { return _state; }
    void replaceState(State next)
    {
        _state = std::move(next);
        _booted = true;
        _signatures.clear();
        _services.clear();
        _serviceBuilds.clear();
        _reached.clear();
    }
    void reset()
    {
        ++_generation;
        _state = State{};
        _booted = false;
        _nativeDelivery = false;
        _deliveryCredit = 0;
        _pollStation = 0;
        _pollVehicle = EntityId::null;
        _componentEpochs.clear();
        _stationEpochs.clear();
        _deliveryOrigin.reset();
        _services.clear();
        _serviceBuilds.clear();
        _reached.clear();
        _signatures.clear();
        _state.lastVisits.clear();
        _state.incomeAdjustments.clear();
    }
    void syncStation(const OpenLoco::Station& station)
    {
        if (station.empty())
        {
            return;
        }
        CargoDist::Station node;
        node.id = enumValue(station.id());
        node.company = enumValue(station.owner);
        node.x = station.x;
        node.y = station.y;
        for (uint8_t cargo = 0; cargo < 32; ++cargo)
        {
            const auto& native = station.cargoStats[cargo];
            if (native.isAccepted())
            {
                node.accepts |= 1U << cargo;
            }
            if (native.origin == station.id())
            {
                node.supply[cargo] = native.quantity;
            }
        }
        _state.setStation(node);
        for (uint8_t cargo = 0; cargo < 32; ++cargo)
        {
            const auto& native = station.cargoStats[cargo];
            _state.reconcile({ false, node.id, cargo, 0 }, identity(station), native.quantity, enumValue(native.origin), native.enrouteAge);
            _state.assign({ false, node.id, cargo, 0 });
        }
    }
    void stationChanged(uint16_t station)
    {
        if (auto* native = StationManager::get(static_cast<StationId>(station)); native && !native->empty())
        {
            syncStation(*native);
        }
    }
    void stationRemoved(uint16_t station)
    {
        ++_stationEpochs[station];
        _state.removeStation(station);
    }
    void componentRemoved(uint16_t component)
    {
        ++_componentEpochs[component];
        _state.forgetComponent(component);
        _state.removeService(component);
        _services.erase(component);
        _serviceBuilds.erase(component);
        _reached.erase(component);
        _signatures.erase(component);
        _state.lastVisits.erase(component);
        _state.incomeAdjustments.erase(component);
    }
    void propertiesChanged(const Vehicles::VehicleHead& head)
    {
        if (_booted && !_nativeDelivery)
        {
            syncVehicle(head);
        }
    }
    void movedCargo(uint16_t oldComponent, uint16_t newComponent, uint8_t slot)
    {
        if (oldComponent == newComponent)
        {
            return;
        }
        const auto* native = EntityManager::get<Vehicles::VehicleBase>(static_cast<EntityId>(newComponent));
        if (!native)
        {
            return;
        }
        for (uint8_t cargo = 0; cargo < 32; ++cargo)
        {
            const Holder oldHolder{ true, oldComponent, cargo, slot }, nextHolder{ true, newComponent, cargo, slot };
            const auto it = _state.holdings.find(oldHolder);
            if (it == _state.holdings.end())
            {
                continue;
            }
            auto holding = std::move(it->second);
            _state.holdings.erase(it);
            if (const auto target = _state.holdings.find(nextHolder); target != _state.holdings.end())
            {
                _state.totalPackets -= target->second.packets.size();
            }
            holding.identity = slot == 0 ? identity(*native->asVehicleBody()) : identity(*native->asVehicleBogie());
            _state.holdings[nextHolder] = std::move(holding);
        }
    }
    void vehicleChanged(const Vehicles::VehicleHead& head)
    {
        bootstrap();
        const auto id = enumValue(head.id);
        if (!head.isPlaced() || head.status == Vehicles::Status::crashed)
        {
            _state.removeService(id);
            _services.erase(id);
            _serviceBuilds.erase(id);
            _reached.erase(id);
            _state.lastVisits.erase(id);
            _signatures.erase(id);
            return;
        }
        const auto program = Rules::snapshot(Rules::ownerForVehicle(id));
        uint64_t signature = uint64_t(program.revision) ^ (uint64_t(enumValue(head.owner)) << 40) ^ (uint64_t(enumValue(head.mode)) << 48) ^ (uint64_t(enumValue(head.vehicleType)) << 52);
        for (const auto& row : program.rows)
        {
            signature = (signature ^ row.raw) * 1099511628211ULL;
        }
        std::array<uint32_t, 32> capacity{};
        compartments(head, [&](auto& cargo, Holder, uint64_t) {
            for (uint8_t type = 0; type < 32; ++type)
            {
                if ((cargo.acceptedTypes & (1U << type)) || cargo.type == type)
                {
                    capacity[type] += cargo.maxQty;
                }
            }
        });
        for (auto amount : capacity)
        {
            signature = (signature ^ amount) * 1099511628211ULL;
        }
        if (const auto* line = Lines::lineForVehicle(id))
        {
            signature = (signature ^ line->id) * 1099511628211ULL;
            for (const auto& order : line->orders)
            {
                for (const auto& filter : order.filters)
                {
                    signature = (signature ^ (uint64_t(filter.cargoType) | uint64_t(filter.load) << 8 | uint64_t(filter.unload) << 9)) * 1099511628211ULL;
                }
            }
        }
        if (_serviceBuilds.contains(id) && _serviceBuilds.at(id).signature == signature)
        {
            return;
        }
        if (_signatures.contains(id) && _signatures.at(id) == signature)
        {
            _serviceBuilds.erase(id);
            return;
        }
        const auto possible = Rules::possibleService(program);
        ServiceBuild work;
        work.context.program = program;
        work.signature = signature;
        work.capacity = capacity;
        work.stops.assign(possible.stops.begin(), possible.stops.end());
        for (auto [from, to] : possible.links)
        {
            work.onward[from].insert(to);
        }
        size_t budget = 4096;
        if (advanceServiceBuild(head, work, budget))
        {
            publishService(id, work);
            _serviceBuilds.erase(id);
        }
        else
        {
            _serviceBuilds[id] = std::move(work);
            _state.dirty = true;
        }
        _state.limited |= possible.limited;
    }
    void stopReached(const Vehicles::VehicleHead& head)
    {
        const auto id = enumValue(head.id);
        vehicleChanged(head);
        const auto p = Rules::snapshot(Rules::ownerForVehicle(id));
        const auto row = rowAtOffset(p, head.currentOrder);
        if (row < p.rows.size() && stationOf(p, p.rows[row].key) == enumValue(head.stationId))
        {
            _reached[id] = { Rules::ownerForVehicle(id), p.rows[row].key, p.revision, _serviceBuilds.contains(id) ? _serviceBuilds.at(id).signature : _signatures.contains(id) ? _signatures.at(id)
                                                                                                                                                                               : 0,
                             enumValue(head.stationId) };
        }
    }
    Settings automaticSettings()
    {
        auto settings = _state.settings;
        for (uint8_t type = 0; type < 32; ++type)
        {
            const auto* object = ObjectManager::get<CargoObject>(type);
            settings.modes[type] = object && (object->cargoCategory == CargoCategory::passengers || object->cargoCategory == CargoCategory::mail) ? Mode::symmetric : Mode::asymmetric;
        }
        return settings;
    }
    TransferMode transferMode()
    {
        if (std::all_of(_state.settings.modes.begin(), _state.settings.modes.end(), [](auto mode) { return mode == Mode::manual; }))
        {
            return TransferMode::classic;
        }
        return _state.settings.modes == automaticSettings().modes ? TransferMode::automatic : TransferMode::custom;
    }
    void arrived(const Vehicles::VehicleHead& head)
    {
        bootstrap();
        stationChanged(enumValue(head.stationId));
        vehicleChanged(head);
        const auto id = enumValue(head.id), station = enumValue(head.stationId);
        const auto tick = ScenarioManager::getScenarioTicks();
        if (_state.lastVisits.contains(id))
        {
            const auto [previous, start] = _state.lastVisits.at(id);
            if (previous != station && _state.services.contains(id))
            {
                auto links = _state.services.at(id);
                std::array<uint32_t, 32> onboard{};
                compartments(head, [&](const auto& cargo, Holder, uint64_t) {
                    if (cargo.type < 32)
                    {
                        onboard[cargo.type] += cargo.qty;
                    }
                });
                for (auto& link : links)
                {
                    if (link.from == previous && link.to == station)
                    {
                        link.travel = std::max(1U, tick - start);
                        link.usage = std::min(link.capacity, onboard[link.cargo]);
                    }
                }
                _state.setService(id, links);
            }
        }
        _state.lastVisits[id] = { station, tick };
    }
    void tick()
    {
        bootstrap();
        auto* station = StationManager::get(static_cast<StationId>(_pollStation));
        if (station && !station->empty())
        {
            syncStation(*station);
        }
        else if (_state.stations.contains(_pollStation))
        {
            _state.removeStation(_pollStation);
        }
        _pollStation = static_cast<uint16_t>((_pollStation + 1) % 1024);
        if (_pollVehicle == EntityId::null)
        {
            _pollVehicle = EntityManager::firstId(EntityManager::EntityListType::vehicleHead);
        }
        if (auto* head = EntityManager::get<Vehicles::VehicleHead>(_pollVehicle); head && head->isVehicleHead())
        {
            syncVehicle(*head);
            vehicleChanged(*head);
            _pollVehicle = head->nextEntityId;
        }
        else
        {
            _pollVehicle = EntityId::null;
        }
        // Complete replacements share the solver's tick budget. Never publish partial rides.
        size_t serviceBudget = 4096;
        std::vector<uint16_t> pending;
        for (const auto& [id, work] : _serviceBuilds)
        {
            pending.push_back(id);
        }
        for (auto id : pending)
        {
            if (!serviceBudget)
            {
                break;
            }
            auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(id));
            if (!head || !head->isVehicleHead())
            {
                _serviceBuilds.erase(id);
                continue;
            }
            vehicleChanged(*head); // Cancel an obsolete owner/order/filter snapshot first.
            auto it = _serviceBuilds.find(id);
            if (it != _serviceBuilds.end() && advanceServiceBuild(*head, it->second, serviceBudget))
            {
                publishService(id, it->second);
                _serviceBuilds.erase(it);
            }
        }
        _state.calculate(serviceBudget);
    }
    bool submitSettings(const Settings& settings)
    {
        bootstrap();
        if (GameCommands::getUpdatingCompanyId() != CompanyManager::getControllingId())
        {
            return false;
        }
        return _state.configure(settings);
    }
    void ageVehicle(const Vehicles::VehicleHead& head)
    {
        syncVehicle(head);
        compartments(head, [](auto&, Holder holder, uint64_t) { if (holder.cargo < 32){ _state.age(holder);
} });
    }
    void ageStation(uint16_t id)
    {
        stationChanged(id);
        for (uint8_t cargo = 0; cargo < 32; ++cargo)
        {
            _state.age({ false, id, cargo, 0 });
        }
    }
    uint32_t availableFor(const Vehicles::VehicleHead& head, uint8_t cargo)
    {
        auto* station = StationManager::get(head.stationId);
        if (!station || cargo >= 32)
        {
            return 0;
        }
        bootstrap();
        vehicleChanged(head);
        if (!currentFilterAllows(head, cargo, true))
        {
            return 0;
        }
        if (_state.settings.modes[cargo] == Mode::manual)
        {
            return station->cargoStats[cargo].quantity;
        }
        stationChanged(enumValue(head.stationId));
        vehicleChanged(head);
        if (!currentFilterAllows(head, cargo, true))
        {
            return 0;
        }
        const Holder source{ false, enumValue(head.stationId), cargo, 0 };
        _state.assign(source);
        return _state.available(source, rideStops(head, cargo, true));
    }
    std::optional<uint32_t> load(Vehicles::VehicleHead& head, Vehicles::VehicleCargo& cargo, Vehicles::VehicleBogie&)
    {
        bootstrap();
        vehicleChanged(head);
        if (cargo.type < 32 && (!currentFilterAllows(head, cargo.type, true) || forceUnload(head, cargo.type)))
        {
            return 0;
        }
        if (cargo.type >= 32 || _state.settings.modes[cargo.type] == Mode::manual)
        {
            return std::nullopt;
        }
        const auto holder = holderFor(head, cargo);
        if (!holder)
        {
            return 0;
        }
        auto* station = StationManager::get(head.stationId);
        if (!station)
        {
            return 0;
        }
        stationChanged(enumValue(head.stationId));
        syncVehicle(head);
        vehicleChanged(head);
        if (!currentFilterAllows(head, cargo.type, true))
        {
            return 0;
        }
        const auto moved = _state.load({ false, enumValue(head.stationId), cargo.type, 0 }, *holder, cargo.maxQty - cargo.qty, rideStops(head, cargo.type, true));
        cargo.qty += static_cast<uint8_t>(moved);
        station->cargoStats[cargo.type].quantity -= static_cast<uint16_t>(moved);
        const auto& packets = _state.holdings.at(*holder).packets;
        if (!packets.empty())
        {
            cargo.townFrom = static_cast<StationId>(packets.front().origin);
            cargo.numDays = packets.front().age;
        }
        station->updateCargoDistribution();
        if (_state.services.contains(enumValue(head.id)))
        {
            uint32_t onboard = 0;
            compartments(head, [&](const auto& compartment, Holder, uint64_t) {
                if (compartment.type == cargo.type)
                {
                    onboard += compartment.qty;
                }
            });
            auto& links = _state.services.at(enumValue(head.id));
            for (auto& link : links)
            {
                if (link.from == enumValue(head.stationId) && link.cargo == cargo.type)
                {
                    link.usage = std::min(link.capacity, onboard);
                }
            }
        }
        return moved;
    }
    std::optional<bool> unload(Vehicles::VehicleHead& head, Vehicles::VehicleCargo& cargo, Vehicles::VehicleBogie&, const std::function<void(const Packet&)>& deliver)
    {
        if (_nativeDelivery || cargo.type >= 32)
        {
            return std::nullopt;
        }
        bootstrap();
        const auto holder = holderFor(head, cargo);
        if (!holder)
        {
            return std::nullopt;
        }
        syncVehicle(head);
        bool hasCredit = false;
        for (const auto& packet : _state.holdings.at(*holder).packets)
        {
            hasCredit |= packet.credit != 0;
        }
        vehicleChanged(head);
        if (!currentFilterAllows(head, cargo.type, false))
        {
            return false;
        }
        if (_state.settings.modes[cargo.type] == Mode::manual && !hasCredit)
        {
            return std::nullopt;
        }
        if (_serviceBuilds.contains(enumValue(head.id)))
        {
            return false;
        }
        auto* station = StationManager::get(head.stationId);
        if (!station)
        {
            return false;
        }
        stationChanged(enumValue(head.stationId));
        vehicleChanged(head);
        if (!currentFilterAllows(head, cargo.type, false))
        {
            return false;
        }
        std::set<uint16_t> future;
        const auto onward = rideStops(head, cargo.type, false);
        for (const auto& packet : _state.holdings.at(*holder).packets)
        {
            if (onward.contains(packet.destination) || std::any_of(onward.begin(), onward.end(), [&](auto next) {
                    return _state.usefulHop(enumValue(head.owner), cargo.type, enumValue(head.stationId), next, packet.destination);
                }))
            {
                future.insert(packet.destination);
            }
        }
        if (forceUnload(head, cargo.type))
        {
            future.clear();
        }
        auto result = _state.unload(*holder, enumValue(head.stationId), future, UINT16_MAX - station->cargoStats[cargo.type].quantity, [&](const Packet& packet) {
            if (!ObjectManager::get<CargoObject>(cargo.type))
            {
                return int64_t(0);
            }
            const auto distance = Math::Vector::distance2D(World::Pos2{ station->x, station->y }, World::Pos2{ packet.originX, packet.originY }) / 32;
            return int64_t(CompanyManager::calculateDeliveredCargoPayment(cargo.type, static_cast<uint16_t>(packet.quantity), static_cast<uint16_t>(distance), packet.age));
        });
        auto& waitingNative = station->cargoStats[cargo.type];
        const bool previouslyEmpty = waitingNative.quantity == 0;
        waitingNative.quantity += static_cast<uint16_t>(result.transferred);
        if (result.transferred)
        {
            const auto& waiting = _state.holdings.at({ false, enumValue(head.stationId), cargo.type, 0 }).packets;
            uint64_t days = 0;
            for (const auto& packet : waiting)
            {
                days += uint64_t(packet.quantity) * packet.age;
            }
            waitingNative.enrouteAge = static_cast<uint8_t>(days / waitingNative.quantity);
            if (previouslyEmpty && !waiting.empty())
            {
                waitingNative.origin = static_cast<StationId>(waiting.front().origin);
            }
        }
        for (const auto& packet : result.delivered)
        {
            struct DeliveryScope
            {
                DeliveryScope(const Packet& packet)
                {
                    _nativeDelivery = true;
                    _deliveryCredit = packet.credit;
                    _deliveryOrigin = packet.origin == kUnknownStation ? std::nullopt : std::optional<World::Pos2>{ { packet.originX, packet.originY } };
                }
                ~DeliveryScope()
                {
                    _nativeDelivery = false;
                    _deliveryCredit = 0;
                    _deliveryOrigin.reset();
                }
            } scope(packet);
            deliver(packet);
            _state.incomeAdjustments[enumValue(head.id)] -= packet.credit;
        }
        _state.incomeAdjustments[enumValue(head.id)] += result.virtualCredit;
        const auto before = cargo.qty;
        cargo.qty = static_cast<uint8_t>(_state.quantity(*holder));
        station->updateCargoDistribution();
        return cargo.qty != before;
    }
    World::Pos2 paymentOrigin(World::Pos2 fallback) { return _deliveryOrigin.value_or(fallback); }
    currency32_t reportedPayment(currency32_t actual)
    {
        return static_cast<currency32_t>(std::clamp<int64_t>(int64_t(actual) - _deliveryCredit, INT32_MIN, INT32_MAX));
    }
    currency32_t takeIncomeAdjustment(uint16_t head)
    {
        auto it = _state.incomeAdjustments.find(head);
        if (it == _state.incomeAdjustments.end())
        {
            return 0;
        }
        auto result = static_cast<currency32_t>(std::clamp<int64_t>(it->second, INT32_MIN, INT32_MAX));
        _state.incomeAdjustments.erase(it);
        return result;
    }
    uint16_t lastStation(uint16_t vehicle)
    {
        const auto it = _state.lastVisits.find(vehicle);
        return it == _state.lastVisits.end() ? kUnknownStation : it->second.first;
    }
    std::vector<Holder> vehicleHolders(uint16_t id)
    {
        std::vector<Holder> result;
        if (auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(id)); head && head->isVehicleHead())
        {
            compartments(*head, [&](auto&, Holder holder, uint64_t) { if (holder.cargo < 32){ result.push_back(holder);
} });
        }
        return result;
    }
    uint32_t vehicleDestined(uint16_t head, uint8_t cargo, uint16_t destination)
    {
        uint32_t result = 0;
        for (auto holder : vehicleHolders(head))
        {
            if (holder.cargo == cargo)
            {
                result += _state.destined(holder, destination);
            }
        }
        return result;
    }
    std::vector<Link> lineLinks(uint16_t line)
    {
        std::vector<Link> result;
        if (const auto* record = Lines::findLine(line))
        {
            for (auto vehicle : record->vehicles)
            {
                if (_state.services.contains(vehicle))
                {
                    result.insert(result.end(), _state.services.at(vehicle).begin(), _state.services.at(vehicle).end());
                }
            }
        }
        return result;
    }
    std::string status()
    {
        return _state.limited ? "Routing preview limit reached" : _state.job ? "Calculating cargo routes"
            : _state.routes.empty()                                          ? "Waiting for a connected service"
                                                                             : "Cargo routes ready";
    }
    std::map<Holder, std::pair<uint64_t, uint32_t>> nativeQuantities()
    {
        bootstrap();
        std::map<Holder, std::pair<uint64_t, uint32_t>> result;
        for (const auto& station : StationManager::stations())
        {
            for (uint8_t cargo = 0; cargo < 32; ++cargo)
            {
                result[{ false, enumValue(station.id()), cargo, 0 }] = { identity(station), station.cargoStats[cargo].quantity };
            }
        }
        for (auto* head : VehicleManager::VehicleList())
        {
            compartments(*head, [&](auto& cargo, Holder holder, uint64_t stamp) { if (holder.cargo < 32){ result[holder] = { stamp, cargo.qty };
} });
        }
        return result;
    }
}
