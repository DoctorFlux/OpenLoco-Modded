#include "Rules/Rules.h"
#include "Lines/Line.h"

#include "Entities/EntityManager.h"
#include "GameCommands/GameCommands.h"
#include "GameCommands/Vehicles/VehicleRepaint.h"
#include "Objects/ObjectManager.h"
#include "Objects/VehicleObject.h"
#include "World/CompanyManager.h"
#include "Scenario/ScenarioManager.h"
#include "GameCommands/Vehicles/VehicleChangeRunningMode.h"
#include "Map/RoadElement.h"
#include "Map/SurfaceElement.h"
#include "Map/TileManager.h"
#include "Map/TrackElement.h"
#include "Objects/ObjectManager.h"
#include "Objects/RoadObject.h"
#include "Vehicles/OrderManager.h"
#include "Vehicles/Vehicle.h"
#include "Vehicles/VehicleBody.h"
#include "Vehicles/VehicleBogie.h"
#include "Vehicles/Vehicle1.h"
#include "Vehicles/Vehicle2.h"
#include "Vehicles/VehicleHead.h"
#include "World/Station.h"
#include "World/StationManager.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>

namespace OpenLoco::Lines
{
    namespace
    {
        std::vector<Line> _lines;
        std::map<uint16_t, std::vector<LineOrder>> _privateOrders;
        std::map<uint16_t, LineCursor> _cursors;
        std::map<uint32_t, uint32_t> _stopDepartureTick;
        std::map<uint32_t, uint32_t> _vehicleStopDepartureTick;
        std::map<uint16_t, uint32_t> _roundTripTicks;
        std::map<uint32_t, uint32_t> _spreadHoldStart;
        uint16_t _purchaseLine = 0;
        bool _purchaseArmed = false;
        uint16_t _nextId = 1;

        Line* findMutable(uint16_t lineId)
        {
            for (auto& line : _lines)
            {
                if (line.id == lineId)
                {
                    return &line;
                }
            }
            return nullptr;
        }

        LineKind kindsForMode(TransportMode mode)
        {
            switch (mode)
            {
                case TransportMode::rail:
                    return LineKind::rail;
                case TransportMode::road:
                    return LineKind::road | LineKind::tram;
                case TransportMode::air:
                    return LineKind::air;
                case TransportMode::water:
                    return LineKind::water;
            }
            return LineKind::none;
        }

