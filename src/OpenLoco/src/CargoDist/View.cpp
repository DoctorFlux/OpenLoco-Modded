#include "CargoDist/View.h"
#include "CargoDist/Game.h"
#include "Entities/EntityManager.h"
#include "Localisation/FormatArguments.hpp"
#include "Localisation/Formatting.h"
#include "Localisation/StringIds.h"
#include "Localisation/StringManager.h"
#include "Objects/CargoObject.h"
#include "Objects/ObjectManager.h"
#include "Ui/WindowManager.h"
#include "Vehicles/Vehicle.h"
#include "Vehicles/VehicleBody.h"
#include "Vehicles/VehicleBogie.h"
#include "Vehicles/VehicleHead.h"
#include "World/StationManager.h"
#include <algorithm>
#include <tuple>

namespace OpenLoco::CargoDist
{
    CargoSummary summarize(const State& state, const std::vector<Holder>& holders, uint8_t company, uint16_t station, size_t& budget)
    {
        CargoSummary result;
        result.pending = state.dirty || state.job.has_value();
        std::map<std::tuple<uint8_t, uint16_t, uint16_t>, uint32_t> quantities;
        for (auto holder : holders)
        {
            if (holder.cargo >= 32 || state.settings.modes[holder.cargo] == Mode::manual)
            {
                continue;
            }
            result.automatic = true;
            const auto entry = state.holdings.find(holder);
            if (entry == state.holdings.end())
            {
                continue;
            }
            for (const auto& packet : entry->second.packets)
            {
                if (!budget)
                {
                    result.limited = true;
                    break;
                }
                --budget;
                if (packet.destination == kUnknownStation)
                {
                    result.unknown += packet.quantity;
                    continue;
                }
                const auto next = state.nextHop(company, holder.cargo, station, packet.destination);
                quantities[{ holder.cargo, packet.destination, next }] += packet.quantity;
            }
        }
        for (const auto& [key, quantity] : quantities)
        {
            result.destinations.push_back({ std::get<0>(key), std::get<1>(key), std::get<2>(key), quantity });
        }
        std::sort(result.destinations.begin(), result.destinations.end(), [](const auto& a, const auto& b) {
            if (a.quantity != b.quantity)
            {
                return a.quantity > b.quantity;
            }
            return std::tie(a.cargo, a.destination, a.next) < std::tie(b.cargo, b.destination, b.next);
        });
        return result;
    }
    const CargoRow* CargoRows::at(int32_t y) const
    {
        const auto it = std::upper_bound(rows.begin(), rows.end(), y, [](auto position, const auto& row) { return position < row.top; });
        if (it == rows.begin())
        {
            return nullptr;
        }
        const auto& row = *std::prev(it);
        return y < row.top + row.height ? &row : nullptr;
    }
    CargoRows layoutCargoRows(const std::vector<CargoRow>& entries)
    {
        CargoRows result;
        for (auto row : entries)
        {
            if (result.height + row.baseHeight + int32_t(row.information.size() * 10) > 32000)
            {
                break;
            }
            row.top = result.height;
            row.height = row.baseHeight + static_cast<int32_t>(row.information.size() * 10);
            result.height += row.height;
            result.rows.push_back(std::move(row));
        }
        return result;
    }
    namespace
    {
        struct Session
        {
            uint64_t generation = 0, ownerGeneration = 0, signature = 0;
            uint16_t frame = 0;
            uint8_t company = 255;
            bool dirty = true;
            std::set<uint16_t> expanded;
            CargoRows layout;
        };
        std::map<std::pair<Ui::WindowType, uint16_t>, Session> sessions;
        std::string text(StringId id, FormatArgumentsView args = {})
        {
            char buffer[1024]{};
            StringManager::formatString(buffer, id, args);
            return buffer;
        }
        std::string stationName(uint16_t id)
        {
            const auto* station = id < 1024 ? StationManager::get(static_cast<StationId>(id)) : nullptr;
            return station && !station->empty() ? text(station->name, FormatArguments::common(station->town)) : text(StringIds::cargo_transfer_connection);
        }
        std::vector<std::string> information(const CargoSummary& summary, bool expanded)
        {
            if (!summary.automatic)
            {
                return {};
            }
            std::string caption;
            if (summary.limited)
            {
                caption = text(StringIds::cargo_transfer_limited);
            }
            else if (summary.destinations.empty())
            {
                caption = text(summary.pending ? StringIds::cargo_transfer_finding : StringIds::cargo_transfer_connection);
            }
            else if (summary.destinations.size() == 1)
            {
                const auto& destination = summary.destinations.front();
                const auto name = stationName(destination.destination);
                caption = text(StringIds::cargo_transfer_to, FormatArguments::common(name.c_str()));
                if (destination.next == kUnknownStation && !summary.pending)
                {
                    caption += " (" + text(StringIds::cargo_transfer_connection) + ")";
                }
                if (destination.next != kUnknownStation && destination.next != destination.destination)
                {
                    const auto via = stationName(destination.next);
                    caption += text(StringIds::cargo_transfer_via, FormatArguments::common(via.c_str()));
                }
            }
            else
            {
                caption = text(StringIds::cargo_transfer_many, FormatArguments::common(static_cast<uint16_t>(summary.destinations.size())));
            }
            std::vector<std::string> result{ std::string(expanded ? "- " : "+ ") + caption };
            if (expanded)
            {
                for (const auto& destination : summary.destinations)
                {
                    const auto name = stationName(destination.destination);
                    const auto* cargo = ObjectManager::get<CargoObject>(destination.cargo);
                    std::string units = cargo ? text(destination.quantity == 1 ? cargo->unitNameSingular : cargo->unitNamePlural, FormatArguments::common(destination.quantity)) : std::to_string(destination.quantity);
                    result.push_back("  " + units + " " + text(StringIds::cargo_transfer_to, FormatArguments::common(name.c_str())));
                    if (result.size() >= 64)
                    {
                        result.push_back(text(StringIds::cargo_transfer_limited));
                        break;
                    }
                }
                if (summary.unknown)
                {
                    result.push_back("  " + std::to_string(summary.unknown) + " " + text(summary.pending ? StringIds::cargo_transfer_finding : StringIds::cargo_transfer_connection));
                }
            }
            return result;
        }
    }
    const CargoRows& cargoRows(Ui::Window& window)
    {
        auto& session = sessions[{ window.type, window.number }];
        const bool stationWindow = window.type == Ui::WindowType::station;
        const auto epoch = stationWindow ? stationGeneration(window.number) : componentGeneration(window.number);
        if (session.generation != generation() || session.ownerGeneration != epoch)
        {
            session = {};
            session.generation = generation();
            session.ownerGeneration = epoch;
        }
        std::vector<CargoRow> entries;
        std::vector<std::vector<Holder>> holders;
        uint64_t signature = state().revision;
        uint8_t company = 255;
        uint16_t source = kUnknownStation;
        auto hash = [&](auto value) { signature = (signature ^ uint64_t(value)) * 1099511628211ULL; };
        if (stationWindow)
        {
            const auto* station = StationManager::get(static_cast<StationId>(window.number));
            if (station && !station->empty())
            {
                source = window.number;
                company = enumValue(station->owner);
                for (uint8_t cargo = 0; cargo < 32; ++cargo)
                {
                    const auto& native = station->cargoStats[cargo];
                    hash(native.quantity);
                    hash(enumValue(native.origin));
                    if (!native.quantity)
                    {
                        continue;
                    }
                    entries.push_back({ cargo, 0, native.origin != station->id() ? 22 : 12, 0, native.quantity, enumValue(native.origin), {} });
                    holders.push_back({ { false, window.number, cargo, 0 } });
                }
            }
        }
        else
        {
            const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(window.number));
            if (head && head->isVehicleHead())
            {
                company = enumValue(head->owner);
                source = lastStation(window.number);
                if (source == kUnknownStation)
                {
                    source = enumValue(head->stationId);
                }
                Vehicles::Vehicle train(*head);
                for (auto& car : train.cars)
                {
                    std::vector<Holder> cargoHolders;
                    uint32_t quantity = 0;
                    for (auto& part : car)
                    {
                        for (auto [cargo, holder] : { std::pair{ &part.body->primaryCargo, Holder{ true, enumValue(part.body->id), part.body->primaryCargo.type, 0 } },
                                                      std::pair{ &part.front->secondaryCargo, Holder{ true, enumValue(part.front->id), part.front->secondaryCargo.type, 1 } },
                                                      std::pair{ &part.back->secondaryCargo, Holder{ true, enumValue(part.back->id), part.back->secondaryCargo.type, 1 } } })
                        {
                            hash(holder.id);
                            hash(componentGeneration(holder.id));
                            hash(cargo->qty);
                            hash(cargo->type);
                            quantity += cargo->qty;
                            if (cargo->qty)
                            {
                                cargoHolders.push_back(holder);
                            }
                        }
                    }
                    entries.push_back({ enumValue(car.front->id), 0, window.rowHeight, 0, quantity, kUnknownStation, {} });
                    holders.push_back(std::move(cargoHolders));
                }
            }
        }
        if (session.company != company)
        {
            session.expanded.clear();
            session.dirty = true;
            session.company = company;
        }
        hash(company);
        hash(source);
        hash(window.rowHeight);
        if (!session.dirty && signature == session.signature && static_cast<uint16_t>(window.frameNo - session.frame) < 16)
        {
            return session.layout;
        }
        size_t budget = 8192;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].quantity)
            {
                entries[i].information = information(summarize(state(), holders[i], company, source, budget), session.expanded.contains(entries[i].key));
            }
        }
        session.layout = layoutCargoRows(entries);
        session.signature = signature;
        session.frame = window.frameNo;
        session.dirty = false;
        return session.layout;
    }
    void toggleCargoRow(Ui::Window& window, int16_t y)
    {
        const auto* row = cargoRows(window).at(y);
        if (!row || row->information.empty())
        {
            return;
        }
        auto& session = sessions[{ window.type, window.number }];
        if (!session.expanded.erase(row->key))
        {
            session.expanded.insert(row->key);
        }
        session.dirty = true;
        window.updateScrollWidgets();
        window.invalidate();
    }
    void closeCargoRows(Ui::Window& window) { sessions.erase({ window.type, window.number }); }
}
