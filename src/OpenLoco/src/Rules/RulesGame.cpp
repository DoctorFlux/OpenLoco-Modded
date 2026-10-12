#include "Date.h"
#include "Entities/EntityManager.h"
#include "GameCommands/GameCommands.h"
#include "GameCommands/Vehicles/VehicleOrderDelete.h"
#include "Lines/Line.h"
#include "Objects/CargoObject.h"
#include "Objects/ObjectManager.h"
#include "Rules/Platforms.h"
#include "Rules/Rules.h"
#include "S5/Limits.h"
#include "Scenario/ScenarioManager.h"
#include "Ui/WindowManager.h"
#include "Vehicles/OrderManager.h"
#include "Vehicles/VehicleBody.h"
#include "Vehicles/VehicleBogie.h"
#include "Vehicles/VehicleHead.h"
#include "World/CompanyManager.h"
#include "World/Station.h"
#include "World/StationManager.h"
#include <OpenLoco/Diagnostics/Logging.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace OpenLoco::Rules
{
    namespace
    {
        std::map<Owner, Program> programs;
        struct VehicleRun
        {
            Runtime runtime;
            bool ready = false;
            bool hasOrder = false;
            Owner owner{};
            std::map<Key, bool> cargoDecisions;
            Key reachedOrder = 0;
        };
        std::map<uint16_t, VehicleRun> vehicles;
        bool installing = false;
        constexpr size_t sizes[] = { 1, 2, 2, 6, 1, 1 };
        uint64_t rawOrder(const Lines::LineOrder& o)
        {
            using namespace Vehicles;
            switch (o.type)
            {
                case OrderType::StopAt: return OrderStopAt(static_cast<StationId>(o.station)).getRaw();
                case OrderType::RouteThrough: return OrderRouteThrough(static_cast<StationId>(o.station)).getRaw();
                case OrderType::RouteWaypoint: return OrderRouteWaypoint(World::TilePos2{ o.tileX, o.tileY }, o.baseZ, o.direction, o.trackId).getRaw();
                case OrderType::UnloadAll: return OrderUnloadAll(o.cargo).getRaw();
                case OrderType::WaitFor: return OrderWaitFor(o.cargo).getRaw();
                default: return 0;
            }
        }
        size_t rowAtOffset(const Program& p, uint16_t offset)
        {
            size_t i = 0, position = 0;
            while (i < p.rows.size() && position < offset)
            {
                position += sizes[p.rows[i++].raw & 7];
            }
            return i;
        }
        uint16_t offsetOf(const Program& p, Key key)
        {
            uint16_t offset = 0;
            for (const auto& row : p.rows)
            {
                if (row.key == key)
                {
                    return offset;
                }
                offset += static_cast<uint16_t>(sizes[row.raw & 7]);
            }
            return 0;
        }
        bool owns(Owner owner)
        {
            if (owner.kind == OwnerKind::vehicle)
            {
                const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(owner.id));
                return head != nullptr && head->owner == CompanyManager::getControllingId() && Lines::lineForVehicle(owner.id) == nullptr;
            }
            const auto* line = Lines::findLine(owner.id);
            if (line == nullptr)
            {
                return false;
            }
            for (auto id : line->vehicles)
            {
                if (const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(id)); head != nullptr && head->owner != CompanyManager::getControllingId())
                {
                    return false;
                }
            }
            return true;
        }
        Result installRows(Owner owner, const Program& old, const Program& p)
        {
            if (owner.kind == OwnerKind::line)
            {
                const auto* line = Lines::findLine(owner.id);
                if (line == nullptr)
                {
                    return { false, "Line no longer exists" };
                }
                std::vector<Lines::LineOrder> orders;
                for (const auto& row : p.rows)
                {
                    auto original = indexOf(old, row.key);
                    if (original < line->orders.size())
                    {
                        orders.push_back(line->orders[original]);
                    }
                    else
                    {
                        orders.push_back(Lines::fromVehicleOrder(*reinterpret_cast<const Vehicles::Order*>(&row.raw)));
                    }
                }
                const auto result = Lines::replaceOrders(owner.id, std::move(orders));
                return { result.ok, result.message };
            }
            auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(owner.id));
            if (head == nullptr)
            {
                return { false, "Vehicle no longer exists" };
            }
            size_t required = 1;
            for (const auto& row : p.rows)
            {
                required += sizes[row.raw & 7];
            }
            if (p.rows.size() > S5::Limits::kMaxOrdersPerVehicle || Vehicles::OrderManager::orderTableLength() - head->sizeOfOrderTable + required > S5::Limits::kMaxOrders)
            {
                return { false, "There is no room in the order table" };
            }
            if (p.rows.size() < old.rows.size())
            {
                // Use the existing deletion command, from the bottom upwards so offsets remain valid.
                for (size_t i = old.rows.size(); i > 0; --i)
                {
                    if (indexOf(p, old.rows[i - 1].key) >= p.rows.size())
                    {
                        GameCommands::VehicleOrderDeleteArgs args;
                        args.head = head->id;
                        args.orderOffset = offsetOf(old, old.rows[i - 1].key);
                        if (GameCommands::doCommand(args, GameCommands::Flags::apply) == GameCommands::kFailure)
                        {
                            return { false, "Unable to delete this order" };
                        }
                    }
                }
                return { true, {} };
            }
            Vehicles::OrderManager::freeOrders(head);
            Vehicles::OrderManager::allocateOrders(*head);
            uint16_t offset = 0;
            for (const auto& row : p.rows)
            {
                Vehicles::OrderManager::insertOrder(head, offset, reinterpret_cast<const Vehicles::Order*>(&row.raw));
                offset += static_cast<uint16_t>(sizes[row.raw & 7]);
            }
            return { true, {} };
        }
        bool compare(int32_t value, const Condition& c)
        {
            return c.compare == Compare::less ? value < c.value : c.compare == Compare::greater ? value > c.value
                                                                                                : value == c.value;
        }
        CheckResult evaluate(const Program& p, const Vehicles::VehicleHead& head, const Condition& c)
        {
            using namespace Vehicles;
            if (c.check == Check::year)
            {
                return { true, compare(getCurrentYear(), c), {} };
            }
            if (c.check == Check::full || c.check == Check::empty || c.check == Check::hasSpace)
            {
                Vehicle train(head);
                uint32_t quantity = 0, capacity = 0;
                bool allFull = true;
                bool anySpace = false;
                for (auto& car : train.cars)
                {
                    for (auto& part : car)
                    {
                        for (const auto* cargo : { &part.front->secondaryCargo, &part.body->primaryCargo })
                        {
                            if (cargo->maxQty != 0 && (cargo->type != 0xFF || cargo->acceptedTypes != 0))
                            {
                                capacity += cargo->maxQty;
                                quantity += cargo->qty;
                                allFull &= cargo->qty == cargo->maxQty;
                                anySpace |= cargo->qty < cargo->maxQty;
                            }
                        }
                    }
                }
                return { true, c.check == Check::full ? capacity != 0 && allFull : c.check == Check::empty ? quantity == 0
                                                                                                           : anySpace,
                         {} };
            }
            const auto index = indexOf(p, c.stop);
            if (index >= p.rows.size())
            {
                return { false, false, "Stop no longer exists" };
            }
            const auto& raw = p.rows[index].raw;
            const auto* order = reinterpret_cast<const Order*>(&raw)->as<OrderStation>();
            if (order == nullptr)
            {
                return { false, false, "Choose a station" };
            }
            const auto* station = StationManager::get(order->getStation());
            if (station == nullptr || station->empty())
            {
                return { false, false, "Station no longer exists" };
            }
            if (c.check == Check::cargoWaiting)
            {
                if (ObjectManager::get<CargoObject>(c.cargo) == nullptr)
                {
                    return { false, false, "This cargo type is unavailable" };
                }
                return { true, compare(station->cargoStats[c.cargo].quantity, c), {} };
            }
            if (c.check == Check::platformFree)
            {
                const auto platforms = railPlatforms(enumValue(order->getStation()));
                auto chosen = std::find_if(platforms.begin(), platforms.end(), [&](const Platform& platform) { return platform.anchor == c.platformAnchor; });
                if (chosen == platforms.end())
                {
                    return { false, false, "Platform no longer exists" };
                }
                return { true, platformIsFree(*chosen, enumValue(head.id)), {} };
            }
            const auto* line = Lines::lineForVehicle(enumValue(head.id));
            if (line == nullptr || index >= line->orders.size() || line->orders[index].slots.empty())
            {
                return { false, false, "This stop has no timetable slot" };
            }
            const auto now = Lines::timetableMinute(ScenarioManager::getScenarioTicks());
            const auto decision = Lines::decideDeparture(line->orders[index], now, 0xFFFF, 0, false, 0, 1);
            return { true, c.check == Check::late ? decision.late : !decision.late, {} };
        }
        bool select(Vehicles::VehicleHead& head, const Program& p, VehicleRun& run, bool includeCargo)
        {
            const auto tick = ScenarioManager::getScenarioTicks();
            for (;;)
            {
                const auto d = next(p, run.runtime, tick, [&](const Condition& c) { return evaluate(p, head, c); });
                if (d.kind != RunKind::order)
                {
                    run.ready = false;
                    return false;
                }
                const auto index = indexOf(p, d.order);
                if (index >= p.rows.size())
                {
                    return false;
                }
                if (!includeCargo && !isRoutable(p.rows[index].raw))
                {
                    continue;
                }
                const auto nextOffset = offsetOf(p, d.order);
                if (head.currentOrder != nextOffset)
                {
                    Signals::dropPendingPath(enumValue(head.id));
                    Signals::clearWaitingSignals(enumValue(head.id));
                }
                head.currentOrder = nextOffset;
                run.hasOrder = true;
                run.ready = true;
                Ui::WindowManager::invalidateOrderPageByVehicleNumber(enumValue(head.id));
                return true;
            }
        }
        std::string identity(Owner owner)
        {
            if (owner.kind == OwnerKind::line)
            {
                const auto* line = Lines::findLine(owner.id);
                return line == nullptr ? std::string{} : line->name;
            }
            const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(owner.id));
            if (head == nullptr)
            {
                return {};
            }
            Vehicles::Vehicle train(*head);
            uint32_t created = train.cars.empty() ? 0 : train.cars.firstCar.body->creationDay;
            return std::to_string(enumValue(head->owner)) + ":" + std::to_string(head->ordinalNumber) + ":" + std::to_string(created);
        }
    }
    Owner ownerForVehicle(uint16_t id)
    {
        const auto* line = Lines::lineForVehicle(id);
        return line == nullptr ? Owner{ OwnerKind::vehicle, id } : Owner{ OwnerKind::line, line->id };
    }
    const Program* find(Owner owner)
    {
        auto it = programs.find(owner);
        return it == programs.end() ? nullptr : &it->second;
    }
    Program snapshot(Owner owner)
    {
        if (const auto* p = find(owner))
        {
            return *p;
        }
        std::vector<uint64_t> rows;
        bool rail = false;
        if (owner.kind == OwnerKind::line)
        {
            if (const auto* line = Lines::findLine(owner.id))
            {
                for (const auto& order : line->orders)
                {
                    rows.push_back(rawOrder(order));
                }
                rail = line->mode == TransportMode::rail;
            }
        }
        else if (const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(owner.id)); head != nullptr && head->sizeOfOrderTable != 0)
        {
            for (const auto& order : Vehicles::OrderRingView(head->orderTableOffset))
            {
                rows.push_back(order.getRaw());
            }
            rail = head->mode == TransportMode::rail;
        }
        return makeProgram(rows, rail);
    }
    Result submit(Owner owner, const Edit& command)
    {
        if (!owns(owner))
        {
            return { false, "This list belongs to another company or no longer exists" };
        }
        if (!programs.contains(owner) && programs.size() >= 2048)
        {
            return { false, "The game already has rules on 2048 lists" };
        }
        const auto old = snapshot(owner);
        auto updated = old;
        const auto result = edit(updated, command);
        if (!result)
        {
            return result;
        }
        if (command.kind == EditKind::insertRow)
        {
            const auto* order = reinterpret_cast<const Vehicles::Order*>(&command.raw);
            const auto mode = owner.kind == OwnerKind::line ? Lines::findLine(owner.id)->mode : EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(owner.id))->mode;
            if ((mode == TransportMode::air && (order->is<Vehicles::OrderRouteThrough>() || order->is<Vehicles::OrderRouteWaypoint>())) || (mode == TransportMode::water && order->is<Vehicles::OrderRouteThrough>()))
            {
                return { false, "This order is not available for this vehicle type" };
            }
            if (const auto* stop = order->as<Vehicles::OrderStation>())
            {
                const auto* station = StationManager::get(stop->getStation());
                if (!station || station->empty() || station->owner != CompanyManager::getControllingId())
                {
                    return { false, "Choose a station belonging to this company" };
                }
            }
        }
        const bool changesRows = command.kind == EditKind::insertRow || command.kind == EditKind::deleteRow || command.kind == EditKind::deleteRows || command.kind == EditKind::moveRows || command.kind == EditKind::reverseRows;
        std::map<uint16_t, Key> cursors;
        for (uint16_t i = 0; i < S5::Limits::kMaxEntities; ++i)
        {
            const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(i));
            if (head == nullptr || ownerForVehicle(i) != owner)
            {
                continue;
            }
            const auto index = rowAtOffset(old, head->currentOrder);
            if (index < old.rows.size())
            {
                cursors[i] = old.rows[index].key;
            }
        }
        if (changesRows)
        {
            installing = true;
            const auto installed = installRows(owner, old, updated);
            installing = false;
            if (!installed)
            {
                return installed;
            }
        }
        programs[owner] = std::move(updated);
        const auto& p = programs[owner];
        for (auto [id, key] : cursors)
        {
            auto& run = vehicles[id];
            run.owner = owner;
            run.cargoDecisions.clear();
            if (indexOf(p, run.reachedOrder) >= p.rows.size())
            {
                run.reachedOrder = 0;
            }
            if (indexOf(p, key) < p.rows.size())
            {
                auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(id));
                if (changesRows)
                {
                    head->currentOrder = offsetOf(p, key);
                }
                const auto pendingKey = !run.ready && indexOf(p, run.runtime.current) < p.rows.size() ? run.runtime.current : key;
                resumeAfter(p, run.runtime, pendingKey);
                run.ready = pendingKey == key;
                run.hasOrder = true;
                if (!head->isPlaced())
                {
                    run = {};
                    run.owner = owner;
                    run.runtime.revision = p.revision;
                }
            }
            else
            {
                run = {};
                run.owner = owner;
                run.runtime.revision = p.revision;
            }
        }
        Ui::WindowManager::invalidate(Ui::WindowType::lineManager);
        Ui::WindowManager::invalidate(Ui::WindowType::vehicle);
        return { true, {} };
    }
    void forget(Owner owner)
    {
        programs.erase(owner);
        std::erase_if(vehicles, [&](const auto& entry) { return entry.second.owner == owner || (owner.kind == OwnerKind::vehicle && entry.first == owner.id); });
    }
    void clear()
    {
        programs.clear();
        vehicles.clear();
    }
    void assigned(uint16_t vehicle)
    {
        forget({ OwnerKind::vehicle, vehicle });
        vehicles.erase(vehicle);
    }
    void detached(uint16_t vehicle, uint16_t line)
    {
        vehicles.erase(vehicle);
        if (const auto* p = find({ OwnerKind::line, line }))
        {
            programs[{ OwnerKind::vehicle, vehicle }] = *p;
        }
    }
    void cloned(uint16_t source, uint16_t destination)
    {
        vehicles.erase(destination);
        if (const auto* p = find(ownerForVehicle(source)))
        {
            programs[{ OwnerKind::vehicle, destination }] = *p;
        }
    }
    Result noteOrderEdit(Owner owner, const Edit& command)
    {
        if (installing)
        {
            return { true, {} };
        }
        auto it = programs.find(owner);
        if (it == programs.end())
        {
            return { true, {} };
        }
        std::map<uint16_t, Key> cursors;
        if (owner.kind == OwnerKind::line)
        {
            if (const auto* line = Lines::findLine(owner.id))
            {
                for (auto id : line->vehicles)
                {
                    if (const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(id)))
                    {
                        const auto index = rowAtOffset(it->second, head->currentOrder);
                        if (index < it->second.rows.size())
                        {
                            cursors[id] = it->second.rows[index].key;
                        }
                    }
                }
            }
        }
        const auto result = edit(it->second, command);
        if (!result)
        {
            return result;
        }
        for (auto [id, key] : cursors)
        {
            if (auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(id)))
            {
                head->currentOrder = offsetOf(it->second, key);
            }
        }
        for (auto& [id, run] : vehicles)
        {
            if (run.owner == owner)
            {
                run.cargoDecisions.clear();
                if (indexOf(it->second, run.reachedOrder) >= it->second.rows.size())
                {
                    run.reachedOrder = 0;
                }
                if (indexOf(it->second, run.runtime.current) < it->second.rows.size())
                {
                    resumeAfter(it->second, run.runtime, run.runtime.current);
                }
                else
                {
                    run.ready = false;
                    run.hasOrder = false;
                    run.runtime = {};
                    run.runtime.revision = it->second.revision;
                }
            }
        }
        return { true, {} };
    }
    void noteRawOrderChange(Owner owner, size_t index, uint64_t raw)
    {
        if (auto it = programs.find(owner); it != programs.end() && index < it->second.rows.size())
        {
            it->second.rows[index].raw = raw;
            ++it->second.revision;
            for (auto& [id, run] : vehicles)
            {
                if (run.owner == owner)
                {
                    resumeAfter(it->second, run.runtime, run.runtime.current);
                }
            }
        }
    }
    Result movedPrivateOrder(uint16_t vehicle, uint16_t offset, bool up)
    {
        if (installing)
        {
            return { true, {} };
        }
        const auto owner = Owner{ OwnerKind::vehicle, vehicle };
        const auto* p = find(owner);
        if (p == nullptr)
        {
            return { true, {} };
        }
        const auto index = rowAtOffset(*p, offset);
        if (index >= p->rows.size() || (up && index == 0) || (!up && index + 1 >= p->rows.size()))
        {
            return { true, {} };
        }
        Edit e;
        e.kind = EditKind::moveRows;
        e.first = e.last = index;
        e.destination = up ? index - 1 : index + 2;
        return noteOrderEdit(owner, e);
    }
    Result reversePrivateOrders(uint16_t vehicle)
    {
        Edit e;
        e.kind = EditKind::reverseRows;
        return noteOrderEdit({ OwnerKind::vehicle, vehicle }, e);
    }
    bool prepareVehicle(uint16_t vehicle)
    {
        const auto owner = ownerForVehicle(vehicle);
        const auto* p = find(owner);
        if (p == nullptr || p->blocks.empty() || p->rows.empty())
        {
            return true;
        }
        auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicle));
        if (head == nullptr || !head->isPlaced() || head->var_52 == 1)
        {
            return true;
        }
        auto& run = vehicles[vehicle];
        if (run.owner != owner)
        {
            run = {};
            run.owner = owner;
        }
        if (run.ready)
        {
            return true;
        }
        if (head->status == Vehicles::Status::loading || head->status == Vehicles::Status::unloading)
        {
            return true;
        }
        if (!run.hasOrder)
        {
            const auto index = rowAtOffset(*p, head->currentOrder);
            startAt(*p, run.runtime, index < p->rows.size() ? p->rows[index].key : p->rows.front().key);
            run.hasOrder = true;
        }
        return select(*head, *p, run, false);
    }
    void reached(uint16_t vehicle)
    {
        const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicle));
        if (head == nullptr || !head->isPlaced() || head->var_52 == 1)
        {
            return;
        }
        if (auto it = vehicles.find(vehicle); it != vehicles.end())
        {
            it->second.reachedOrder = it->second.runtime.current;
            it->second.ready = false;
            it->second.cargoDecisions.clear();
            it->second.runtime.prechecked.clear();
            it->second.runtime.skipped.clear();
        }
    }
    uint16_t timetableOrder(uint16_t vehicle, uint16_t vanillaOffset)
    {
        const auto* p = find(ownerForVehicle(vehicle));
        auto it = vehicles.find(vehicle);
        if (!p || it == vehicles.end())
        {
            return vanillaOffset;
        }
        const auto index = indexOf(*p, it->second.reachedOrder);
        if (index >= p->rows.size() || (p->rows[index].raw & 7) != 1)
        {
            return vanillaOffset;
        }
        return offsetOf(*p, it->second.reachedOrder);
    }
    bool canStopAt(uint16_t vehicle, uint16_t station)
    {
        const auto* p = find(ownerForVehicle(vehicle));
        const auto it = vehicles.find(vehicle);
        if (!p || p->blocks.empty() || it == vehicles.end())
        {
            return true;
        }
        const auto current = indexOf(*p, it->second.runtime.current);
        if (current < p->rows.size())
        {
            const auto* order = reinterpret_cast<const Vehicles::Order*>(&p->rows[current].raw)->as<Vehicles::OrderStation>();
            if (order && enumValue(order->getStation()) == station)
            {
                return true;
            }
        }
        for (const auto key : it->second.runtime.skipped)
        {
            const auto index = indexOf(*p, key);
            if (index >= p->rows.size() || (p->rows[index].raw & 7) != 1)
            {
                continue;
            }
            const auto* order = reinterpret_cast<const Vehicles::Order*>(&p->rows[index].raw)->as<Vehicles::OrderStation>();
            if (enumValue(order->getStation()) == station)
            {
                return false;
            }
        }
        return true;
    }
    bool cargoOrderEnabled(uint16_t vehicle, uint16_t orderOffset)
    {
        const auto* p = find(ownerForVehicle(vehicle));
        if (!p || p->blocks.empty())
        {
            return true;
        }
        auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicle));
        auto it = vehicles.find(vehicle);
        if (!head || it == vehicles.end())
        {
            return true;
        }
        const auto index = rowAtOffset(*p, orderOffset);
        if (index >= p->rows.size())
        {
            return true;
        }
        auto& run = it->second;
        const auto key = p->rows[index].key;
        if (run.cargoDecisions.contains(key))
        {
            return run.cargoDecisions[key];
        }
        const auto current = indexOf(*p, run.runtime.current);
        bool allowed = true;
        for (const auto& b : p->blocks)
        {
            if ((b.kind != Kind::ifBlock && b.kind != Kind::otherwise) || index < indexOf(*p, b.first) || index > indexOf(*p, b.last))
            {
                continue;
            }
            bool passed = false;
            if (run.runtime.prechecked.contains(b.id))
            {
                passed = run.runtime.prechecked[b.id];
            }
            else if (indexOf(*p, b.first) <= current && current <= indexOf(*p, b.last) && run.runtime.branches.contains(b.id))
            {
                passed = run.runtime.branches[b.id];
            }
            else
            {
                if (b.kind == Kind::otherwise)
                {
                    passed = !run.runtime.branches.contains(b.parent) || !run.runtime.branches[b.parent];
                }
                else
                {
                    auto result = b.enabled ? evaluate(*p, *head, b.condition) : CheckResult{ false, false, "Check unavailable" };
                    if (b.join != Join::none)
                    {
                        const auto extra = evaluate(*p, *head, b.extra);
                        result.passed = result.available && extra.available && (b.join == Join::andCheck ? result.passed && extra.passed : result.passed || extra.passed);
                        result.available = result.available && extra.available;
                    }
                    passed = result.available && result.passed;
                    if (!result.available)
                    {
                        run.runtime.status = result.reason.empty() ? "Check unavailable" : result.reason;
                    }
                }
                for (const auto& child : p->blocks)
                {
                    if (child.parent == b.id && child.kind == Kind::goTo && !child.enabled)
                    {
                        passed = false;
                        run.runtime.status = "Go to destination is missing";
                    }
                }
                run.runtime.prechecked[b.id] = passed;
                run.runtime.branches[b.id] = passed;
            }
            bool hasActions = false;
            for (const auto& child : p->blocks)
            {
                if (child.parent == b.id && (child.kind == Kind::skip || child.kind == Kind::goTo))
                {
                    hasActions = true;
                    if (passed && child.enabled)
                    {
                        allowed = false;
                    }
                }
            }
            if (!hasActions && !passed)
            {
                allowed = false;
            }
        }
        run.cargoDecisions[key] = allowed;
        return allowed;
    }
    void manuallySkipped(uint16_t vehicle)
    {
        const auto owner = ownerForVehicle(vehicle);
        const auto* p = find(owner);
        auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicle));
        if (!p || !head)
        {
            return;
        }
        auto& run = vehicles[vehicle];
        run = {};
        run.owner = owner;
        const auto index = rowAtOffset(*p, head->currentOrder);
        if (index < p->rows.size())
        {
            startAt(*p, run.runtime, p->rows[index].key);
            run.hasOrder = true;
        }
    }
    bool holdAtStation(uint16_t vehicle)
    {
        const auto* p = find(ownerForVehicle(vehicle));
        if (p == nullptr || p->blocks.empty())
        {
            return false;
        }
        auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicle));
        auto it = vehicles.find(vehicle);
        if (head == nullptr || it == vehicles.end())
        {
            return false;
        }
        auto& run = it->second;
        if (run.ready)
        {
            return false;
        }
        return !select(*head, *p, run, false);
    }
    const Runtime* runtimeForVehicle(uint16_t vehicle)
    {
        const auto it = vehicles.find(vehicle);
        if (it == vehicles.end() || it->second.owner != ownerForVehicle(vehicle))
        {
            return nullptr;
        }
        const auto* program = find(it->second.owner);
        return program && it->second.runtime.revision == program->revision ? &it->second.runtime : nullptr;
    }
    std::string vehicleStatus(uint16_t vehicle)
    {
        const auto it = vehicles.find(vehicle);
        if (it == vehicles.end())
        {
            return "Rules run when the next order is chosen";
        }
        const auto& r = it->second.runtime;
        if (!r.status.empty())
        {
            return r.status;
        }
        if (!it->second.hasOrder || r.current == 0)
        {
            return "Rules start when the vehicle departs";
        }
        const auto* p = find(it->second.owner);
        if (!p)
        {
            return "Following the orders";
        }
        size_t number = 0;
        for (const auto& row : p->rows)
        {
            if (isRoutable(row.raw))
            {
                ++number;
            }
            if (row.key == r.current)
            {
                break;
            }
        }
        std::string text = "Next stop / waypoint " + std::to_string(number);
        if (!r.repeats.empty())
        {
            text = "Repeat: " + std::to_string(r.repeats.rbegin()->second) + " passes left. " + text;
        }
        return text;
    }

    std::filesystem::path companionPath(const std::filesystem::path& path)
    {
        auto companion = path;
        companion += ".orules";
        return companion;
    }
    // Text companion, bounded records, explicit version. Never changes the SV5 order byte.
    static void writeCondition(std::ostream& out, const Condition& c)
    {
        out << int(c.check) << ' ' << int(c.compare) << ' ' << c.stop << ' ' << int(c.cargo) << ' ' << c.platform << ' ' << c.platformAnchor << ' ' << c.value << ' ';
    }
    static bool readCondition(std::istream& in, Condition& c)
    {
        unsigned check, comparison, cargo;
        if (!(in >> check >> comparison >> c.stop >> cargo >> c.platform >> c.platformAnchor >> c.value) || check > unsigned(Check::year) || comparison > 2 || cargo >= 32)
        {
            return false;
        }
        c.check = static_cast<Check>(check);
        c.compare = static_cast<Compare>(comparison);
        c.cargo = static_cast<uint8_t>(cargo);
        return true;
    }
    bool saveCompanion(const std::filesystem::path& savePath)
    {
        const auto path = companionPath(savePath);
        auto temporary = path;
        temporary += ".tmp";
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.imbue(std::locale::classic());
        if (!out)
        {
            return false;
        }
        out << "ORULES 1\n"
            << programs.size() << '\n';
        for (const auto& [owner, p] : programs)
        {
            out << int(owner.kind) << ' ' << owner.id << ' ' << std::quoted(identity(owner)) << '\n';
            out << p.nextKey << ' ' << p.revision << ' ' << p.rail << ' ' << p.rows.size() << ' ' << p.blocks.size() << '\n';
            for (const auto& row : p.rows)
            {
                out << row.key << ' ' << row.raw << '\n';
            }
            for (const auto& b : p.blocks)
            {
                out << b.id << ' ' << int(b.kind) << ' ' << b.first << ' ' << b.last << ' ' << b.parent << ' ' << b.target << ' ' << int(b.join) << ' ' << b.number << ' ' << b.enabled << ' ' << std::quoted(b.name) << ' ';
                writeCondition(out, b.condition);
                writeCondition(out, b.extra);
                out << '\n';
            }
        }
        out << vehicles.size() << '\n';
        for (const auto& [id, run] : vehicles)
        {
            const auto& r = run.runtime;
            out << id << ' ' << int(run.owner.kind) << ' ' << run.owner.id << ' ' << std::quoted(identity({ OwnerKind::vehicle, id })) << ' ' << run.ready << ' ' << run.hasOrder << ' ' << run.reachedOrder << ' '
                << r.revision << ' ' << r.pc << ' ' << r.current << ' ' << r.waiting << ' ' << r.waitStart << ' ' << r.budgetTick << ' ' << r.steps << ' ' << std::quoted(r.status) << '\n';
            out << r.repeats.size();
            for (auto [key, count] : r.repeats)
            {
                out << ' ' << key << ' ' << count;
            }
            out << '\n';
            out << r.branches.size();
            for (auto [key, passed] : r.branches)
            {
                out << ' ' << key << ' ' << passed;
            }
            out << '\n';
            out << r.prechecked.size();
            for (auto [key, passed] : r.prechecked)
            {
                out << ' ' << key << ' ' << passed;
            }
            out << '\n';
            out << run.cargoDecisions.size();
            for (auto [key, allowed] : run.cargoDecisions)
            {
                out << ' ' << key << ' ' << allowed;
            }
            out << '\n';
            out << r.skipped.size();
            for (auto key : r.skipped)
            {
                out << ' ' << key;
            }
            out << '\n';
        }
        out.flush();
        const bool success = static_cast<bool>(out);
        out.close();
        if (!success || out.fail())
        {
            std::filesystem::remove(temporary);
            return false;
        }
        // Windows rename cannot replace an existing destination; use the platform's replacement API.
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            return false;
        }