        bool placedVehicleBlocks(const Line& line, LineKind next)
        {
            for (const auto vehicleId : line.vehicles)
            {
                auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId));
                if (head == nullptr || !head->isPlaced())
                {
                    continue;
                }
                if (!vehicleFitsLine(head->vehicleType, next))
                {
                    return true;
                }
            }
            return false;
        }

        bool isStationOrder(Vehicles::OrderType type)
        {
            return type == Vehicles::OrderType::StopAt || type == Vehicles::OrderType::RouteThrough;
        }

        bool sameConsecutiveStop(const LineOrder& previous, const LineOrder& next)
        {
            return isStationOrder(previous.type) && isStationOrder(next.type) && previous.station == next.station;
        }

        uint16_t wrapMinute(int32_t minute)
        {
            constexpr int32_t kDay = 24 * 60;
            minute %= kDay;
            if (minute < 0)
            {
                minute += kDay;
            }
            return static_cast<uint16_t>(minute);
        }

        uint16_t orderSize(Vehicles::OrderType type)
        {
            switch (type)
            {
                case Vehicles::OrderType::StopAt:
                case Vehicles::OrderType::RouteThrough:
                    return 2;
                case Vehicles::OrderType::RouteWaypoint:
                    return 6;
                case Vehicles::OrderType::End:
                case Vehicles::OrderType::UnloadAll:
                case Vehicles::OrderType::WaitFor:
                    return 1;
            }
            return 1;
        }

        bool canSyncOrders(const Line& line, size_t count, size_t bytes, std::optional<uint16_t> additionalVehicle = {})
        {
            if (count > Limits::kMaxOrdersPerVehicle) return false;
            int64_t delta = 0;
            for (const auto id : line.vehicles)
                if (const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(id))) delta += static_cast<int64_t>(bytes) - head->sizeOfOrderTable;
            if (additionalVehicle && std::find(line.vehicles.begin(), line.vehicles.end(), *additionalVehicle) == line.vehicles.end())
                if (const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(*additionalVehicle))) delta += static_cast<int64_t>(bytes) - head->sizeOfOrderTable;
            return static_cast<int64_t>(Vehicles::OrderManager::orderTableLength()) + delta <= static_cast<int64_t>(Limits::kMaxOrders);
        }

        void syncVehicleOrders(uint16_t vehicleId, const std::vector<LineOrder>& orders)
        {
            auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId));
            if (head == nullptr || head->sizeOfOrderTable == 0)
            {
                return;
            }
            const auto preservedOrder = head->currentOrder;
            const auto preserveRules = Rules::find(Rules::ownerForVehicle(vehicleId)) != nullptr;
            Vehicles::OrderManager::freeOrders(head);
            Vehicles::OrderManager::allocateOrders(*head);
            uint16_t offset = 0;
            for (const auto& order : orders)
            {
                if (order.type == Vehicles::OrderType::StopAt)
                {
                    Vehicles::OrderStopAt stop(static_cast<StationId>(order.station));
                    Vehicles::OrderManager::insertOrder(head, offset, &stop);
                }
                else if (order.type == Vehicles::OrderType::RouteThrough)
                {
                    Vehicles::OrderRouteThrough route(static_cast<StationId>(order.station));
                    Vehicles::OrderManager::insertOrder(head, offset, &route);
                }
                else if (order.type == Vehicles::OrderType::UnloadAll)
                {
                    Vehicles::OrderUnloadAll unload(order.cargo);
                    Vehicles::OrderManager::insertOrder(head, offset, &unload);
                }
                else if (order.type == Vehicles::OrderType::WaitFor)
                {
                    Vehicles::OrderWaitFor wait(order.cargo);
                    Vehicles::OrderManager::insertOrder(head, offset, &wait);
                }
                else if (order.type == Vehicles::OrderType::RouteWaypoint)
                {
                    Vehicles::OrderRouteWaypoint waypoint(World::TilePos2{ order.tileX, order.tileY }, order.baseZ, order.direction, order.trackId);
                    Vehicles::OrderManager::insertOrder(head, offset, &waypoint);
                }
                else
                {
                    continue;
                }
                offset = static_cast<uint16_t>(offset + orderSize(order.type));
            }
            if (preserveRules) head->currentOrder = preservedOrder < head->sizeOfOrderTable - 1 ? preservedOrder : 0;
        }

        void syncLine(const Line& line)
        {
            for (const auto vehicleId : line.vehicles)
            {
                syncVehicleOrders(vehicleId, line.orders);
            }
        }

        void applyExpress(const Line& line)
        {
            for (const auto vehicleId : line.vehicles)
            {
                auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId));
                if (head == nullptr || head->sizeOfOrderTable == 0 || !head->isVehicleHead())
                {
                    continue;
                }
                Vehicles::Vehicle train(*head);
                if (train.veh1 == nullptr)
                {
                    continue;
                }
                const bool isExpress = (train.veh1->var_48 & Vehicles::Flags48::expressMode) != Vehicles::Flags48::none;
                if (isExpress == line.express)
                {
                    continue;
                }
                GameCommands::VehicleChangeRunningModeArgs args{};
                args.head = head->id;
                args.mode = GameCommands::VehicleChangeRunningModeArgs::Mode::toggleLocalExpress;
                GameCommands::doCommand(args, GameCommands::Flags::apply);
            }
        }

        std::vector<size_t> routableIndexes(const Line& line)
        {
            std::vector<size_t> indexes;
            for (size_t i = 0; i < line.orders.size(); ++i)
            {
                const auto type = line.orders[i].type;
                if (type == Vehicles::OrderType::StopAt || type == Vehicles::OrderType::RouteThrough || type == Vehicles::OrderType::RouteWaypoint)
                {
                    indexes.push_back(i);
                }
            }
            return indexes;
        }
    }

    std::filesystem::path companionPath(const std::filesystem::path& savePath)
    {
        return std::filesystem::path(savePath.string() + ".olines");
    }

    void clearLines()
    {
        clearRouteView();
        _lines.clear();
        _privateOrders.clear();
        _cursors.clear();
        _stopDepartureTick.clear();
        _vehicleStopDepartureTick.clear();
        _roundTripTicks.clear();
        _spreadHoldStart.clear();
        _purchaseArmed = false;
        _purchaseLine = 0;
        _nextId = 1;
    }

    const std::vector<Line>& allLines()
    {
        return _lines;
    }

    const Line* findLine(uint16_t lineId)
    {
        return findMutable(lineId);
    }

    const Line* lineForVehicle(uint16_t vehicleId)
    {
        for (const auto& line : _lines)
        {
            if (std::find(line.vehicles.begin(), line.vehicles.end(), vehicleId) != line.vehicles.end())
            {
                return &line;
            }
        }
        return nullptr;
    }

    const std::vector<LineOrder>* ordersForVehicle(uint16_t vehicleId)
    {
        if (const auto* line = lineForVehicle(vehicleId))
        {
            return &line->orders;
        }
        const auto found = _privateOrders.find(vehicleId);
        if (found == _privateOrders.end())
        {
            return nullptr;
        }
        return &found->second;
    }

    bool canRun(const Line& line)
    {
        std::vector<uint16_t> stations;
        for (const auto& order : line.orders)
        {
            if (!isStationOrder(order.type))
            {
                continue;
            }
            if (std::find(stations.begin(), stations.end(), order.station) == stations.end())
            {
                stations.push_back(order.station);
            }
        }
        return stations.size() >= 2;
    }

    LineError createLine(std::string name, TransportMode mode, uint8_t colour)
    {
        if (colour < 1 || colour > 9)
        {
            return { false, 0, "line colour must be a bright colour from 1 to 9" };
        }
        if (name.empty())
        {
            name = "Line " + std::to_string(_nextId);
        }
        Line line;
        line.id = _nextId++;
        line.name = std::move(name);
        line.mode = mode;
        line.colour = colour;
        line.secondaryColour = colour;
        for (size_t scheme = 0; scheme < kVehicleColourSchemes; ++scheme)
        {
            line.schemePrimary[scheme] = colour;
            line.schemeSecondary[scheme] = colour;
        }
        const auto id = line.id;
        _lines.push_back(std::move(line));
        return { true, id, {} };
    }

    LineError createCompanyLine(std::string name, TransportMode mode, uint8_t primary, uint8_t secondary)
    {
        if (primary >= static_cast<uint8_t>(Colour::max) || secondary >= static_cast<uint8_t>(Colour::max))
        {
            return { false, 0, "line colour is outside the company palette" };
        }
        if (name.empty())
        {
            name = "Line " + std::to_string(_nextId);
        }
        Line line;
        line.id = _nextId++;
        line.name = std::move(name);
        line.mode = mode;
        line.colour = primary;
        line.secondaryColour = secondary;
        for (size_t scheme = 0; scheme < kVehicleColourSchemes; ++scheme)
        {
            line.schemePrimary[scheme] = primary;
            line.schemeSecondary[scheme] = secondary;
        }
        const auto id = line.id;
        _lines.push_back(std::move(line));
        return { true, id, {} };
    }

    LineError renameLine(uint16_t lineId, std::string name)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        if (name.empty())
        {
            return { false, 0, "line name is empty" };
        }
        line->name = std::move(name);
        return { true, 0, {} };
    }

    LineError recolourLine(uint16_t lineId, uint8_t colour)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        if (colour < 1 || colour > 9)
        {
            return { false, 0, "line colour must be a bright colour from 1 to 9" };
        }
        line->colour = colour;
        line->secondaryColour = colour;
        invalidateRouteView();
        return { true, 0, {} };
    }

    namespace
    {
        ColourScheme companySchemeForObject(const Company* company, const VehicleObject* object)
        {
            if (company == nullptr)
            {
                return { Colour::black, Colour::black };
            }
            auto scheme = company->mainColours;
            if (object != nullptr && object->colourType > 0 && object->colourType <= kVehicleColourSchemes && (company->customVehicleColoursSet & (1U << object->colourType)) != 0)
            {
                scheme = company->vehicleColours[object->colourType - 1];
            }
            return scheme;
        }

        bool usesOwnColours(const Line& line, uint16_t vehicleId)
        {
            return std::find(line.ownColourVehicles.begin(), line.ownColourVehicles.end(), vehicleId) != line.ownColourVehicles.end();
        }

        ColourScheme schemeForObject(const Line& line, const VehicleObject* object)
        {
            ColourScheme scheme{ static_cast<Colour>(line.colour), static_cast<Colour>(line.secondaryColour) };
            if (object != nullptr && object->colourType > 0 && object->colourType <= kVehicleColourSchemes)
            {
                const auto index = static_cast<size_t>(object->colourType - 1);
                if ((line.schemeEnabled & (1U << index)) != 0)
                {
                    scheme.primary = static_cast<Colour>(line.schemePrimary[index]);
                    scheme.secondary = static_cast<Colour>(line.schemeSecondary[index]);
                }
            }
            return scheme;
        }

        void paintOneVehicle(const Line& line, Vehicles::VehicleHead& head, bool useLineColours)
        {
            if (usesOwnColours(line, enumValue(head.id)))
            {
                return;
            }
            const auto* company = CompanyManager::get(head.owner);
            Vehicles::Vehicle train(head);
            for (auto& car : train.cars)
            {
                Vehicles::VehicleBase* source = car.body;
                uint16_t objectId = car.body != nullptr ? car.body->objectId : 0;
                if (source == nullptr && car.front != nullptr)
                {
                    source = car.front;
                    objectId = car.front->objectId;
                }
                if (source == nullptr)
                {
                    continue;
                }
                const auto* object = ObjectManager::get<VehicleObject>(objectId);
                GameCommands::VehicleRepaintArgs args{};
                args.paintFlags = GameCommands::VehicleRepaintFlags::paintFromVehicleUi;
                args.head = source->id;
                args.setColours(useLineColours ? schemeForObject(line, object) : companySchemeForObject(company, object));
                GameCommands::doCommand(args, GameCommands::Flags::apply);
            }
        }

        void paintAssignedVehicles(const Line& line, bool useLineColours)
        {
            for (const auto vehicleId : line.vehicles)
            {
                auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId));
                if (head == nullptr || !head->isVehicleHead())
                {
                    continue;
                }
                paintOneVehicle(line, *head, useLineColours);
            }
        }
    }

    LineError setLineColours(uint16_t lineId, uint8_t primary, uint8_t secondary)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        if (primary >= static_cast<uint8_t>(Colour::max) || secondary >= static_cast<uint8_t>(Colour::max))
        {
            return { false, 0, "line colour is outside the company palette" };
        }
        line->colour = primary;
        line->secondaryColour = secondary;
        invalidateRouteView();
        if (line->paintVehicles)
        {
            paintAssignedVehicles(*line, true);
        }
        return { true, lineId, {} };
    }

    LineError setPaintVehicles(uint16_t lineId, bool paint)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        line->paintVehicles = paint;
        paintAssignedVehicles(*line, paint);
        return { true, lineId, {} };
    }

    bool lineSchemeEnabled(const Line& line, size_t index)
    {
        return index < kVehicleColourSchemes && (line.schemeEnabled & (1U << index)) != 0;
    }

    LineError setLineSchemeEnabled(uint16_t lineId, size_t index, bool enabled)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr || index >= kVehicleColourSchemes)
        {
            return { false, 0, "line not found" };
        }
        if (enabled)
        {
            line->schemeEnabled |= static_cast<uint16_t>(1U << index);
        }
        else
        {
            line->schemeEnabled &= static_cast<uint16_t>(~(1U << index));
        }
        if (line->paintVehicles)
        {
            paintAssignedVehicles(*line, true);
        }
        return { true, lineId, {} };
    }

    LineError setLineSchemeColours(uint16_t lineId, size_t index, uint8_t primary, uint8_t secondary)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr || index >= kVehicleColourSchemes)
        {
            return { false, 0, "line not found" };
        }
        if (primary >= static_cast<uint8_t>(Colour::max) || secondary >= static_cast<uint8_t>(Colour::max))
        {
            return { false, 0, "line colour is outside the company palette" };
        }
        line->schemePrimary[index] = primary;
        line->schemeSecondary[index] = secondary;
        if (line->paintVehicles)
        {
            paintAssignedVehicles(*line, true);
        }
        return { true, lineId, {} };
    }

    bool vehicleUsesOwnColours(uint16_t lineId, uint16_t vehicleId)
    {
        const auto* line = findLine(lineId);
        return line != nullptr && std::find(line->ownColourVehicles.begin(), line->ownColourVehicles.end(), vehicleId) != line->ownColourVehicles.end();
    }

    LineError setVehicleOwnColours(uint16_t lineId, uint16_t vehicleId, bool own)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        auto& owned = line->ownColourVehicles;
        const auto found = std::find(owned.begin(), owned.end(), vehicleId);
        if (own)
        {
            if (found == owned.end())
            {
                owned.push_back(vehicleId);
            }
            return { true, lineId, {} };
        }
        if (found != owned.end())
        {
            owned.erase(found);
        }
        if (line->paintVehicles)
        {
            if (auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId)))
            {
                paintOneVehicle(*line, *head, true);
            }
        }
        return { true, lineId, {} };
    }

    void noteVehiclePainted(uint16_t vehicleId)
    {
        for (auto& line : _lines)
        {
            if (std::find(line.vehicles.begin(), line.vehicles.end(), vehicleId) == line.vehicles.end())
            {
                continue;
            }
            if (std::find(line.ownColourVehicles.begin(), line.ownColourVehicles.end(), vehicleId) == line.ownColourVehicles.end())
            {
                line.ownColourVehicles.push_back(vehicleId);
            }
            return;
        }
    }

    LineError setLoop(uint16_t lineId, bool loop)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        line->loop = loop;
        return { true, 0, {} };
    }

    LineError setExpress(uint16_t lineId, bool express)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        line->express = express;
        applyExpress(*line);
        return { true, 0, {} };
    }

    LineError deleteLine(uint16_t lineId)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        for (const auto vehicleId : line->vehicles)
        {
            Rules::detached(vehicleId, lineId);
            _privateOrders[vehicleId] = line->orders;
            _cursors.erase(vehicleId);
        }
        Rules::forget({ Rules::OwnerKind::line, lineId });
        _lines.erase(std::remove_if(_lines.begin(), _lines.end(), [&](const Line& item) { return item.id == lineId; }), _lines.end());
        return { true, 0, {} };
    }

    LineError addOrder(uint16_t lineId, LineOrder order)
    {
        const auto* line = findLine(lineId);
        if (line == nullptr) return { false, 0, "line not found" };
        if (!line->orders.empty() && sameConsecutiveStop(line->orders.back(), order))
            return { false, 0, "consecutive identical stops are not allowed" };
        return insertOrder(lineId, line->orders.size(), std::move(order));
    }

    LineOrder fromVehicleOrder(const Vehicles::Order& order)
    {
        LineOrder result;
        result.type = order.getType();
        if (const auto* station = order.as<Vehicles::OrderStation>())
        {
            result.station = enumValue(station->getStation());
        }
        if (order.getType() == Vehicles::OrderType::UnloadAll || order.getType() == Vehicles::OrderType::WaitFor)
        {
            result.cargo = static_cast<const Vehicles::OrderCargo&>(order).getCargo();
        }
        if (const auto* waypoint = order.as<Vehicles::OrderRouteWaypoint>())
        {
            const auto pos = waypoint->getWaypoint();
            result.tileX = static_cast<int16_t>(pos.x / World::kTileSize);
            result.tileY = static_cast<int16_t>(pos.y / World::kTileSize);
            result.baseZ = static_cast<uint8_t>(pos.z / 8);
            result.direction = waypoint->getDirection();
            result.trackId = waypoint->getTrackId();
        }
        return result;
    }

    LineError insertOrder(uint16_t lineId, size_t index, LineOrder order)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        if (order.type == Vehicles::OrderType::End || static_cast<unsigned>(order.type) > 5) return { false, 0, "invalid order type" };
        if (index > line->orders.size())
        {
            index = line->orders.size();
        }
        if (order.type == Vehicles::OrderType::StopAt && index > 0 && line->mode != TransportMode::air && line->mode != TransportMode::water)
        {
            auto& previous = line->orders[index - 1];
            if (previous.type == Vehicles::OrderType::StopAt && previous.station == order.station)
            {
                previous.type = Vehicles::OrderType::RouteThrough;
                Rules::noteRawOrderChange({ Rules::OwnerKind::line, lineId }, index - 1, Vehicles::OrderRouteThrough(static_cast<StationId>(previous.station)).getRaw());
                syncLine(*line);
                return { true, 0, {} };
            }
        }
        if (index > 0 && sameConsecutiveStop(line->orders[index - 1], order))
        {
            return { false, 0, "consecutive identical stops are not allowed" };
        }
        if (index < line->orders.size() && sameConsecutiveStop(order, line->orders[index]))
        {
            return { false, 0, "consecutive identical stops are not allowed" };
        }
        size_t bytes = 1 + orderSize(order.type);
        for (const auto& existing : line->orders) bytes += orderSize(existing.type);
        if (!canSyncOrders(*line, line->orders.size() + 1, bytes)) return { false, 0, "order table is full" };
        Rules::Edit edit; edit.kind = Rules::EditKind::insertRow; edit.first = index;
        // Convert before the move into the line vector.
        const auto raw = [&]() -> uint64_t {
            using namespace Vehicles;
            switch (order.type) {
                case OrderType::StopAt: return OrderStopAt(static_cast<StationId>(order.station)).getRaw();
                case OrderType::RouteThrough: return OrderRouteThrough(static_cast<StationId>(order.station)).getRaw();
                case OrderType::RouteWaypoint: return OrderRouteWaypoint(World::TilePos2{ order.tileX, order.tileY }, order.baseZ, order.direction, order.trackId).getRaw();
                case OrderType::UnloadAll: return OrderUnloadAll(order.cargo).getRaw();
                case OrderType::WaitFor: return OrderWaitFor(order.cargo).getRaw();
                default: return 0;
            }
        }();
        edit.raw = raw; if (const auto rulesResult = Rules::noteOrderEdit({ Rules::OwnerKind::line, lineId }, edit); !rulesResult) return { false, 0, rulesResult.message };
        line->orders.insert(line->orders.begin() + static_cast<std::ptrdiff_t>(index), std::move(order));
        syncLine(*line);
        invalidateRouteView();
        return { true, 0, {} };
    }

    LineError moveOrder(uint16_t lineId, size_t index, int delta)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr || index >= line->orders.size())
        {
            return { false, 0, "order not found" };
        }
        const auto destination = static_cast<int32_t>(index) + delta;
        if (destination < 0 || destination >= static_cast<int32_t>(line->orders.size()))
        {
            return { false, 0, "order not found" };
        }
        Rules::Edit edit; edit.kind = Rules::EditKind::moveRows; edit.first = edit.last = index; edit.destination = delta < 0 ? static_cast<size_t>(destination) : static_cast<size_t>(destination + 1);
        if (const auto rulesResult = Rules::noteOrderEdit({ Rules::OwnerKind::line, lineId }, edit); !rulesResult) return { false, 0, rulesResult.message };
        std::swap(line->orders[index], line->orders[static_cast<size_t>(destination)]);
        syncLine(*line);
        invalidateRouteView();
        return { true, 0, {} };
    }

    LineError reverseOrders(uint16_t lineId)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        Rules::Edit edit; edit.kind = Rules::EditKind::reverseRows; if (const auto rulesResult = Rules::noteOrderEdit({ Rules::OwnerKind::line, lineId }, edit); !rulesResult) return { false, 0, rulesResult.message };
        std::reverse(line->orders.begin(), line->orders.end());
        syncLine(*line);
        invalidateRouteView();
        return { true, 0, {} };
    }

    LineError replaceOrders(uint16_t lineId, std::vector<LineOrder> orders)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr || orders.size() > Limits::kMaxOrdersPerVehicle)
            return { false, 0, "line not found or too many orders" };
        size_t needed = 1;
        for (const auto& order : orders)
        {
            if (order.type == Vehicles::OrderType::End || static_cast<unsigned>(order.type) > 5)
                return { false, 0, "invalid order" };
            needed += orderSize(order.type);
        }
        int64_t delta = 0;
        for (const auto vehicleId : line->vehicles)
            if (auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId)))
                delta += static_cast<int64_t>(needed) - head->sizeOfOrderTable;
        if (static_cast<int64_t>(Vehicles::OrderManager::orderTableLength()) + delta > static_cast<int64_t>(Limits::kMaxOrders))
            return { false, 0, "order table is full" };
        line->orders = std::move(orders);
        syncLine(*line);
        invalidateRouteView();
        return { true, 0, {} };
    }

    LineError removeOrder(uint16_t lineId, size_t index)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr || index >= line->orders.size())
        {
            return { false, 0, "order not found" };
        }
        Rules::Edit edit; edit.kind = Rules::EditKind::deleteRow; edit.first = index;
        if (const auto rulesResult = Rules::noteOrderEdit({ Rules::OwnerKind::line, lineId }, edit); !rulesResult) return { false, 0, rulesResult.message };
        line->orders.erase(line->orders.begin() + static_cast<std::ptrdiff_t>(index));
        syncLine(*line);
        invalidateRouteView();
        return { true, 0, {} };
    }

    LineKind roadObjectKind(uint8_t roadObjectId)
    {
        const auto* road = ObjectManager::get<RoadObject>(roadObjectId);
        if (road != nullptr && !road->hasFlags(RoadObjectFlags::isRoad))
        {
            return LineKind::tram;
        }
        return LineKind::road;
    }

    static LineKind roadKindsAtStation(const Station& station)
    {
        bool road = false;
        bool tram = false;
        for (uint16_t i = 0; i < station.stationTileSize; ++i)
        {
            const auto pos = station.stationTiles[i];
            auto tile = World::TileManager::get(World::Pos2{ pos.x, pos.y });
            if (tile.isNull())
            {
                continue;
            }
            for (auto& entry : tile)
            {
                auto* element = entry.as<World::RoadElement>();
                if (element == nullptr || element->isGhost())
                {
                    continue;
                }
                if (roadObjectKind(element->roadObjectId()) == LineKind::tram)
                {
                    tram = true;
                }
                else
                {
                    road = true;
                }
            }
        }
        if (!road && !tram)
        {
            return LineKind::road;
        }
        auto kinds = LineKind::none;
        if (road)
        {
            kinds |= LineKind::road;
        }
        if (tram)
        {
            kinds |= LineKind::tram;
        }
        return kinds;
    }

    LineKind stationKinds(uint16_t stationId)
    {
        const auto* station = StationManager::get(static_cast<StationId>(stationId));
        if (station == nullptr || station->empty())
        {
            return LineKind::none;
        }
        auto kinds = LineKind::none;
        const auto flags = station->flags;
        if ((flags & StationFlags::transportModeRail) != StationFlags::none)
        {
            kinds |= LineKind::rail;
        }
        if ((flags & StationFlags::transportModeAir) != StationFlags::none)
        {
            kinds |= LineKind::air;
        }
        if ((flags & StationFlags::transportModeWater) != StationFlags::none)
        {
            kinds |= LineKind::water;
        }
        if ((flags & StationFlags::transportModeRoad) != StationFlags::none)
        {
            kinds |= roadKindsAtStation(*station);
        }
        return kinds;
    }

    std::optional<TransportMode> stationTransportMode(uint16_t stationId)
    {
        const auto kinds = stationKinds(stationId);
        if (kinds == LineKind::none)
        {
            return std::nullopt;
        }
        std::optional<TransportMode> mode;
        bool conflict = false;
        auto add = [&](LineKind bit, TransportMode value) {
            if (conflict || (kinds & bit) == LineKind::none)
            {
                return;
            }
            if (!mode.has_value())
            {
                mode = value;
            }
            else if (*mode != value)
            {
                conflict = true;
                mode.reset();
            }
        };
        add(LineKind::rail, TransportMode::rail);
        if ((kinds & (LineKind::road | LineKind::tram)) != LineKind::none)
        {
            add(LineKind::road | LineKind::tram, TransportMode::road);
        }
        add(LineKind::air, TransportMode::air);
        add(LineKind::water, TransportMode::water);
        return mode;
    }

    bool stopFits(uint16_t lineId, LineKind stopKinds)
    {
        const auto* line = findLine(lineId);
        if (line == nullptr || stopKinds == LineKind::none)
        {
            return false;
        }
        const auto next = line->kinds & stopKinds;
        if (next == LineKind::none)
        {
            return false;
        }
        return !placedVehicleBlocks(*line, next);
    }

    LineError acceptStopKinds(uint16_t lineId, LineKind stopKinds)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        if (!stopFits(lineId, stopKinds))
        {
            return { false, 0, "stops belong to another mode" };
        }
        line->kinds &= stopKinds;
        if (lineKindIsOne(line->kinds))
        {
            line->mode = transportModeForKind(line->kinds);
        }
        return { true, lineId, {} };
    }

    LineError setLineMode(uint16_t lineId, TransportMode mode)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        if (line->mode == mode)
        {
            return { true, lineId, {} };
        }
        for (const auto vehicleId : line->vehicles)
        {
            auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId));
            if (head != nullptr && head->isPlaced() && head->mode != mode)
            {
                return { false, 0, "assigned vehicles use another mode" };
            }
        }
        for (const auto& order : line->orders)
        {
            if (order.type == Vehicles::OrderType::RouteWaypoint)
            {
                return { false, 0, "waypoints belong to the current mode" };
            }
            if (order.type != Vehicles::OrderType::StopAt && order.type != Vehicles::OrderType::RouteThrough)
            {
                continue;
            }
            const auto kinds = stationKinds(order.station);
            if (kinds == LineKind::none)
            {
                continue;
            }
            if ((kinds & kindsForMode(mode)) == LineKind::none)
            {
                return { false, 0, "stops belong to another mode" };
            }
        }
        line->mode = mode;
        return { true, lineId, {} };
    }

    LineKind waypointKinds(const LineOrder& order)
    {
        const World::Pos2 pos{ static_cast<coord_t>(order.tileX * World::kTileSize), static_cast<coord_t>(order.tileY * World::kTileSize) };
        auto tile = World::TileManager::get(pos);
        if (tile.isNull())
        {
            return LineKind::none;
        }
        auto kinds = LineKind::none;
        for (auto& entry : tile)
        {
            if (auto* track = entry.as<World::TrackElement>(); track != nullptr && !track->isGhost() && !track->isAiAllocated())
            {
                if (order.trackId == 0 || track->trackId() == order.trackId)
                {
                    kinds |= LineKind::rail;
                }
            }
            if (auto* road = entry.as<World::RoadElement>(); road != nullptr && !road->isGhost() && !road->isAiAllocated())
            {
                if (order.trackId == 0 || road->roadId() == order.trackId)
                {
                    kinds |= roadObjectKind(road->roadObjectId());
                }
            }
        }
        if (kinds == LineKind::none && tile.surface() != nullptr && tile.surface()->waterHeight() > 0)
        {
            kinds |= LineKind::water;
        }
        return kinds;
    }

    void reconcileLineMode(uint16_t lineId)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return;
        }
        auto allowed = kEveryLineKind;
        bool saw = false;
        for (const auto& order : line->orders)
        {
            LineKind kinds = LineKind::none;
            if (order.type == Vehicles::OrderType::RouteWaypoint)
            {
                kinds = waypointKinds(order);
            }
            else if (order.type == Vehicles::OrderType::StopAt || order.type == Vehicles::OrderType::RouteThrough)
            {
                kinds = stationKinds(order.station);
            }
            if (kinds == LineKind::none)
            {
                continue;
            }
            saw = true;
            allowed &= kinds;
        }
        if (!saw || allowed == LineKind::none || placedVehicleBlocks(*line, allowed))
        {
            return;
        }
        line->kinds = allowed;
        if (lineKindIsOne(allowed))
        {
            line->mode = transportModeForKind(allowed);
        }
    }

    LineError assignVehicle(uint16_t lineId, uint16_t vehicleId, TransportMode mode)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr)
        {
            return { false, 0, "line not found" };
        }
        if (!lineAcceptsMode(*line, mode))
        {
            return { false, 0, "vehicle mode does not match the line" };
        }
        size_t bytes = 1;
        for (const auto& order : line->orders) bytes += orderSize(order.type);
        if (!canSyncOrders(*line, line->orders.size(), bytes, vehicleId)) return { false, 0, "order table is full" };
        if (line->kinds != kEveryLineKind)
        {
            if (auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId)))
            {
                if (!vehicleFitsLine(head->vehicleType, line->kinds))
                {
                    return { false, 0, "vehicle mode does not match the line" };
                }
            }
        }
        if (const auto* existing = lineForVehicle(vehicleId))
        {
            if (existing->id != lineId)
            {
                unassignVehicle(vehicleId);
                line = findMutable(lineId);
            }
        }
        Rules::assigned(vehicleId);
        _privateOrders.erase(vehicleId);
        if (std::find(line->vehicles.begin(), line->vehicles.end(), vehicleId) == line->vehicles.end())
        {
            line->vehicles.push_back(vehicleId);
        }
        _cursors[vehicleId] = {};
        syncLine(*line);
        applyExpress(*line);
        if (line->paintVehicles)
        {
            paintAssignedVehicles(*line, true);
        }
        return { true, 0, {} };
    }

    LineError unassignVehicle(uint16_t vehicleId)
    {
        for (auto& line : _lines)
        {
            const auto found = std::find(line.vehicles.begin(), line.vehicles.end(), vehicleId);
            if (found == line.vehicles.end())
            {
                continue;
            }
            Rules::detached(vehicleId, line.id);
            _privateOrders[vehicleId] = line.orders;
            const auto own = std::find(line.ownColourVehicles.begin(), line.ownColourVehicles.end(), vehicleId) != line.ownColourVehicles.end();
            line.vehicles.erase(found);
            line.ownColourVehicles.erase(std::remove(line.ownColourVehicles.begin(), line.ownColourVehicles.end(), vehicleId), line.ownColourVehicles.end());
            _cursors.erase(vehicleId);
            if (line.paintVehicles && !own)
            {
                if (auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId)))
                {
                    paintOneVehicle(line, *head, false);
                }
            }
            return { true, 0, {} };
        }
        return { false, 0, "vehicle is not on a line" };
    }

    void setTimetable(uint16_t lineId, bool autoUnbunch, uint16_t gapMinutes, uint16_t roundTripMinutes)
    {
        if (auto* line = findMutable(lineId))
        {
            line->autoUnbunch = autoUnbunch;
            line->unbunchGapMinutes = gapMinutes;
            line->roundTripMinutes = roundTripMinutes;
        }
    }

    void setStopSettings(uint16_t lineId, size_t index, DepartureRule rule, uint16_t minWait, uint16_t maxWait, uint8_t platform, std::vector<CargoFilter> filters, std::vector<TimeSlot> slots)
    {
        auto* line = findMutable(lineId);
        if (line == nullptr || index >= line->orders.size())
        {
            return;
        }
        auto& order = line->orders[index];
        order.departure = rule;
        order.minWait = minWait;
        order.maxWait = maxWait;
        order.platform = platform;
        order.filters = std::move(filters);
        order.slots = std::move(slots);
    }

    uint16_t orderByteOffset(const Line& line, size_t index)
    {
        uint16_t offset = 0;
        for (size_t i = 0; i < index && i < line.orders.size(); ++i)
        {
            offset = static_cast<uint16_t>(offset + orderSize(line.orders[i].type));
        }
        return offset;
    }

    size_t orderIndexAtOffset(const Line& line, uint16_t offset)
    {
        for (size_t i = 0; i < line.orders.size(); ++i)
        {
            const auto start = orderByteOffset(line, i);
            if (offset < start)
            {
                return i == 0 ? 0 : i - 1;
            }
            if (offset == start)
            {
                return i;
            }
        }
        return line.orders.empty() ? 0 : line.orders.size() - 1;
    }

    namespace
    {
        constexpr size_t kMaxDiagramStops = 63;

        struct IndexedStop
        {
            size_t orderIndex = 0;
            uint16_t station = 0;
            bool stationStop = false;
        };

        bool isNumberedStop(Vehicles::OrderType type)
        {
            return type == Vehicles::OrderType::StopAt || type == Vehicles::OrderType::RouteThrough || type == Vehicles::OrderType::RouteWaypoint;
        }

        size_t collectNumberedStops(const Line& line, IndexedStop* stops)
        {
            size_t count = 0;
            for (size_t i = 0; i < line.orders.size() && count < kMaxDiagramStops; ++i)
            {
                const auto& order = line.orders[i];
                if (!isNumberedStop(order.type))
                {
                    continue;
                }
                stops[count].orderIndex = i;
                stops[count].station = order.station;
                stops[count].stationStop = order.type == Vehicles::OrderType::StopAt || order.type == Vehicles::OrderType::RouteThrough;
                count++;
            }
            return count;
        }

        uint32_t straightDistance(int32_t fromX, int32_t fromY, int32_t toX, int32_t toY)
        {
            const int64_t dx = static_cast<int64_t>(fromX) - toX;
            const int64_t dy = static_cast<int64_t>(fromY) - toY;
            return static_cast<uint32_t>(std::llround(std::sqrt(static_cast<double>(dx * dx + dy * dy))));
        }

        // Distance already covered, over the distance still ahead plus the distance covered.
        // 20 tiles into a 100-tile gap is 20 percent, and 100 percent only when the vehicle is at the next stop.
        uint16_t fractionBetween(int32_t fromX, int32_t fromY, int32_t toX, int32_t toY, int32_t vehicleX, int32_t vehicleY)
        {
            const auto covered = straightDistance(fromX, fromY, vehicleX, vehicleY);
            const auto ahead = straightDistance(vehicleX, vehicleY, toX, toY);
            const auto total = static_cast<uint64_t>(covered) + ahead;
            if (total == 0)
            {
                return 500;
            }
            return static_cast<uint16_t>(std::min<uint64_t>(1000, covered * 1000 / total));
        }

        bool isWrapLeg(bool loop, size_t stopCount, size_t fromOrdinal, size_t toOrdinal)
        {
            return loop && stopCount > 1 && fromOrdinal + 1 == stopCount && toOrdinal == 0;
        }
    }

    LineProgress progressAlongLine(const Line& line, uint16_t currentOrder, uint16_t stationId, bool atStation, int32_t vehicleX, int32_t vehicleY, const std::vector<StopPosition>& positions)
    {
        IndexedStop stops[kMaxDiagramStops]{};
        const auto stopCount = collectNumberedStops(line, stops);
        if (stopCount == 0)
        {
            return {};
        }

        if (atStation)
        {
            const auto current = orderIndexAtOffset(line, currentOrder);
            size_t match = stopCount;
            for (size_t i = 0; i < stopCount; ++i)
            {
                if (!stops[i].stationStop || stops[i].station != stationId)
                {
                    continue;
                }
                if (stops[i].orderIndex == current)
                {
                    match = i;
                    break;
                }
                if (match == stopCount || stops[i].orderIndex <= current)
                {
                    match = i;
                }
            }
            if (match < stopCount)
            {
                LineProgress progress;
                progress.fromOrdinal = match;
                progress.toOrdinal = match;
                progress.atStop = true;
                return progress;
            }
        }

        if (stopCount == 1)
        {
            LineProgress progress;
            progress.atStop = true;
            return progress;
        }

        const auto current = orderIndexAtOffset(line, currentOrder);
        size_t destination = 0;
        bool foundDestination = false;
        for (size_t i = 0; i < stopCount; ++i)
        {
            if (stops[i].orderIndex >= current)
            {
                destination = i;
                foundDestination = true;
                break;
            }
        }
        if (!foundDestination)
        {
            destination = 0;
        }

        const auto origin = destination > 0 ? destination - 1 : (stopCount > 1 ? stopCount - 1 : destination);
        LineProgress progress;
        progress.fromOrdinal = origin;
        progress.toOrdinal = destination;
        progress.atStop = origin == destination;
        progress.wraps = !progress.atStop && isWrapLeg(true, stopCount, origin, destination);
        if (progress.atStop)
        {
            return progress;
        }

        if (positions.size() != stopCount)
        {
            progress.perThousand = 500;
            return progress;
        }

        progress.perThousand = fractionBetween(positions[origin].x, positions[origin].y, positions[destination].x, positions[destination].y, vehicleX, vehicleY);
        return progress;
    }

    size_t diagramGapCount(size_t stopCount, bool loop)
    {
        if (loop && stopCount > 1)
        {
            return stopCount;
        }
        return std::max<size_t>(1, stopCount == 0 ? 1 : stopCount - 1);
    }

    int32_t diagramStopX(int32_t left, int32_t width, size_t stopCount, bool loop, size_t ordinal)
    {
        const auto gaps = static_cast<int32_t>(diagramGapCount(stopCount, loop));
        if (stopCount == 0)
        {
            return left;
        }
        const auto clamped = static_cast<int32_t>(std::min(ordinal, stopCount - 1));
        return left + width * clamped / gaps;
    }

    int32_t diagramMarkerX(int32_t left, int32_t width, size_t stopCount, bool loop, const LineProgress& progress)
    {
        if (stopCount == 0)
        {
            return left;
        }
        if (progress.wraps && progress.perThousand >= 1000)
        {
            return diagramStopX(left, width, stopCount, loop, 0);
        }
        if (progress.atStop || progress.fromOrdinal == progress.toOrdinal || stopCount == 1)
        {
            return diagramStopX(left, width, stopCount, loop, progress.fromOrdinal);
        }
        const auto from = diagramStopX(left, width, stopCount, loop, progress.fromOrdinal);
        if (progress.wraps)
        {
            const auto end = left + width;
            return from + (end - from) * static_cast<int32_t>(progress.perThousand) / 1000;
        }
        const auto to = diagramStopX(left, width, stopCount, loop, progress.toOrdinal);
        return from + (to - from) * static_cast<int32_t>(progress.perThousand) / 1000;
    }

    uint16_t timetableMinute(uint32_t scenarioTicks)
    {
        constexpr uint32_t kTicksPerMinute = 40;
        constexpr uint32_t kMinutesPerDay = 24 * 60;
        return static_cast<uint16_t>((scenarioTicks / kTicksPerMinute) % kMinutesPerDay);
    }

    EvenSpread chooseEvenSpread(int32_t lineLength, uint16_t myId, int32_t myDistance, const SpreadPlace* others, size_t otherCount, uint32_t headwayTicks)
    {
        if (lineLength <= 0 || others == nullptr || otherCount == 0)
        {
            return {};
        }
        const auto running = static_cast<int32_t>(otherCount + 1);
        const auto ideal = lineLength / running;
        if (ideal <= 0)
        {
            return {};
        }

        auto wrap = [lineLength](int32_t distance) {
            auto wrapped = distance % lineLength;
            if (wrapped < 0)
            {
                wrapped += lineLength;
            }
            return wrapped;
        };
        const auto mine = wrap(myDistance);
        bool samePlaceLowerId = false;
        int32_t bestAhead = lineLength;
        bool foundAhead = false;
        for (size_t i = 0; i < otherCount; ++i)
        {
            const auto other = wrap(others[i].distance);
            auto forward = other - mine;
            if (forward < 0)
            {
                forward += lineLength;
            }
            if (forward == 0)
            {
                if (others[i].id < myId)
                {
                    samePlaceLowerId = true;
                }
                continue;
            }
            foundAhead = true;
            if (forward < bestAhead)
            {
                bestAhead = forward;
            }
        }

        const auto gap = samePlaceLowerId ? 0 : (foundAhead ? bestAhead : lineLength);
        if (gap >= ideal)
        {
            return {};
        }
        const auto missing = static_cast<uint64_t>(ideal - gap);
        auto cap = headwayTicks == 0 ? 1U : static_cast<uint32_t>(static_cast<uint64_t>(headwayTicks) * missing / static_cast<uint64_t>(ideal));
        if (cap == 0)
        {
            cap = 1;
        }
        if (headwayTicks > 0 && cap > headwayTicks)
        {
            cap = headwayTicks;
        }
        return { true, cap };
    }

    namespace
    {
        bool isLeavingStop(Vehicles::OrderType type)
        {
            return type == Vehicles::OrderType::StopAt || type == Vehicles::OrderType::RouteThrough;
        }

        int numberedOrdinal(const Line& line, size_t index)
        {
            int ordinal = 0;
            const auto last = std::min(index, line.orders.size());
            for (size_t i = 0; i < last; ++i)
            {
                if (isLeavingStop(line.orders[i].type) || line.orders[i].type == Vehicles::OrderType::RouteWaypoint)
                {
                    ordinal++;
                }
            }
            return ordinal;
        }

        size_t leavingStopIndex(const Line& line, uint16_t currentOrder, uint16_t stationId)
        {
            auto index = orderIndexAtOffset(line, currentOrder);
            if (index < line.orders.size() && isLeavingStop(line.orders[index].type) && line.orders[index].station == stationId)
            {
                return index;
            }
            for (size_t i = std::min(index, line.orders.size()); i-- > 0;)
            {
                if (isLeavingStop(line.orders[i].type))
                {
                    return i;
                }
            }
            return index;
        }

        bool isInService(const Vehicles::VehicleHead* head)
        {
            if (head == nullptr || !head->isVehicleHead() || head->tileX == -1 || head->sizeOfOrderTable == 0)
            {
                return false;
            }
            if (head->hasVehicleFlags(Vehicles::VehicleFlags::commandStop))
            {
                return false;
            }
            return head->status != Vehicles::Status::stopped;
        }

        void recordDeparture(uint16_t lineId, uint16_t vehicleId, size_t stopIndex, uint32_t now)
        {
            const auto key = (static_cast<uint32_t>(stopIndex) << 16) | lineId;
            const auto vehicleKey = (static_cast<uint32_t>(stopIndex) << 16) | vehicleId;
            const auto previous = _vehicleStopDepartureTick.find(vehicleKey);
            if (previous != _vehicleStopDepartureTick.end() && now > previous->second)
            {
                _roundTripTicks[lineId] = now - previous->second;
            }
            _vehicleStopDepartureTick[vehicleKey] = now;
            _stopDepartureTick[key] = now;
            _spreadHoldStart.erase(vehicleKey);
        }

        struct LoopGeometry
        {
            int32_t cumulative[64]{};
            int32_t length = 0;
            size_t stopCount = 0;
            std::vector<StopPosition> positions;
        };

        LoopGeometry measureLoop(const Line& line)
        {
            LoopGeometry loop;
            for (const auto& order : line.orders)
            {
                if (!isNumberedStop(order.type) || loop.stopCount >= std::size(loop.cumulative))
                {
                    continue;
                }
                StopPosition position;
                if (order.type == Vehicles::OrderType::RouteWaypoint)
                {
                    position.x = static_cast<int32_t>(order.tileX) * World::kTileSize;
                    position.y = static_cast<int32_t>(order.tileY) * World::kTileSize;
                }
                else if (const auto* station = StationManager::get(static_cast<StationId>(order.station)); station != nullptr && !station->empty())
                {
                    position.x = station->x;
                    position.y = station->y;
                }
                loop.positions.push_back(position);
                loop.stopCount++;
            }
            if (loop.stopCount < 2)
            {
                return loop;
            }
            int32_t cursor = 0;
            for (size_t i = 0; i < loop.stopCount; ++i)
            {
                loop.cumulative[i] = cursor;
                const auto next = (i + 1) % loop.stopCount;
                cursor += static_cast<int32_t>(straightDistance(loop.positions[i].x, loop.positions[i].y, loop.positions[next].x, loop.positions[next].y));
            }
            loop.length = cursor;
            return loop;
        }

        int32_t distanceOnLoop(const LoopGeometry& loop, const LineProgress& progress)
        {
            if (loop.stopCount == 0 || loop.length <= 0)
            {
                return 0;
            }
            auto from = progress.fromOrdinal;
            if (from >= loop.stopCount)
            {
                from = 0;
            }
            if (progress.atStop || progress.fromOrdinal == progress.toOrdinal)
            {
                return loop.cumulative[from];
            }
            const auto start = loop.cumulative[from];
            auto end = loop.length;
            if (!progress.wraps && progress.toOrdinal < loop.stopCount)
            {
                end = loop.cumulative[progress.toOrdinal];
            }
            if (end < start)
            {
                end = loop.length;
            }
            const auto leg = end - start;
            const auto fraction = std::min<uint16_t>(progress.perThousand, 1000);
            return start + leg * static_cast<int32_t>(fraction) / 1000;
        }

        int32_t runningDistance(const Line& line, const Vehicles::VehicleHead& head, const LoopGeometry& loop)
        {
            Vehicles::Vehicle train(head);
            const auto* body = train.veh2 != nullptr && train.veh2->tileX != -1 ? static_cast<const Vehicles::VehicleBase*>(train.veh2) : static_cast<const Vehicles::VehicleBase*>(&head);
            const auto placed = body->tileX != -1;
            const auto atStation = head.status == Vehicles::Status::loading || head.status == Vehicles::Status::unloading;
            const auto progress = progressAlongLine(line, head.currentOrder, static_cast<uint16_t>(head.stationId), atStation, placed ? body->tileX : 0, placed ? body->tileY : 0, placed ? loop.positions : std::vector<StopPosition>{});
            return distanceOnLoop(loop, progress);
        }
    }

    bool holdForTimetable(uint16_t vehicleId, uint16_t currentOrder, uint16_t stationId)
    {
        const auto* line = lineForVehicle(vehicleId);
        if (line == nullptr || line->orders.empty() || stationId == 0xFFFF)
        {
            return false;
        }
        const auto stopIndex = leavingStopIndex(*line, currentOrder, stationId);
        if (stopIndex >= line->orders.size())
        {
            return false;
        }
        const auto& stop = line->orders[stopIndex];
        const auto now = ScenarioManager::getScenarioTicks();
        const auto minute = timetableMinute(now);
        const auto loop = measureLoop(*line);
        SpreadPlace others[64]{};
        size_t otherCount = 0;
        for (const auto otherId : line->vehicles)
        {
            if (otherId == vehicleId || otherCount >= std::size(others))
            {
                continue;
            }
            auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(otherId));
            if (!isInService(head))
            {
                continue;
            }
            others[otherCount++] = { otherId, runningDistance(*line, *head, loop) };
        }
        const auto running = static_cast<int>(otherCount + 1);
        if (!stop.slots.empty())
        {
            const auto decision = decideDeparture(stop, minute, 0xFFFF, 0, false, 0, static_cast<uint16_t>(std::max(running, 1)));
            if (decision.wait && !decision.late)
            {
                return true;
            }
        }
        if (!line->autoUnbunch || running < 2)
        {
            recordDeparture(line->id, vehicleId, stopIndex, now);
            return false;
        }
        const auto measured = _roundTripTicks.find(line->id);
        const auto headway = measured == _roundTripTicks.end() || measured->second == 0 ? static_cast<uint32_t>(10 * 40) : static_cast<uint32_t>(measured->second / static_cast<uint32_t>(std::max(running, 1)));
        const auto ordinal = numberedOrdinal(*line, stopIndex);
        const auto myDistance = ordinal >= 0 && static_cast<size_t>(ordinal) < loop.stopCount ? loop.cumulative[ordinal] : 0;
        const auto spread = chooseEvenSpread(loop.length, vehicleId, myDistance, others, otherCount, headway);
        const auto holdKey = (static_cast<uint32_t>(stopIndex) << 16) | vehicleId;
        if (!spread.hold)
        {
            _spreadHoldStart.erase(holdKey);
            recordDeparture(line->id, vehicleId, stopIndex, now);
            return false;
        }
        const auto started = _spreadHoldStart.find(holdKey);
        if (started == _spreadHoldStart.end())
        {
            _spreadHoldStart[holdKey] = now;
            return true;
        }
        if (now >= started->second && now - started->second >= spread.capTicks)
        {
            _spreadHoldStart.erase(holdKey);
            recordDeparture(line->id, vehicleId, stopIndex, now);
            return false;
        }
        return true;
    }

    void armPurchase(uint16_t lineId)
    {
        _purchaseLine = lineId;
        _purchaseArmed = true;
    }

    void clearPurchase()
    {
        _purchaseArmed = false;
        _purchaseLine = 0;
    }

    uint16_t purchaseLine()
    {
        return _purchaseArmed ? _purchaseLine : 0;
    }

    LineCursor cursorForVehicle(uint16_t vehicleId)
    {
        const auto found = _cursors.find(vehicleId);
        if (found == _cursors.end())
        {
            return {};
        }
        return found->second;
    }

    LineCursor advanceAssignedVehicle(uint16_t vehicleId)
    {
        const auto* line = lineForVehicle(vehicleId);
        if (line == nullptr)
        {
            return {};
        }
        auto cursor = cursorForVehicle(vehicleId);
        if (cursor.index >= line->orders.size())
        {
            cursor.index = 0;
        }
        cursor = nextStop(*line, cursor);
        _cursors[vehicleId] = cursor;
        return cursor;
    }

    LineCursor nextStop(const Line& line, LineCursor cursor)
    {
        const auto routable = routableIndexes(line);
        if (routable.size() < 2)
        {
            return cursor;
        }
        const auto found = std::find(routable.begin(), routable.end(), cursor.index);
        size_t position = found == routable.end() ? 0 : static_cast<size_t>(std::distance(routable.begin(), found));
        if (!cursor.backward)
        {
            if (position + 1 < routable.size())
            {
                return { routable[position + 1], false };
            }
            if (line.loop)
            {
                return { routable.front(), false };
            }
            return { routable[routable.size() - 2], true };
        }
        if (position > 0)
        {
            return { routable[position - 1], true };
        }
        return { routable[1], false };
    }

    DepartureDecision decideDeparture(const LineOrder& stop, uint16_t arrivalMinute, uint16_t previousDepartureMinute, uint16_t gapMinutes, bool autoUnbunch, uint16_t roundTripMinutes, uint16_t vehicleCount)
    {
        DepartureDecision decision;
        uint16_t gap = gapMinutes;
        if (autoUnbunch && vehicleCount > 0 && roundTripMinutes > 0)
        {
            gap = static_cast<uint16_t>(std::max<uint16_t>(1, roundTripMinutes / vehicleCount));
        }

        bool foundSlot = false;
        uint16_t bestDeparture = 0;
        uint16_t bestDistance = 0xFFFF;
        for (const auto& slot : stop.slots)
        {
            const auto departure = wrapMinute(slot.departureMinute);
            const auto distance = wrapMinute(static_cast<int32_t>(departure) - static_cast<int32_t>(arrivalMinute));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestDeparture = departure;
                foundSlot = true;
            }
        }

        if (foundSlot && bestDistance > 12 * 60)
        {
            decision.late = true;
            decision.wait = false;
            decision.waitMinutes = 0;
            return decision;
        }

        if (foundSlot && bestDeparture != arrivalMinute)
        {
            decision.waitMinutes = bestDistance;
        }

        if (previousDepartureMinute != 0xFFFF && gap > 0)
        {
            const auto since = wrapMinute(static_cast<int32_t>(arrivalMinute) - static_cast<int32_t>(previousDepartureMinute));
            if (since < gap)
            {
                decision.waitMinutes = std::max(decision.waitMinutes, static_cast<uint16_t>(gap - since));
            }
        }

        if (stop.departure != DepartureRule::loadIfAvailable)
        {
            decision.waitMinutes = std::max(decision.waitMinutes, stop.minWait);
            if (stop.maxWait > 0)
            {
                decision.waitMinutes = std::min(decision.waitMinutes, stop.maxWait);
            }
        }

        decision.wait = decision.waitMinutes > 0;
        return decision;
    }

    bool saveCompanion(const std::filesystem::path& savePath)
    {
        std::ofstream stream(companionPath(savePath), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }
        stream << "OLINES 5\n";
        stream << _lines.size() << "\n";
        for (const auto& line : _lines)
        {
            stream << "LINE " << line.id << " " << static_cast<int>(line.colour) << " " << static_cast<int>(line.mode) << " " << (line.loop ? 1 : 0) << " " << (line.autoUnbunch ? 1 : 0) << " " << line.unbunchGapMinutes << " " << line.roundTripMinutes << " " << (line.express ? 1 : 0) << " " << static_cast<int>(line.secondaryColour) << " " << (line.paintVehicles ? 1 : 0) << " " << line.name << "\n";
            stream << "SCHEMES " << line.schemeEnabled;
            for (size_t scheme = 0; scheme < kVehicleColourSchemes; ++scheme)
            {
                stream << " " << static_cast<int>(line.schemePrimary[scheme]) << " " << static_cast<int>(line.schemeSecondary[scheme]);
            }
            stream << "\n";
            stream << "ORDERS " << line.orders.size() << "\n";
            for (const auto& order : line.orders)
            {
                stream << static_cast<int>(order.type) << " " << order.station << " " << static_cast<int>(order.cargo) << " " << static_cast<int>(order.platform) << " " << static_cast<int>(order.departure) << " " << order.minWait << " " << order.maxWait << " " << order.filters.size() << " " << order.slots.size() << " " << order.tileX << " " << order.tileY << " " << static_cast<int>(order.baseZ) << " " << static_cast<int>(order.direction) << " " << static_cast<int>(order.trackId) << "\n";
                for (const auto& filter : order.filters)
                {
                    stream << static_cast<int>(filter.cargoType) << " " << static_cast<int>(filter.percent) << " " << (filter.load ? 1 : 0) << " " << (filter.unload ? 1 : 0) << "\n";
                }
                for (const auto& slot : order.slots)
                {
                    stream << slot.arrivalMinute << " " << slot.departureMinute << "\n";
                }
            }
            stream << "VEHICLES " << line.vehicles.size();
            for (const auto vehicleId : line.vehicles)
            {
                stream << " " << vehicleId;
            }
            stream << "\n";
            stream << "OWN " << line.ownColourVehicles.size();
            for (const auto vehicleId : line.ownColourVehicles)
            {
                stream << " " << vehicleId;
            }
            stream << "\n";
        }
        return static_cast<bool>(stream);
    }

    bool loadCompanion(const std::filesystem::path& savePath)
    {
        clearLines();
        const auto path = companionPath(savePath);
        if (!std::filesystem::is_regular_file(path))
        {
            return true;
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            return false;
        }
        std::string magic;
        int version = 0;
        stream >> magic >> version;
        if (magic != "OLINES" || version < 1 || version > 5)
        {
            clearLines();
            return false;
        }
        size_t lineCount = 0;
        stream >> lineCount;
        for (size_t i = 0; i < lineCount; ++i)
        {
            std::string tag;
            Line line;
            int colour = 1;
            int mode = 0;
            int loop = 0;
            int autoUnbunch = 0;
            int express = 0;
            stream >> tag >> line.id >> colour >> mode >> loop >> autoUnbunch >> line.unbunchGapMinutes >> line.roundTripMinutes;
            if (version >= 3)
            {
                stream >> express;
            }
            int secondary = colour;
            int paintVehicles = 0;
            if (version >= 4)
            {
                stream >> secondary >> paintVehicles;
            }
            std::getline(stream, line.name);
            if (!line.name.empty() && line.name.front() == ' ')
            {
                line.name.erase(line.name.begin());
            }
            line.colour = static_cast<uint8_t>(colour);
            line.secondaryColour = static_cast<uint8_t>(secondary);
            line.paintVehicles = paintVehicles != 0;
            line.mode = static_cast<TransportMode>(mode);
            line.loop = loop != 0;
            line.express = express != 0;
            line.autoUnbunch = autoUnbunch != 0;
            for (size_t scheme = 0; scheme < kVehicleColourSchemes; ++scheme)
            {
                line.schemePrimary[scheme] = line.colour;
                line.schemeSecondary[scheme] = line.secondaryColour;
            }
            if (version >= 5)
            {
                int mask = 0;
                stream >> tag >> mask;
                line.schemeEnabled = static_cast<uint16_t>(mask);
                for (size_t scheme = 0; scheme < kVehicleColourSchemes; ++scheme)
                {
                    int primaryColour = line.colour;
                    int secondaryColour = line.secondaryColour;
                    stream >> primaryColour >> secondaryColour;
                    line.schemePrimary[scheme] = static_cast<uint8_t>(primaryColour);
                    line.schemeSecondary[scheme] = static_cast<uint8_t>(secondaryColour);
                }
            }
            size_t orderCount = 0;
            stream >> tag >> orderCount;
            for (size_t orderIndex = 0; orderIndex < orderCount; ++orderIndex)
            {
                LineOrder order;
                int type = 0;
                int cargo = 0;
                int platform = 0;
                int departure = 0;
                size_t filterCount = 0;
                size_t slotCount = 0;
                stream >> type >> order.station >> cargo >> platform >> departure >> order.minWait >> order.maxWait >> filterCount >> slotCount;
                if (version >= 2)
                {
                    int baseZ = 0;
                    int direction = 0;
                    int trackId = 0;
                    stream >> order.tileX >> order.tileY >> baseZ >> direction >> trackId;
                    order.baseZ = static_cast<uint8_t>(baseZ);
                    order.direction = static_cast<uint8_t>(direction);
                    order.trackId = static_cast<uint8_t>(trackId);
                }
                order.type = static_cast<Vehicles::OrderType>(type);
                order.cargo = static_cast<uint8_t>(cargo);
                order.platform = static_cast<uint8_t>(platform);
                order.departure = static_cast<DepartureRule>(departure);
                for (size_t filterIndex = 0; filterIndex < filterCount; ++filterIndex)
                {
                    CargoFilter filter;
                    int filterCargo = 0;
                    int percent = 100;
                    int load = 1;
                    int unload = 1;
                    stream >> filterCargo >> percent >> load >> unload;
                    filter.cargoType = static_cast<uint8_t>(filterCargo);
                    filter.percent = static_cast<uint8_t>(percent);
                    filter.load = load != 0;
                    filter.unload = unload != 0;
                    order.filters.push_back(filter);
                }
                for (size_t slotIndex = 0; slotIndex < slotCount; ++slotIndex)
                {
                    TimeSlot slot;
                    stream >> slot.arrivalMinute >> slot.departureMinute;
                    order.slots.push_back(slot);
                }
                line.orders.push_back(std::move(order));
            }
            size_t vehicleCount = 0;
            stream >> tag >> vehicleCount;
            for (size_t vehicleIndex = 0; vehicleIndex < vehicleCount; ++vehicleIndex)
            {
                uint16_t vehicleId = 0;
                stream >> vehicleId;
                line.vehicles.push_back(vehicleId);
            }
            if (version >= 5)
            {
                size_t ownCount = 0;
                stream >> tag >> ownCount;
                for (size_t ownIndex = 0; ownIndex < ownCount; ++ownIndex)
                {
                    uint16_t vehicleId = 0;
                    stream >> vehicleId;
                    line.ownColourVehicles.push_back(vehicleId);
                }
            }
            _nextId = std::max<uint16_t>(_nextId, static_cast<uint16_t>(line.id + 1));
            _lines.push_back(std::move(line));
        }
        if (stream)
        {
            const auto loaded = _lines;
            for (const auto& loadedLine : loaded)
            {
                reconcileLineMode(loadedLine.id);
            }
        }
        return static_cast<bool>(stream);
    }
}
