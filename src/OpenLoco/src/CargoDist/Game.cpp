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
        std::map<uint16_t, std::set<uint16_t>> _stops;
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
        bool filterAllows(const Vehicles::VehicleHead& head, uint16_t station, uint8_t cargo, bool loading)
        {
            if (const auto* line = Lines::lineForVehicle(enumValue(head.id)))
            {
                for (const auto& stop : line->orders)
                {
                    if (stop.type == Vehicles::OrderType::StopAt && stop.station == station)
                    {
                        for (const auto& filter : stop.filters)
                        {
                            if (filter.cargoType == cargo && !(loading ? filter.load : filter.unload))
                            {
                                return false;
                            }
                        }
                    }
                }
            }
            return true;
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
        _stops.clear();
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
        _stops.clear();
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
        _stops.erase(component);
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
            _stops.erase(id);
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
        if (_signatures.contains(id) && _signatures.at(id) == signature)
        {
            return;
        }
        _signatures[id] = signature;
        const auto possible = Rules::possibleService(program);
        _stops[id].clear();
        auto stationOf = [&](Rules::Key key) -> uint16_t {
            const auto index = Rules::indexOf(program, key);
            if (index >= program.rows.size())
            {
                return kUnknownStation;
            }
            const auto raw = program.rows[index].raw;
            const auto* order = reinterpret_cast<const Vehicles::Order*>(&raw)->as<Vehicles::OrderStation>();
            return order ? enumValue(order->getStation()) : kUnknownStation;
        };
        for (auto key : possible.stops)
        {
            _stops[id].insert(stationOf(key));
        }
        std::vector<Link> links;
        for (auto [fromKey, toKey] : possible.links)
        {
            const auto from = stationOf(fromKey), to = stationOf(toKey);
            if (!_state.stations.contains(from) || !_state.stations.contains(to) || from == to)
            {
                continue;
            }
            const auto& a = _state.stations.at(from);
            const auto& b = _state.stations.at(to);
            const auto travel = static_cast<uint32_t>(std::max(1, (std::abs(int(a.x) - b.x) + std::abs(int(a.y) - b.y)) / 32));
            for (uint8_t cargo = 0; cargo < 32; ++cargo)
            {
                if (capacity[cargo] && filterAllows(head, from, cargo, true) && filterAllows(head, to, cargo, false))
                {
                    Link link{ enumValue(head.owner), cargo, from, to, capacity[cargo], travel, 0 };
                    if (_state.services.contains(id))
                    {
                        for (const auto& previous : _state.services.at(id))
                        {
                            if (previous.company == link.company && previous.cargo == cargo && previous.from == from && previous.to == to)
                            {
                                link.travel = previous.travel;
                                link.usage = std::min(link.capacity, previous.usage);
                                break;
                            }
                        }
                    }
                    links.push_back(link);
                }
            }
        }
        std::sort(links.begin(), links.end());
        links.erase(std::unique(links.begin(), links.end()), links.end());
        _state.setService(id, links);
        _state.limited |= possible.limited;
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
        _state.calculate();
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
        if (!filterAllows(head, enumValue(head.stationId), cargo, true))
        {
            return 0;
        }
        if (_state.settings.modes[cargo] == Mode::manual)
        {
            return station->cargoStats[cargo].quantity;
        }
        stationChanged(enumValue(head.stationId));
        vehicleChanged(head);
        if (!filterAllows(head, enumValue(head.stationId), cargo, true))
        {
            return 0;
        }
        const Holder source{ false, enumValue(head.stationId), cargo, 0 };
        _state.assign(source);
        return _state.available(source, _stops[enumValue(head.id)]);
    }
    std::optional<uint32_t> load(Vehicles::VehicleHead& head, Vehicles::VehicleCargo& cargo, Vehicles::VehicleBogie&)
    {
        bootstrap();
        if (cargo.type < 32 && !filterAllows(head, enumValue(head.stationId), cargo.type, true))
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
        if (!filterAllows(head, enumValue(head.stationId), cargo.type, true))
        {
            return 0;
        }
        const auto moved = _state.load({ false, enumValue(head.stationId), cargo.type, 0 }, *holder, cargo.maxQty - cargo.qty, _stops[enumValue(head.id)]);
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
        if (!filterAllows(head, enumValue(head.stationId), cargo.type, false))
        {
            return false;
        }
        if (_state.settings.modes[cargo.type] == Mode::manual && !hasCredit)
        {
            return std::nullopt;
        }
        auto* station = StationManager::get(head.stationId);
        if (!station)
        {
            return false;
        }
        stationChanged(enumValue(head.stationId));
        vehicleChanged(head);
        if (!filterAllows(head, enumValue(head.stationId), cargo.type, false))
        {
            return false;
        }
        auto future = _stops[enumValue(head.id)];
        if (forceUnload(head, cargo.type))
        {
            future.clear();
        }
        auto result = _state.unload(*holder, enumValue(head.stationId), future, UINT16_MAX - station->cargoStats[cargo.type].quantity, [&](const Packet& packet) {
            const auto distance = Math::Vector::distance2D(World::Pos2{ station->x, station->y }, World::Pos2{ packet.originX, packet.originY }) / 32;
            return int64_t(CompanyManager::calculateDeliveredCargoPayment(cargo.type, static_cast<uint16_t>(packet.quantity), static_cast<uint16_t>(distance), packet.age));
        });
        station->cargoStats[cargo.type].quantity += static_cast<uint16_t>(result.transferred);
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
