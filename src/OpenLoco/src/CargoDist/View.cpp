#include "CargoDist/View.h"
#include "CargoDist/Game.h"
#include "Entities/EntityManager.h"
#include "GameCommands/GameCommands.h"
#include "Graphics/ImageIds.h"
#include "Graphics/TextRenderer.h"
#include "Lines/Line.h"
#include "Localisation/FormatArguments.hpp"
#include "Localisation/Formatting.h"
#include "Localisation/StringIds.h"
#include "Localisation/StringManager.h"
#include "Objects/CargoObject.h"
#include "Objects/InterfaceSkinObject.h"
#include "Objects/ObjectManager.h"
#include "Scenario/ScenarioManager.h"
#include "Ui/Dropdown.h"
#include "Ui/ScrollView.h"
#include "Ui/Widgets/ButtonWidget.h"
#include "Ui/Widgets/CaptionWidget.h"
#include "Ui/Widgets/FrameWidget.h"
#include "Ui/Widgets/ImageButtonWidget.h"
#include "Ui/Widgets/PanelWidget.h"
#include "Ui/Widgets/ScrollViewWidget.h"
#include "Ui/WindowManager.h"
#include "Vehicles/VehicleHead.h"
#include "World/CompanyManager.h"
#include "World/StationManager.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <numeric>