#else
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error)
        {
            return false;
        }
#endif
        return true;
    }
    bool loadCompanion(const std::filesystem::path& savePath)
    {
        clear();
        const auto path = companionPath(savePath);
        std::error_code error;
        if (!std::filesystem::exists(path, error))
        {
            return !error;
        }
        if (std::filesystem::file_size(path, error) > 256 * 1024 * 1024 || error)
        {
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        in.imbue(std::locale::classic());
        std::string magic;
        unsigned version;
        size_t count;
        if (!(in >> magic >> version >> count) || magic != "ORULES" || version != 1 || count > 2048)
        {
            return false;
        }
        std::map<Owner, Program> loaded;
        for (size_t i = 0; i < count; ++i)
        {
            Owner owner;
            unsigned kind;
            std::string stamp;
            Program p;
            size_t rows, blocks;
            if (!(in >> kind >> owner.id >> std::quoted(stamp)) || kind > 1 || stamp.size() > 256)
            {
                return false;
            }
            owner.kind = static_cast<OwnerKind>(kind);
            if (!(in >> p.nextKey >> p.revision >> p.rail >> rows >> blocks) || rows > 63 || blocks > 256)
            {
                return false;
            }
            for (size_t j = 0; j < rows; ++j)
            {
                Row row;
                if (!(in >> row.key >> row.raw))
                {
                    return false;
                }
                p.rows.push_back(row);
            }
            for (size_t j = 0; j < blocks; ++j)
            {
                Block b;
                unsigned blockKind, join;
                if (!(in >> b.id >> blockKind >> b.first >> b.last >> b.parent >> b.target >> join >> b.number >> b.enabled >> std::quoted(b.name)) || blockKind > unsigned(Kind::goTo) || join > 2 || b.name.size() > 80)
                {
                    return false;
                }
                b.kind = static_cast<Kind>(blockKind);
                b.join = static_cast<Join>(join);
                if (!readCondition(in, b.condition) || !readCondition(in, b.extra))
                {
                    return false;
                }
                p.blocks.push_back(b);
            }
            if (!validate(p))
            {
                return false;
            }
            const auto actual = snapshot(owner);
            bool matches = !stamp.empty() && actual.rows.size() == p.rows.size() && actual.rail == p.rail && identity(owner) == stamp;
            for (size_t j = 0; matches && j < p.rows.size(); ++j)
            {
                matches = actual.rows[j].raw == p.rows[j].raw;
            }
            if (matches)
            {
                if (!loaded.emplace(owner, std::move(p)).second)
                {
                    return false;
                }
            }
            else
            {
                Diagnostics::Logging::warn("Rules companion owner {} does not match this save; rules ignored", owner.id);
            }
        }
        size_t running;
        if (!(in >> running) || running > 1000)
        {
            return false;
        }
        std::map<uint16_t, VehicleRun> loadedRuns;
        for (size_t i = 0; i < running; ++i)
        {
            uint16_t id;
            VehicleRun run;
            unsigned ownerKind;
            std::string stamp;
            auto& r = run.runtime;
            if (!(in >> id >> ownerKind >> run.owner.id >> std::quoted(stamp) >> run.ready >> run.hasOrder >> run.reachedOrder >> r.revision >> r.pc >> r.current >> r.waiting >> r.waitStart >> r.budgetTick >> r.steps >> std::quoted(r.status)) || ownerKind > 1 || r.steps > 256 || r.pc > 2048 || r.status.size() > 256)
            {
                return false;
            }
            run.owner.kind = static_cast<OwnerKind>(ownerKind);
            size_t frames;
            if (!(in >> frames) || frames > 4)
            {
                return false;
            }
            for (size_t j = 0; j < frames; ++j)
            {
                Key key;
                uint16_t remaining;
                if (!(in >> key >> remaining) || remaining < 1 || remaining > 20 || !r.repeats.emplace(key, remaining).second)
                {
                    return false;
                }
            }
            if (!(in >> frames) || frames > 256)
            {
                return false;
            }
            for (size_t j = 0; j < frames; ++j)
            {
                Key key;
                bool passed;
                if (!(in >> key >> passed) || !r.branches.emplace(key, passed).second)
                {
                    return false;
                }
            }
            if (!(in >> frames) || frames > 256)
            {
                return false;
            }
            for (size_t j = 0; j < frames; ++j)
            {
                Key key;
                bool passed;
                if (!(in >> key >> passed) || !r.prechecked.emplace(key, passed).second)
                {
                    return false;
                }
            }
            if (!(in >> frames) || frames > 63)
            {
                return false;
            }
            for (size_t j = 0; j < frames; ++j)
            {
                Key key;
                bool allowed;
                if (!(in >> key >> allowed) || !run.cargoDecisions.emplace(key, allowed).second)
                {
                    return false;
                }
            }
            if (!(in >> frames) || frames > 63)
            {
                return false;
            }
            for (size_t j = 0; j < frames; ++j)
            {
                Key key;
                if (!(in >> key) || !r.skipped.insert(key).second)
                {
                    return false;
                }
            }
            if (loaded.contains(run.owner) && ownerForVehicle(id) == run.owner && identity({ OwnerKind::vehicle, id }) == stamp)
            {
                const auto& p = loaded.at(run.owner);
                if (!validRuntime(p, r))
                {
                    return false;
                }
                if (run.reachedOrder && indexOf(p, run.reachedOrder) >= p.rows.size())
                {
                    return false;
                }
                for (auto [key, allowed] : run.cargoDecisions)
                {
                    if (indexOf(p, key) >= p.rows.size())
                    {
                        return false;
                    }
                }
                if (!loadedRuns.emplace(id, std::move(run)).second)
                {
                    return false;
                }
            }
        }
        in >> std::ws;
        if (!in.eof())
        {
            return false;
        }
        programs = std::move(loaded);
        vehicles = std::move(loadedRuns);
        return true;
    }
}