namespace OpenLoco::CargoDist
{
    ViewLayout viewLayout(int16_t width, int16_t height)
    {
        return { { 4, 57, static_cast<int16_t>(width - 8), static_cast<int16_t>(height - 130) },
                 { 4, 37, static_cast<int16_t>(width - 8), 16 },
                 { 4, static_cast<int16_t>(height - 68), static_cast<int16_t>(width - 8), 43 },
                 { 4, static_cast<int16_t>(height - 22), static_cast<int16_t>(width - 8), 18 } };
    }
    std::vector<ViewRow> groupedRows(const std::vector<ViewEntry>& entries, uint8_t grouping, const std::set<std::string>& expanded, bool largestFirst)
    {
        static constexpr uint8_t orders[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };
        const auto* order = orders[grouping % 6];
        struct Node
        {
            uint8_t cargo, depth, field;
            uint16_t station;
            uint32_t quantity = 0;
            int64_t credit = 0;
            std::string parent;
        };
        std::map<std::string, Node> nodes;
        for (const auto& entry : entries)
        {
            std::string parent;
            for (uint8_t depth = 0; depth < 3; ++depth)
            {
                const auto field = order[depth];
                const auto station = field == 0 ? entry.origin : field == 1 ? entry.next
                                                                            : entry.destination;
                const auto key = parent + "/" + std::to_string(entry.cargo) + ":" + std::to_string(field) + ":" + std::to_string(station);
                auto [it, added] = nodes.try_emplace(key, Node{ entry.cargo, depth, field, station, 0, 0, parent });
                it->second.quantity += entry.quantity;
                it->second.credit += entry.credit;
                parent = key;
            }
        }
        std::vector<ViewRow> result;
        std::map<std::string, std::vector<std::pair<std::string, const Node*>>> childrenByParent;
        for (const auto& [key, node] : nodes)
        {
            childrenByParent[node.parent].emplace_back(key, &node);
        }
        std::function<void(const std::string&)> append = [&](const std::string& parent) {
            if (result.size() >= 4096)
            {
                return;
            }
            auto& children = childrenByParent[parent];
            std::sort(children.begin(), children.end(), [&](const auto& a, const auto& b) {
                if (largestFirst && a.second->quantity != b.second->quantity)
                {
                    return a.second->quantity > b.second->quantity;
                }
                return std::tie(a.second->cargo, a.second->station) < std::tie(b.second->cargo, b.second->station);
            });
            for (const auto& [key, node] : children)
            {
                const auto open = expanded.contains(key);
                result.push_back({ key, node->cargo, node->depth, node->field, node->station, node->quantity, node->credit, open });
                if (open && node->depth < 2)
                {
                    append(key);
                }
                if (result.size() >= 4096)
                {
                    break;
                }
            }
        };
        append({});
        return result;
    }
    namespace
    {
        constexpr int kRowHeight = 14;
        struct Session
        {
            ViewKind kind = ViewKind::settings;
            uint16_t id = 0;
            uint8_t tab = 0, cargo = 32, company = 255, grouping = 0, cargoPage = 0;
            bool largestFirst = true;
            uint64_t generation = 0;
            uint64_t ownerGeneration = 0;
            std::string cargoLabel, companyLabel, groupLabel, sortLabel, caption;
            std::set<std::string> expanded;
            std::vector<ViewRow> rows;
            std::vector<std::string> text;
            std::vector<uint8_t> choices;
            std::optional<Settings> dialogSettings;
            Ui::WidgetId dialogField;
            std::string feedback;
        };
        std::map<std::pair<Ui::WindowType, uint16_t>, Session> _sessions;
        std::map<std::pair<Ui::WindowType, uint16_t>, std::vector<std::string>> _menuLabels;
        bool _overlay = false;
        uint8_t _overlayCargo = 32, _overlayCompany = 255;
        enum Index
        {
            frame,
            caption,
            close,
            panel,
            settings,
            waiting,
            planned,
            capacity,
            cargo,
            company,
            group,
            sort,
            list,
            manual,
            symmetric,
            asymmetric,
            preset,
            overlay,
            distance,
            returning,
            accuracy
        };
        constexpr Ui::WidgetId settingsId{ "cdSettings" }, waitingId{ "cdWaiting" }, plannedId{ "cdPlanned" }, capacityId{ "cdCapacity" },
            cargoId{ "cdCargo" }, companyId{ "cdCompany" }, groupId{ "cdGroup" }, sortId{ "cdSort" }, manualId{ "cdManual" },
            symmetricId{ "cdSymmetric" }, asymmetricId{ "cdAsymmetric" }, presetId{ "cdPreset" }, overlayId{ "cdOverlay" },
            distanceId{ "cdDistance" }, returningId{ "cdReturning" }, accuracyId{ "cdAccuracy" }, closeId{ "cdClose" };
        Session& session(Ui::Window& window) { return _sessions.at({ window.type, window.number }); }
        std::string cargoName(uint8_t type)
        {
            if (const auto* object = ObjectManager::get<CargoObject>(type))
            {
                char buffer[512];
                StringManager::formatString(buffer, object->name);
                return buffer;
            }
            return "Cargo " + std::to_string(type);
        }
        std::string stationName(uint16_t id)
        {
            if (id == kUnknownStation)
            {
                return "Awaiting route";
            }
            if (const auto* station = StationManager::get(static_cast<StationId>(id)); station && !station->empty())
            {
                char buffer[512];
                StringManager::formatString(buffer, station->name, FormatArguments::common(station->town));
                return buffer;
            }
            return "Removed station";
        }
        void refresh(Ui::Window& window)
        {
            auto& s = session(window);
            s.text.clear();
            s.rows.clear();
            if (s.tab == 0)
            {
                for (uint8_t type = 0; type < 32; ++type)
                {
                    if (ObjectManager::get<CargoObject>(type))
                    {
                        s.text.push_back(cargoName(type) + ": " + modeName(state().settings.modes[type]));
                    }
                }
                return;
            }
            if (s.kind == ViewKind::line && s.tab != 0)
            {
                s.tab = 3;
            }
            if (s.tab == 3)
            {
                const auto links = s.kind == ViewKind::line ? lineLinks(s.id) : [&] {
                    std::vector<Link> result;
                    if (s.kind == ViewKind::vehicle)
                    {
                        if (state().services.contains(s.id))
                        {
                            result = state().services.at(s.id);
                        }
                        return result;
                    }
                    for (const auto& [vehicle, links] : state().services)
                    {
                        result.insert(result.end(), links.begin(), links.end());
                    }
                    return result;
                }();
                std::map<RouteKey, std::pair<uint64_t, uint64_t>> sums;
                for (const auto& link : links)
                {
                    if ((s.cargo == 32 || s.cargo == link.cargo) && (s.company == 255 || s.company == link.company) && (s.kind != ViewKind::station || link.from == s.id || link.to == s.id))
                    {
                        auto& sum = sums[{ link.company, link.cargo, link.from, link.to }];
                        sum.first += link.capacity;
                        sum.second += link.usage;
                    }
                }
                for (const auto& [key, sum] : sums)
                {
                    s.text.push_back(cargoName(key.cargo) + ": " + stationName(key.from) + " -> " + stationName(key.to) + "; " + std::to_string(sum.second) + "/" + std::to_string(sum.first) + " used/capacity");
                }
                return;
            }
            std::vector<ViewEntry> entries;
            const auto holders = s.kind == ViewKind::vehicle ? vehicleHolders(s.id) : std::vector<Holder>{};
            if (s.tab == 2)
            {
                for (const auto& flow : state().pendingFlows(s.cargo, s.company, s.kind == ViewKind::station ? s.id : kUnknownStation))
                {
                    const auto& key = flow.key;
                    if ((s.kind == ViewKind::station && key.from != s.id) || (s.cargo != 32 && key.cargo != s.cargo) || (s.company != 255 && key.company != s.company))
                    {
                        continue;
                    }
                    entries.push_back({ key.cargo, key.from, flow.next, key.to, flow.quantity, 0 });
                }
            }
            else
            {
                for (const auto& [holder, holding] : state().holdings)
                {
                    if ((s.cargo != 32 && holder.cargo != s.cargo) || (s.kind == ViewKind::station && (holder.vehicle || holder.id != s.id))
                        || (s.kind == ViewKind::vehicle && std::find(holders.begin(), holders.end(), holder) == holders.end()))
                    {
                        continue;
                    }
                    uint16_t originStation = holder.id;
                    uint8_t vehicleCompany = 255;
                    if (holder.vehicle)
                    {
                        const auto* component = EntityManager::get<Vehicles::VehicleBase>(static_cast<EntityId>(holder.id));
                        const auto* head = component ? EntityManager::get<Vehicles::VehicleHead>(component->head) : nullptr;
                        originStation = head ? enumValue(head->stationId) : kUnknownStation;
                        if (head)
                        {
                            vehicleCompany = enumValue(head->owner);
                            if (originStation == kUnknownStation)
                            {
                                originStation = lastStation(enumValue(head->id));
                            }
                        }
                    }
                    const auto company = holder.vehicle ? vehicleCompany : state().stations.contains(originStation) ? state().stations.at(originStation).company
                                                                                                                    : uint8_t{ 255 };
                    if (s.company != 255 && company != s.company)
                    {
                        continue;
                    }
                    for (const auto& packet : holding.packets)
                    {
                        entries.push_back({ holder.cargo, packet.originRemoved ? kUnknownStation : packet.origin, (originStation == packet.destination ? packet.destination : state().nextHop(company, holder.cargo, originStation == kUnknownStation ? packet.origin : originStation, packet.destination)), packet.destination, packet.quantity, packet.credit });
                    }
                }
            }
            s.rows = groupedRows(entries, s.grouping, s.expanded, s.largestFirst);
            for (const auto& row : s.rows)
            {
                static constexpr const char* fields[] = { "From ", "Via ", "To " };
                s.text.push_back(std::string(row.depth * 2, ' ') + (row.depth < 2 ? row.expanded ? "- " : "+ " : "  ") + cargoName(row.cargo) + ": " + fields[row.field] + (row.field == 0 && row.station == kUnknownStation ? "Unknown/removed origin" : stationName(row.station)) + " - " + std::to_string(row.quantity) + (row.credit ? "; transfer credit " + std::to_string(row.credit) : ""));
            }
        }
        bool apply(Ui::Window& window, const Settings& value)
        {
            const auto before = GameCommands::getUpdatingCompanyId();
            GameCommands::setUpdatingCompanyId(CompanyManager::getControllingId());
            const auto accepted = submitSettings(value);
            GameCommands::setUpdatingCompanyId(before);
            session(window).feedback = accepted ? "Cargo settings updated" : "Cargo setting rejected";
            refresh(window);
            window.invalidate();
            return accepted;
        }
        void onClose(Ui::Window& window)
        {
            _sessions.erase({ window.type, window.number });
            _menuLabels.erase({ window.type, window.number });
        }
        void onMouseUp(Ui::Window& window, Ui::WidgetIndex_t index, Ui::WidgetId id)
        {
            auto& s = session(window);
            if (s.generation != generation())
            {
                return;
            }
            if (id == closeId)
            {
                Ui::WindowManager::close(&window);
                return;
            }
            if (id == settingsId)
            {
                s.tab = 0;
            }
            else if (id == waitingId)
            {
                s.tab = 1;
            }
            else if (id == plannedId)
            {
                s.tab = 2;
            }
            else if (id == capacityId)
            {
                s.tab = 3;
            }
            else if (id == groupId)
            {
                s.grouping = (s.grouping + 1) % 6;
                s.expanded.clear();
            }
            else if (id == sortId)
            {
                s.largestFirst = !s.largestFirst;
            }
            else if (id == overlayId)
            {
                _overlay = !_overlay;
                _overlayCargo = s.cargo;
                _overlayCompany = s.company;
                Gfx::invalidateScreen();
            }
            else if (id == manualId || id == symmetricId || id == asymmetricId || id == presetId)
            {
                auto next = state().settings;
                for (uint8_t type = 0; type < 32; ++type)
                {
                    if (id != presetId && s.cargo != 32 && s.cargo != type)
                    {
                        continue;
                    }
                    if (id == presetId)
                    {
                        const auto* object = ObjectManager::get<CargoObject>(type);
                        next.modes[type] = object && (object->cargoCategory == CargoCategory::passengers || object->cargoCategory == CargoCategory::mail) ? Mode::symmetric : Mode::asymmetric;
                    }
                    else
                    {
                        next.modes[type] = id == manualId ? Mode::manual : id == symmetricId ? Mode::symmetric
                                                                                             : Mode::asymmetric;
                    }
                }
                apply(window, next);
            }
            else if (id == distanceId || id == returningId || id == accuracyId)
            {
                s.dialogSettings = state().settings;
                s.dialogField = id;
                const auto value = id == distanceId ? state().settings.distanceEffect : id == returningId ? state().settings.returning
                                                                                                          : state().settings.accuracy;
                const auto text = std::to_string(value);
                Ui::Windows::TextInput::openTextInput(&window, StringIds::cargo_dist_title, StringIds::rules_number_prompt, StringIds::line_name_raw, index, FormatArguments::common(text.c_str()), 3);
            }
            refresh(window);
            window.updateScrollWidgets();
            window.invalidate();
        }
        void onMouseDown(Ui::Window& window, Ui::WidgetIndex_t index, Ui::WidgetId id)
        {
            if (id != cargoId && id != companyId)
            {
                return;
            }
            auto& s = session(window);
            s.choices.clear();
            auto& labels = _menuLabels[{ window.type, window.number }];
            labels.clear();
            labels.reserve(40);
            s.choices.push_back(id == cargoId ? 32 : 255);
            Ui::Dropdown::add(0, StringIds::line_name_raw, id == cargoId ? "All cargo" : "All companies");
            uint8_t visibleCargo = 0;
            for (uint8_t type = 0; type < (id == cargoId ? 32 : 15); ++type)
            {
                if (id == cargoId && !ObjectManager::get<CargoObject>(type))
                {
                    continue;
                }
                if (id == companyId && CompanyManager::get(static_cast<CompanyId>(type))->empty())
                {
                    continue;
                }
                if (id == cargoId && visibleCargo++ / 29 != s.cargoPage)
                {
                    continue;
                }
                const auto text = id == cargoId ? cargoName(type) : "Company " + std::to_string(type + 1);
                // Native dropdown copies its pointer arguments only; keep strings alive until selection.
                s.choices.push_back(type);
                auto& label = _menuLabels[{ window.type, window.number }];
                label.push_back(text);
                Ui::Dropdown::add(static_cast<uint8_t>(s.choices.size() - 1), StringIds::line_name_raw, label.back().c_str());
            }
            if (id == cargoId && visibleCargo > 29)
            {
                s.choices.push_back(253);
                labels.push_back(s.cargoPage ? "Previous cargo..." : "More cargo...");
                Ui::Dropdown::add(static_cast<uint8_t>(s.choices.size() - 1), StringIds::line_name_raw, labels.back().c_str());
            }
            const auto& widget = window.widgets[index];
            Ui::Dropdown::showText(window.x + widget.left, window.y + widget.top, widget.width(), widget.height(), window.getColour(Ui::WindowColour::secondary), static_cast<uint8_t>(s.choices.size()), 0);
        }
        // deque-like lifetime through reserved vector storage for native raw-string menus.

        void onDropdown(Ui::Window& window, Ui::WidgetIndex_t widgetIndex, Ui::WidgetId id, int16_t choice)
        {
            auto& s = session(window);
            if (s.generation != generation() || choice < 0 || static_cast<size_t>(choice) >= s.choices.size())
            {
                return;
            }
            if (id == cargoId && s.choices[choice] == 253)
            {
                s.cargoPage = s.cargoPage ? 0 : 1;
                onMouseDown(window, widgetIndex, cargoId);
                return;
            }
            if (id == cargoId)
            {
                s.cargo = s.choices[choice];
            }
            else if (id == companyId)
            {
                s.company = s.choices[choice];
            }
            if (_overlay)
            {
                _overlayCargo = s.cargo;
                _overlayCompany = s.company;
                Gfx::invalidateScreen();
            }
            refresh(window);
            window.updateScrollWidgets();
            window.invalidate();
        }
        void textInput(Ui::Window& window, Ui::WidgetIndex_t, Ui::WidgetId id, const char* input)
        {
            auto& s = session(window);
            if (s.generation != generation() || !input || !s.dialogSettings || id != s.dialogField || *s.dialogSettings != state().settings)
            {
                s.dialogSettings.reset();
                return;
            }
            unsigned value = 0;
            const auto length = std::strlen(input);
            const auto parsed = std::from_chars(input, input + length, value);
            if (parsed.ec != std::errc{} || parsed.ptr != input + length || value > 255)
            {
                s.feedback = "Enter a valid value";
                s.dialogSettings.reset();
                window.invalidate();
                return;
            }
            auto next = *s.dialogSettings;
            s.dialogSettings.reset();
            if (id == distanceId)
            {
                next.distanceEffect = static_cast<uint16_t>(value);
            }
            else if (id == returningId)
            {
                next.returning = static_cast<uint8_t>(value);
            }
            else
            {
                next.accuracy = static_cast<uint8_t>(value);
            }
            apply(window, next);
        }
        void scrollMouseDown(Ui::Window& window, int16_t, int16_t y, uint8_t)
        {
            auto& s = session(window);
            auto row = static_cast<size_t>(std::max(0, int(y)) / kRowHeight);
            if (s.tab == 0)
            {
                size_t index = 0;
                for (uint8_t type = 0; type < 32; ++type)
                {
                    if (ObjectManager::get<CargoObject>(type) && index++ == row)
                    {
                        s.cargo = type;
                        break;
                    }
                }
            }
            else if (row < s.rows.size() && s.rows[row].depth < 2)
            {
                const auto key = s.rows[row].key;
                if (!s.expanded.erase(key))
                {
                    s.expanded.insert(key);
                }
            }
            refresh(window);
            window.updateScrollWidgets();
            window.invalidate();
        }
        void prepareDraw(Ui::Window& window)
        {
            auto& s = session(window);
            const auto layout = viewLayout(window.width, window.height);
            window.widgets[frame].right = window.width - 1;
            window.widgets[frame].bottom = window.height - 1;
            window.widgets[panel].right = window.width - 1;
            window.widgets[panel].bottom = window.height - 1;
            window.widgets[caption].right = window.width - 2;
            window.widgets[close].left = window.width - 15;
            window.widgets[close].right = window.width - 3;
            for (int i = 0; i < 4; ++i)
            {
                auto& widget = window.widgets[settings + i];
                widget.left = 4 + i * ((window.width - 8) / 4);
                widget.right = widget.left + (window.width - 8) / 4 - 3;
                auto& filter = window.widgets[cargo + i];
                filter.left = widget.left;
                filter.right = widget.right;
            }
            auto& scroll = window.widgets[list];
            scroll.left = layout.list.x;
            scroll.top = layout.list.y;
            scroll.right = layout.list.x + layout.list.width - 1;
            scroll.bottom = layout.list.y + layout.list.height - 1;
            for (int i = 0; i < 5; ++i)
            {
                auto& widget = window.widgets[manual + i];
                widget.left = 4 + i * ((window.width - 8) / 5);
                widget.right = widget.left + (window.width - 8) / 5 - 3;
                widget.top = layout.controls.y;
                widget.bottom = widget.top + 16;
            }
            for (int i = 0; i < 3; ++i)
            {
                auto& widget = window.widgets[distance + i];
                widget.left = 4 + i * ((window.width - 8) / 3);
                widget.right = widget.left + (window.width - 8) / 3 - 3;
                widget.top = layout.controls.y + 21;
                widget.bottom = widget.top + 16;
            }
            FormatArguments(window.widgets[distance].textArgs).push<uint16_t>(state().settings.distanceEffect);
            FormatArguments(window.widgets[returning].textArgs).push<uint16_t>(state().settings.returning);
            FormatArguments(window.widgets[accuracy].textArgs).push<uint16_t>(state().settings.accuracy);
            s.cargoLabel = s.cargo == 32 ? "All cargo" : cargoName(s.cargo);
            s.companyLabel = s.company == 255 ? "All companies" : "Company " + std::to_string(s.company + 1);
            constexpr const char* groups[] = { "From / Via / To", "From / To / Via", "Via / From / To", "Via / To / From", "To / From / Via", "To / Via / From" };
            s.groupLabel = groups[s.grouping % 6];
            s.sortLabel = s.largestFirst ? "Largest first" : "Station order";
            for (auto [index, label] : { std::pair{ cargo, &s.cargoLabel }, std::pair{ company, &s.companyLabel }, std::pair{ group, &s.groupLabel }, std::pair{ sort, &s.sortLabel } })
            {
                window.widgets[index].text = StringIds::line_name_raw;
                FormatArguments(window.widgets[index].textArgs).push(label->c_str());
            }
            s.caption = "Cargo distribution";
            if (s.kind == ViewKind::station)
            {
                s.caption += ": " + stationName(s.id);
            }
            else if (s.kind == ViewKind::vehicle)
            {
                if (const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(s.id)); head && head->isVehicleHead())
                {
                    s.caption += ": vehicle " + std::to_string(head->ordinalNumber);
                }
            }
            else if (s.kind == ViewKind::line)
            {
                if (const auto* line = Lines::findLine(s.id))
                {
                    s.caption += ": " + line->name;
                }
            }
            window.widgets[caption].text = StringIds::line_name_raw;
            FormatArguments(window.widgets[caption].textArgs).push(s.caption.c_str());
            if (s.kind == ViewKind::line)
            {
                window.disabledWidgets |= (1ULL << waiting) | (1ULL << planned);
            }
            else if (s.kind == ViewKind::vehicle)
            {
                window.disabledWidgets |= 1ULL << planned;
            }
            window.activatedWidgets = (1ULL << (settings + s.tab)) | (_overlay ? 1ULL << overlay : 0);
            if (s.cargo < 32)
            {
                window.activatedWidgets |= 1ULL << (manual + static_cast<unsigned>(state().settings.modes[s.cargo]));
            }
        }
        void draw(Ui::Window& window, Gfx::DrawingContext& context)
        {
            window.draw(context);
            const auto& s = session(window);
            Gfx::TextRenderer text(context);
            const auto layout = viewLayout(window.width, window.height);
            auto footer = status();
            if (!s.feedback.empty())
            {
                footer += " | " + s.feedback;
            }
            std::vector<char> buffer(footer.begin(), footer.end());
            buffer.push_back(0);
            text.clipString(layout.footer.width, buffer.data());
            text.drawString({ layout.footer.x, layout.footer.y }, Colour::black, buffer.data());
            if (_overlay)
            {
                text.setCurrentFont(Gfx::Font::small);
                text.drawString({ layout.footer.x, static_cast<int16_t>(layout.footer.y + 11) }, Colour::black, "White: spare  Green: flow  Yellow: busy  Red: overloaded");
            }
        }
        void drawScroll(Ui::Window& window, Gfx::DrawingContext& context, uint32_t)
        {
            context.clear(Colours::getShade(window.getColour(Ui::WindowColour::secondary).c(), 4));
            Gfx::TextRenderer text(context);
            const auto& s = session(window);
            int16_t y = 0;
            for (const auto& line : s.text)
            {
                std::vector<char> buffer(line.begin(), line.end());
                buffer.push_back(0);
                text.clipString(window.width - 24, buffer.data());
                text.drawString({ 2, y }, Colour::black, buffer.data());
                y += kRowHeight;
                if (y >= 32000)
                {
                    break;
                }
            }
        }
        void getScrollSize(Ui::Window& window, uint32_t, int32_t& width, int32_t& height)
        {
            width = 0;
            height = static_cast<int32_t>(std::min<size_t>(32000, session(window).text.size() * kRowHeight));
        }
        void onUpdate(Ui::Window& window)
        {
            auto& s = session(window);
            bool validOwner = true;
            if (s.kind == ViewKind::vehicle)
            {
                const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(s.id));
                validOwner = head && head->isVehicleHead() && s.ownerGeneration == componentGeneration(s.id);
            }
            else if (s.kind == ViewKind::station)
            {
                const auto* station = StationManager::get(static_cast<StationId>(s.id));
                validOwner = station && !station->empty() && s.ownerGeneration == stationGeneration(s.id);
            }
            else if (s.kind == ViewKind::line)
            {
                validOwner = Lines::findLine(s.id) != nullptr;
            }
            if (!validOwner || s.generation != generation())
            {
                Ui::WindowManager::close(&window);
                return;
            }
            if ((++window.frameNo % 16) == 0)
            {
                refresh(window);
                window.updateScrollWidgets();
                window.invalidate();
            }
        }
        const Ui::WindowEventList events = { .onClose = onClose, .onMouseUp = onMouseUp, .onResize = prepareDraw, .onMouseDown = onMouseDown, .onDropdown = onDropdown, .onUpdate = onUpdate, .getScrollSize = getScrollSize, .scrollMouseDown = scrollMouseDown, .textInput = textInput, .prepareDraw = prepareDraw, .draw = draw, .drawScroll = drawScroll };
        const auto widgets = Ui::makeWidgets(
            Ui::Widgets::Frame({ 0, 0 }, { 620, 340 }, Ui::WindowColour::primary),
            Ui::Widgets::Caption({ 1, 1 }, { 618, 13 }, Ui::Widgets::Caption::Style::colourText, Ui::WindowColour::primary, StringIds::cargo_dist_title),
            Ui::Widgets::ImageButton(closeId, { 605, 2 }, { 13, 13 }, Ui::WindowColour::primary, ImageIds::close_button),
            Ui::Widgets::Panel({ 0, 15 }, { 620, 325 }, Ui::WindowColour::secondary),
            Ui::Widgets::Button(settingsId, { 4, 19 }, { 140, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_settings),
            Ui::Widgets::Button(waitingId, { 148, 19 }, { 140, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_waiting),
            Ui::Widgets::Button(plannedId, { 292, 19 }, { 140, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_planned),
            Ui::Widgets::Button(capacityId, { 436, 19 }, { 140, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_capacity),
            Ui::Widgets::Button(cargoId, { 4, 37 }, { 140, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_cargo),
            Ui::Widgets::Button(companyId, { 148, 37 }, { 140, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_company),
            Ui::Widgets::Button(groupId, { 292, 37 }, { 140, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_group),
            Ui::Widgets::Button(sortId, { 436, 37 }, { 140, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_sort),
            Ui::Widgets::ScrollView({ 4, 57 }, { 612, 210 }, Ui::WindowColour::secondary, Ui::Scrollbars::vertical),
            Ui::Widgets::Button(manualId, { 4, 272 }, { 115, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_manual),
            Ui::Widgets::Button(symmetricId, { 123, 272 }, { 115, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_symmetric),
            Ui::Widgets::Button(asymmetricId, { 242, 272 }, { 115, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_asymmetric),
            Ui::Widgets::Button(presetId, { 361, 272 }, { 115, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_auto),
            Ui::Widgets::Button(overlayId, { 480, 272 }, { 115, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_overlay),
            Ui::Widgets::Button(distanceId, { 4, 293 }, { 195, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_distance),
            Ui::Widgets::Button(returningId, { 203, 293 }, { 195, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_returning),
            Ui::Widgets::Button(accuracyId, { 402, 293 }, { 195, 16 }, Ui::WindowColour::secondary, StringIds::cargo_dist_accuracy));
        std::vector<Link> buildVisibleLinks()
        {
            std::vector<Link> result;
            if (!_overlay)
            {
                return result;
            }
            std::map<RouteKey, Link> links;
            for (const auto& [key, candidates] : state().routes)
            {
                if ((_overlayCargo != 32 && key.cargo != _overlayCargo) || (_overlayCompany != 255 && key.company != _overlayCompany))
                {
                    continue;
                }
                for (const auto& route : candidates)
                {
                    RouteKey edge{ key.company, key.cargo, key.from, route.next };
                    auto [it, added] = links.try_emplace(edge, Link{ key.company, key.cargo, key.from, route.next, route.capacity, route.distance, 0 });
                    it->second.usage += route.assigned;
                    if (links.size() >= 4096)
                    {
                        break;
                    }
                }
                if (links.size() >= 4096)
                {
                    break;
                }
            }
            for (const auto& flow : state().pendingFlows(_overlayCargo, _overlayCompany))
            {
                const auto& key = flow.key;
                if ((_overlayCargo != 32 && key.cargo != _overlayCargo) || (_overlayCompany != 255 && key.company != _overlayCompany))
                {
                    continue;
                }
                const RouteKey edge{ key.company, key.cargo, key.from, flow.next };
                if (auto it = links.find(edge); it != links.end())
                {
                    it->second.usage = static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, uint64_t(it->second.usage) + flow.quantity));
                }
            }
            for (const auto& [key, link] : links)
            {
                result.push_back(link);
            }
            return result;
        }
        Colour linkColour(const Link& link)
        {
            constexpr Colour colours[] = { Colour::grey, Colour::white, Colour::green, Colour::yellow, Colour::red };
            return colours[congestionColour(link.usage, link.capacity)];
        }
    }
    static const std::vector<Link>& visibleLinks()
    {
        static std::vector<Link> cache;
        static uint64_t previousGeneration = 0;
        static uint32_t previousTick = UINT32_MAX;
        static uint8_t previousCargo = 0, previousCompany = 0;
        static bool previousEnabled = false;
        const auto tick = ScenarioManager::getScenarioTicks();
        if (previousGeneration != generation() || previousEnabled != _overlay || previousCargo != _overlayCargo || previousCompany != _overlayCompany
            || static_cast<uint32_t>(tick - previousTick) >= 32)
        {
            cache = buildVisibleLinks();
            previousGeneration = generation();
            previousTick = tick;
            previousCargo = _overlayCargo;
            previousCompany = _overlayCompany;
            previousEnabled = _overlay;
        }
        return cache;
    }
    bool overlayEnabled() { return _overlay; }
    Ui::Window* openView(ViewKind kind, uint16_t id, const Ui::Window* origin)
    {
        const auto type = kind == ViewKind::settings ? Ui::WindowType::cargoDistribution : kind == ViewKind::station ? Ui::WindowType::stationCargoDistribution
            : kind == ViewKind::vehicle                                                                              ? Ui::WindowType::vehicleCargoDistribution
                                                                                                                     : Ui::WindowType::lineCargoDistribution;
        if (auto* existing = Ui::WindowManager::bringToFront(type, id))
        {
            return existing;
        }
        const Ui::Size size{ std::min<int32_t>(620, Ui::width()), std::min<int32_t>(340, Ui::height()) };
        auto* window = Ui::WindowManager::createWindow(type, size, Ui::WindowFlags::resizable, events);
        if (!window)
        {
            return nullptr;
        }
        window->number = id;
        window->setWidgets(widgets);
        window->setSizeBounds({ std::min<int32_t>(440, Ui::width()), std::min<int32_t>(240, Ui::height()) }, { std::min<int32_t>(1000, Ui::width()), std::min<int32_t>(700, Ui::height()) });
        if (const auto* skin = ObjectManager::get<InterfaceSkinObject>())
        {
            window->setColour(Ui::WindowColour::primary, skin->windowTitlebarColour);
            window->setColour(Ui::WindowColour::secondary, skin->windowColour);
        }
        if (origin)
        {
            window->x = static_cast<int16_t>(std::clamp<int>(origin->x + origin->width, 0, std::max(0, Ui::width() - size.width)));
            window->y = static_cast<int16_t>(std::clamp<int>(origin->y, 0, std::max(0, Ui::height() - size.height)));
        }
        Session session;
        session.kind = kind;
        session.id = id;
        session.tab = kind == ViewKind::settings ? 0 : kind == ViewKind::line ? 3
                                                                              : 1;
        session.generation = generation();
        session.ownerGeneration = kind == ViewKind::vehicle ? componentGeneration(id) : kind == ViewKind::station ? stationGeneration(id)
                                                                                                                  : 0;
        _sessions[{ type, id }] = std::move(session);
        refresh(*window);
        prepareDraw(*window);
        window->initScrollWidgets();
        return window;
    }
    void drawWorldLinks(Gfx::DrawingContext& context, ZoomLevel zoom, uint8_t rotation)
    {
        for (const auto& link : visibleLinks())
        {
            if (!state().stations.contains(link.from) || !state().stations.contains(link.to))
            {
                continue;
            }
            const auto& from = state().stations.at(link.from);
            const auto& to = state().stations.at(link.to);
            const auto* source = StationManager::get(static_cast<StationId>(link.from));
            const auto* target = StationManager::get(static_cast<StationId>(link.to));
            const auto a = World::gameToScreen(World::Pos3{ from.x, from.y, source ? source->z : int16_t{ 0 } }, rotation);
            const auto b = World::gameToScreen(World::Pos3{ to.x, to.y, target ? target->z : int16_t{ 0 } }, rotation);
            context.drawLine({ static_cast<int16_t>(zoom.applyInversedTo(a.x)), static_cast<int16_t>(zoom.applyInversedTo(a.y)) }, { static_cast<int16_t>(zoom.applyInversedTo(b.x)), static_cast<int16_t>(zoom.applyInversedTo(b.y)) }, Colours::getShade(linkColour(link), 6));
        }
    }
    void drawMapLinks(Gfx::DrawingContext& context, const std::function<Ui::Point(int16_t, int16_t)>& project)
    {
        for (const auto& link : visibleLinks())
        {
            if (!state().stations.contains(link.from) || !state().stations.contains(link.to))
            {
                continue;
            }
            const auto& from = state().stations.at(link.from);
            const auto& to = state().stations.at(link.to);
            context.drawLine(project(from.x, from.y), project(to.x, to.y), Colours::getShade(linkColour(link), 6));
        }
    }
}
