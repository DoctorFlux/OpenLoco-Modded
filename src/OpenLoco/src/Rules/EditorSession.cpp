#include "Rules/EditorSession.h"
#include "Entities/EntityManager.h"
#include "Graphics/Colour.h"
#include "Graphics/Gfx.h"
#include "Graphics/ImageIds.h"
#include "Graphics/TextRenderer.h"
#include "Input.h"
#include "Lines/Line.h"
#include "Localisation/Formatting.h"
#include "Localisation/StringIds.h"
#include "Localisation/StringManager.h"
#include "Objects/CargoObject.h"
#include "Objects/ObjectManager.h"
#include "Rules/Platforms.h"
#include "Ui.h"
#include "Ui/Dropdown.h"
#include "Ui/ScrollView.h"
#include "Ui/ToolManager.h"
#include "Ui/Widget.h"
#include "Ui/Widgets/ButtonWidget.h"
#include "Ui/Widgets/CaptionWidget.h"
#include "Ui/Widgets/FrameWidget.h"
#include "Ui/Widgets/ImageButtonWidget.h"
#include "Ui/Widgets/PanelWidget.h"
#include "Ui/Widgets/ScrollViewWidget.h"
#include "Ui/WindowManager.h"
#include "Vehicles/OrderManager.h"
#include "Vehicles/VehicleHead.h"
#include "World/Station.h"
#include "World/StationManager.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <optional>

#include <map>

namespace OpenLoco::Rules
{
    using namespace Ui;
    namespace
    {
        constexpr auto checkId = EditorIds::check, compareId = EditorIds::compare, stopId = EditorIds::stop, cargoId = EditorIds::cargo, numberId = EditorIds::number, joinId = EditorIds::join, extraId = EditorIds::extra, targetId = EditorIds::target, nameId = EditorIds::name, limitId = EditorIds::limit, firstId = EditorIds::first, lastId = EditorIds::last, binId = EditorIds::bin, detailsId = EditorIds::details, actionsId = EditorIds::actions;
        void text(Gfx::DrawingContext& ctx, int16_t x, int16_t y, int16_t width, std::string value, bool white = false)
        {
            if (width <= 0)
            {
                return;
            }
            value.insert(value.begin(), static_cast<char>(white ? ControlCodes::Colour::white : ControlCodes::Colour::black));
            auto renderer = Gfx::TextRenderer(ctx);
            renderer.setCurrentFont(Gfx::Font::medium_normal);
            renderer.clipString(width, value.data());
            renderer.drawString({ x, y }, Colour::black, value.c_str());
        }
        bool contains(EditorRect r, int x, int y)
        {
            return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
        }
    }
    InlineLayout inlineLayout(int16_t left, int16_t top, int16_t right, int16_t bottom, bool expanded, bool details)
    {
        const int paletteWidth = expanded ? 88 : 0;
        const int canvasLeft = left + (expanded ? 92 : 0);
        const int width = std::max(1, right - canvasLeft);
        const int rows = width >= 360 ? 2 : 4;
        const int controlsHeight = expanded && details ? rows * 18 : 0;
        const int toolbarHeight = expanded ? 18 : 0;
        const int canvasBottom = std::max<int>(top + 24, bottom - toolbarHeight - controlsHeight);
        return { { left, top, paletteWidth, std::max(1, bottom - top) },
                 { canvasLeft, top, width, canvasBottom - top },
                 { canvasLeft, canvasBottom, width, controlsHeight },
                 { canvasLeft, bottom - toolbarHeight, width, toolbarHeight } };
    }
    Ui::Size expandedEditorSize(Ui::Size previous, Ui::Size screen, bool line)
    {
        const int maxWidth = std::max<int>(1, std::min<int>(1000, screen.width));
        const int maxHeight = std::max<int>(1, std::min<int>(700, screen.height - 28));
        return { std::min(maxWidth, std::max<int>(previous.width, line ? 640 : 560)), std::min(maxHeight, std::max<int>(previous.height, 320)) };
    }
    struct EditorSession::State
    {
        Owner selectedOwner{};
        uint16_t selectedVehicle = UINT16_MAX;
        Program program;
        Key selectedBlock = 0;
        Owner inputOwner{};
        Key inputBlock = 0;
        uint32_t inputRevision = 0;
        bool inputExtra = false;
        BlockField inputField = BlockField::none;
        bool showDetails = false;
        std::optional<EditorRect> inlineAnchor;
        WidgetId inlineDropdownId = WidgetId::none;
        int firstSelected = -1, lastSelected = -1;
        bool editingExtra = false;
        std::string status;
        std::string tooltipText;
        uint16_t messageFrames = 0;
        using Visual = ProgramVisual;
        std::vector<Visual> visuals;
        ProgramLayout canvasLayout;
        using Drag = EditorDrag;
        Drag drag = Drag::none;
        Kind draggedKind{};
        Key draggedBlock = 0;
        int dragFirst = -1, dragLast = -1, dropRow = -1;
        Key dropInto = 0;
        int16_t dragX = 0, dragY = 0;
        bool dragged = false;
        bool draggedConditional = false;
        std::vector<int> choices;
        std::vector<Key> stopChoices;
        std::vector<Platform> platformChoices;
        std::vector<std::string> choiceLabels;
        size_t choicePage = 0;
        std::vector<int> pageItems;
        EditorRect menuAnchor{};
        WidgetIndex_t menuWidget = 0;
        static constexpr const char* names[] = { "Group", "If", "Else", "Repeat", "Wait until", "Skip", "Go to", "Else if", "Break" };
        static constexpr Kind paletteKinds[] = { Kind::group, Kind::ifBlock, Kind::elseIf, Kind::otherwise, Kind::repeat, Kind::waitUntil, Kind::skip, Kind::goTo, Kind::breakLoop, Kind::goTo };
        static constexpr const char* checkNames[] = { "Cargo waiting", "This vehicle is full", "This vehicle is empty", "This vehicle has space", "Platform is free", "This vehicle is late", "This vehicle is on time", "The year", "Cargo waiting for a destination", "Cargo onboard for a destination", "Vehicle load (%)", "Vehicle age (years)", "Vehicle reliability (%)", "Maximum speed (mph)", "Always" };
        int32_t layoutWidth = 200;

        WidgetIndex_t canvas = 0, palette = 0, check = 0, compare = 0, stop = 0, cargo = 0, number = 0, join = 0, extra = 0, target = 0, name = 0, limit = 0, firstRow = 0, lastRow = 0, bin = 0, details = 0, actions = 0;
        uint8_t canvasScroll = 0, paletteScroll = 1;
        bool expanded = false, writable = false;
        Size previousSize{};
        InlineLayout geometry{};
        uint32_t dragRevision = 0;
        Owner dragOwner{};
        WidgetId menuField = WidgetId::none;
        std::string orderLabel(size_t rowIndex) const
        {
            const auto& row = program.rows[rowIndex];
            const auto* order = reinterpret_cast<const Vehicles::Order*>(&row.raw);
            char buffer[256]{};
            if (const auto* station = order->as<Vehicles::OrderStation>())
            {
                const auto* object = StationManager::get(station->getStation());
                if (object && !object->empty())
                {
                    FormatArguments args;
                    station->setFormatArguments(args);
                    StringManager::formatString(buffer, sizeof(buffer), order->getType() == Vehicles::OrderType::StopAt ? StringIds::orders_stop_at : StringIds::orders_route_through, args);
                }
                return buffer[0] ? std::string(buffer, StringManager::locoStrlen(buffer)) : "Missing station";
            }
            else if (order->getType() == Vehicles::OrderType::RouteWaypoint)
            {
                StringManager::formatString(buffer, sizeof(buffer), StringIds::orders_route_thought_waypoint, {});
            }
            else
            {
                const auto cargoType = static_cast<const Vehicles::OrderCargo*>(order)->getCargo();
                const auto* object = ObjectManager::get<CargoObject>(cargoType);
                if (!object)
                {
                    return "Missing cargo";
                }
                FormatArguments args;
                static_cast<const Vehicles::OrderCargo*>(order)->setFormatArguments(args);
                StringManager::formatString(buffer, sizeof(buffer), order->getType() == Vehicles::OrderType::UnloadAll ? StringIds::orders_unload_all : StringIds::orders_wait_for_full_load_of, args);
            }
            return std::string(buffer, StringManager::locoStrlen(buffer));
        }
        const Block* selected() const
        {
            auto it = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.id == selectedBlock; });
            return it == program.blocks.end() ? nullptr : &*it;
        }
        void refresh()
        {
            canvasLayout = programLayout(program, static_cast<int16_t>(layoutWidth), 14);
            visuals = canvasLayout.items;
            if (firstSelected >= static_cast<int>(program.rows.size()))
            {
                firstSelected = lastSelected = -1;
            }
            if (selectedBlock && !selected())
            {
                selectedBlock = 0;
            }
        }
        void replaceProgram(const Program& next, bool preserveSelection)
        {
            const auto first = firstSelected >= 0 && firstSelected < static_cast<int>(program.rows.size()) ? program.rows[firstSelected].key : 0;
            const auto last = lastSelected >= 0 && lastSelected < static_cast<int>(program.rows.size()) ? program.rows[lastSelected].key : 0;
            program = next;
            firstSelected = lastSelected = -1;
            if (preserveSelection && first && last)
            {
                const auto a = indexOf(program, first), b = indexOf(program, last);
                if (a < program.rows.size() && b < program.rows.size())
                {
                    firstSelected = static_cast<int>(std::min(a, b));
                    lastSelected = static_cast<int>(std::max(a, b));
                }
            }
            refresh();
        }
        void cancel()
        {
            drag = Drag::none;
            dragged = false;
            dropRow = -1;
            dropInto = 0;
            inputField = BlockField::none;
            inputBlock = 0;
            inlineDropdownId = WidgetId::none;
            inlineAnchor.reset();
        }
        void cancelMenu(Window& window)
        {
            if (inputField != BlockField::none)
            {
                Ui::Windows::TextInput::sub_4CE6C9(window.type, window.number);
            }
            if (Input::isDropdownActive(window.type, window.number))
            {
                WindowManager::close(WindowType::dropdown, 0);
                Input::resetFlag(Input::Flags::widgetPressed);
                Input::state(Input::State::normal);
            }
        }
        void apply(Window& window, Edit command)
        {
            if (!writable)
            {
                return;
            }
            const auto offset = window.scrollAreas[canvasScroll].contentOffsetY;
            const auto result = submit(selectedOwner, command);
            status = result ? "Rules updated" : result.message;
            messageFrames = 80;
            replaceProgram(snapshot(selectedOwner), true);
            cancel();
            window.updateScrollWidgets();
            auto& scroll = window.scrollAreas[canvasScroll];
            scroll.contentOffsetY = static_cast<int16_t>(std::clamp<int>(offset, 0, std::max<int>(0, canvasLayout.height + 14 - window.widgets[canvas].height())));
            ScrollView::updateThumbs(window, canvas);
            window.invalidate();
        }
        void editInlineField(Window& window, const Visual& visual, const FieldVisual& field)
        {
            if (!writable)
            {
                return;
            }
            selectedBlock = visual.block;
            editingExtra = field.extra;
            if (field.field == BlockField::value || field.field == BlockField::repeat || field.field == BlockField::timeout || field.field == BlockField::name)
            {
                beginTextInput(window, canvas, field.field, field.extra);
                return;
            }
            WidgetId id = WidgetId::none;
            switch (field.field)
            {
                case BlockField::check: id = checkId; break;
                case BlockField::compare: id = compareId; break;
                case BlockField::stop: id = stopId; break;
                case BlockField::cargo: id = cargoId; break;
                case BlockField::join: id = joinId; break;
                case BlockField::when: id = nameId; break;
                case BlockField::target: id = targetId; break;
                default: return;
            }
            const auto& w = window.widgets[canvas];
            const auto x = 3 + visualIndent(visual, static_cast<int16_t>(layoutWidth)) + field.left;
            inlineAnchor = EditorRect{ window.x + w.left + 1 + x, window.y + w.top + 1 + field.top - window.scrollAreas[canvasScroll].contentOffsetY, field.width, field.height };
            onMouseDown(window, canvas, id);
            inlineAnchor.reset();
        }
        const Visual* at(int y) const { return programHit(canvasLayout, static_cast<int16_t>(y)); }
        std::optional<Edit> routeDrop(Drag previous, int canvasY) const
        {
            if (!writable || dragRevision != program.revision || dragOwner != selectedOwner)
            {
                return {};
            }
            const auto position = programDrop(program, canvasLayout, static_cast<int16_t>(canvasY));
            Edit command;
            if (previous == Drag::resize)
            {
                const auto* b = selected();
                const auto destination = programRowAt(canvasLayout, static_cast<int16_t>(canvasY));
                if (!b || destination >= program.rows.size())
                {
                    return {};
                }
                command.kind = EditKind::updateBlock;
                command.block = *b;
                command.block.last = program.rows[destination].key;
            }
            else if (previous == Drag::row || previous == Drag::section)
            {
                command.kind = EditKind::moveRows;
                command.first = dragFirst;
                command.last = dragLast;
                command.destination = position.row;
                command.block.id = draggedBlock;
                command.detach = position.outside;
                if (position.into)
                {
                    const auto it = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.id == position.into; });
                    if (it != program.blocks.end() && ((command.detach && !command.block.id) || !(command.first <= indexOf(program, it->first) && indexOf(program, it->last) <= command.last)))
                    {
                        command.into = position.into;
                    }
                }
            }
            else if (previous == Drag::palette)
            {
                const auto selection = firstSelected >= 0 ? std::optional<std::pair<size_t, size_t>>{ { firstSelected, lastSelected } } : std::nullopt;
                auto proposal = paletteDrop(program, canvasLayout, draggedKind, static_cast<int16_t>(canvasY), selection, draggedConditional);
                if (!proposal)
                {
                    return {};
                }
                command = *proposal;
            }
            else
            {
                return {};
            }
            auto preview = program;
            return edit(preview, command) ? std::optional<Edit>{ command } : std::nullopt;
        }
        void showMenuPage(Window& window)
        {
            pageItems = menuPageItems(choiceLabels.size(), choicePage);
            for (size_t i = 0; i < pageItems.size(); ++i)
            {
                const auto entry = pageItems[i];
                const char* label = entry == -1 ? "Previous page" : entry == -2 ? "Next page"
                                                                                : choiceLabels[entry].c_str();
                Dropdown::add(i, StringIds::line_name_raw, { label });
            }
            Input::setPressedWindowType(window.type);
            Input::setPressedWindowNumber(window.number);
            Input::setPressedWidgetIndex(menuWidget);
            Dropdown::showText(static_cast<int16_t>(menuAnchor.x), static_cast<int16_t>(menuAnchor.y), static_cast<int16_t>(menuAnchor.width), static_cast<int16_t>(menuAnchor.height), window.getColour(WindowColour::secondary), pageItems.size(), 0);
        }
        void menu(Window& window, WidgetIndex_t widget, const std::vector<std::string>& labels)
        {
            choiceLabels = labels;
            choicePage = 0;
            inputOwner = selectedOwner;
            inputBlock = selectedBlock;
            inputRevision = program.revision;
            inputExtra = editingExtra;
            inputField = BlockField::none;
            inlineDropdownId = menuField;
            const auto& w = window.widgets[widget];
            menuAnchor = inlineAnchor.value_or(EditorRect{ window.x + w.left, window.y + w.top, w.width(), w.height() });
            menuWidget = canvas;
            showMenuPage(window);
        }
        void onMouseDown(Window& window, WidgetIndex_t widget, WidgetId id)
        {
            menuField = id;
            const auto* b = selected();
            if (!b)
            {
                return;
            }
            if (id == actionsId)
            {
                menu(window, widget, { "Add Else if", "Add Else", "Remove block" });
                if (b->kind != Kind::ifBlock && b->kind != Kind::elseIf)
                {
                    Dropdown::setItemDisabled(0);
                    Dropdown::setItemDisabled(1);
                }
                return;
            }
            const auto& c = editingExtra ? b->extra : b->condition;
            choices.clear();
            stopChoices.clear();
            platformChoices.clear();
            std::vector<std::string> labels;
            if (id == nameId)
            {
                if (b->kind != Kind::waitUntil)
                {
                    return;
                }
                labels = { "Automatic: on arrival", "Before travelling", "On arrival" };
            }
            else if (id == numberId && b->kind == Kind::repeat)
            {
                labels.push_back("Forever");
                for (int i = 1; i <= 20; ++i)
                {
                    labels.push_back(std::to_string(i) + " times");
                }
            }
            else if (id == targetId && b->kind == Kind::breakLoop)
            {
                for (const auto& loop : program.blocks)
                {
                    if (loop.kind != Kind::repeat || indexOf(program, loop.first) > indexOf(program, b->first) || indexOf(program, b->last) > indexOf(program, loop.last))
                    {
                        continue;
                    }
                    stopChoices.push_back(loop.id);
                    labels.push_back("Repeat loop at row " + std::to_string(indexOf(program, loop.first) + 1));
                }
            }
            else if (id == checkId)
            {
                choices.push_back(256);
                labels.push_back("This stop has cargo");
                choices.push_back(257);
                labels.push_back("This stop has no cargo");
                for (unsigned i = 0; i <= unsigned(Check::always); ++i)
                {
                    if (i == unsigned(Check::platformFree) && !program.rail)
                    {
                        continue;
                    }
                    choices.push_back(i);
                    labels.push_back(checkNames[i]);
                }
            }
            else if (id == firstId || id == lastId)
            {
                for (size_t row = 0; row < program.rows.size(); ++row)
                {
                    stopChoices.push_back(program.rows[row].key);
                    labels.push_back("Row " + std::to_string(row + 1) + ((program.rows[row].raw & 7) == 3 ? ": waypoint" : (program.rows[row].raw & 7) < 3 ? ": stop"
                                                                                                                                                           : ": cargo instruction"));
                }
            }
            else if (id == compareId)
            {
                if (numericCheck(c.check))
                {
                    labels = { "Less than", "The same as", "More than", "Not the same as", "Less than or equal", "More than or equal" };
                }
                else
                {
                    choices = { int(Compare::isTrue), int(Compare::isFalse) };
                    labels = { "Is true", "Is false" };
                }
            }
            else if (id == joinId)
            {
                labels = { "One check", "And another check", "Or another check" };
            }
            else if (id == stopId || id == targetId)
            {
                if (id == targetId)
                {
                    for (const auto& group : program.blocks)
                    {
                        if (group.kind != Kind::group)
                        {
                            continue;
                        }
                        for (size_t i = indexOf(program, group.first); i <= indexOf(program, group.last); ++i)
                        {
                            if (!isRoutable(program.rows[i].raw))
                            {
                                continue;
                            }
                            stopChoices.push_back(program.rows[i].key);
                            labels.push_back("Group: " + group.name);
                            break;
                        }
                    }
                }
                for (const auto& row : program.rows)
                {
                    if (id == stopId && (row.raw & 7) != 1 && (row.raw & 7) != 2)
                    {
                        continue;
                    }
                    if (id == targetId && !isRoutable(row.raw))
                    {
                        continue;
                    }
                    size_t ordinal = 0;
                    for (const auto& numbered : program.rows)
                    {
                        if (isRoutable(numbered.raw))
                        {
                            ++ordinal;
                        }
                        if (numbered.key == row.key)
                        {
                            break;
                        }
                    }
                    stopChoices.push_back(row.key);
                    std::string label = "Stop / waypoint " + std::to_string(ordinal);
                    const auto* order = reinterpret_cast<const Vehicles::Order*>(&row.raw);
                    if (const auto* station = order->as<Vehicles::OrderStation>(); station && StationManager::get(station->getStation()) && !StationManager::get(station->getStation())->empty())
                    {
                        char stationName[128]{};
                        FormatArguments args;
                        station->setFormatArguments(args);
                        StringManager::formatString(stationName, sizeof(stationName), StringIds::orders_stop_at, args);
                        label += ": ";
                        label += stationName;
                    }
                    else if ((row.raw & 7) == 3)
                    {
                        const auto waypoint = Lines::fromVehicleOrder(*order);
                        label = "Waypoint " + std::to_string(ordinal) + " (" + std::to_string(waypoint.tileX) + ", " + std::to_string(waypoint.tileY) + ")";
                    }
                    labels.push_back(std::move(label));
                }
            }
            else if (id == cargoId)
            {
                if (c.check == Check::platformFree)
                {
                    const auto index = indexOf(program, c.stop);
                    if (index < program.rows.size())
                    {
                        const auto* order = reinterpret_cast<const Vehicles::Order*>(&program.rows[index].raw)->as<Vehicles::OrderStation>();
                        if (order)
                        {
                            platformChoices = railPlatforms(enumValue(order->getStation()));
                        }
                    }
                    for (size_t i = 0; i < platformChoices.size(); ++i)
                    {
                        labels.push_back("Platform " + std::to_string(i + 1));
                    }
                }
                else
                {
                    for (uint8_t i = 0; i < 32; ++i)
                    {
                        if (const auto* object = ObjectManager::get<CargoObject>(i))
                        {
                            char buffer[128]{};
                            StringManager::formatString(buffer, sizeof(buffer), object->name);
                            choices.push_back(i);
                            labels.emplace_back(buffer);
                        }
                    }
                }
            }
            if (!labels.empty())
            {
                menu(window, widget, labels);
            }
            else
            {
                status = "No compatible choices are available for this field";
                messageFrames = 80;
                window.invalidate();
            }
        }
        void dropdown(Window& window, WidgetIndex_t widgetIndex, WidgetId id, int16_t index)
        {
            if (widgetIndex == canvas)
            {
                id = inlineDropdownId;
            }
            const auto* selectedValue = selected();
            if (!writable || !selectedValue || inputBlock != selectedBlock || !fieldEditStillValid(inputOwner, selectedOwner, program, inputBlock, inputRevision) || !fieldEditStillValid(inputOwner, selectedOwner, snapshot(selectedOwner), inputBlock, inputRevision) || inputField != BlockField::none || index < 0 || static_cast<size_t>(index) >= pageItems.size())
            {
                return;
            }
            index = static_cast<int16_t>(pageItems[index]);
            if (index < 0)
            {
                choicePage = index == -1 ? choicePage - 1 : choicePage + 1;
                showMenuPage(window);
                return;
            }
            if (id == actionsId)
            {
                if (index == 2)
                {
                    Edit e;
                    e.kind = EditKind::removeBlock;
                    e.block.id = selectedBlock;
                    apply(window, e);
                }
                else
                {
                    const auto command = blockDrop(program, index == 0 ? Kind::elseIf : Kind::otherwise, indexOf(program, selectedValue->first), indexOf(program, selectedValue->last), selectedBlock);
                    if (command)
                    {
                        const auto key = program.nextKey;
                        apply(window, *command);
                        if (program.nextKey > key)
                        {
                            selectedBlock = key;
                        }
                    }
                    else
                    {
                        status = "Add a following order section for this branch";
                        messageFrames = 80;
                        window.invalidate();
                    }
                }
                return;
            }
            auto b = *selectedValue;
            auto& c = inputExtra ? b.extra : b.condition;
            if (id == nameId)
            {
                if (b.kind != Kind::waitUntil)
                {
                    return;
                }
                b.when = static_cast<Timing>(index);
            }
            else if (id == numberId && b.kind == Kind::repeat)
            {
                b.number = static_cast<uint16_t>(index);
            }
            else if (id == checkId)
            {
                const auto previousCheck = c.check;
                const auto choice = choices[index];
                c.check = choice >= 256 ? Check::cargoWaiting : static_cast<Check>(choice);
                if (choice >= 256)
                {
                    c.compare = choice == 256 ? Compare::greater : Compare::equal;
                    c.value = 0;
                }
                if (c.check == Check::cargoWaiting || c.check == Check::cargoWaitingFor || c.check == Check::cargoOnboardFor || c.check == Check::platformFree || c.check == Check::late || c.check == Check::onTime)
                {
                    const auto previousStop = indexOf(program, c.stop);
                    if (previousStop >= program.rows.size() || ((program.rows[previousStop].raw & 7) != 1 && (program.rows[previousStop].raw & 7) != 2))
                    {
                        c.stop = 0;
                        for (const auto& row : program.rows)
                        {
                            if ((row.raw & 7) == 1 || (row.raw & 7) == 2)
                            {
                                c.stop = row.key;
                                break;
                            }
                        }
                    }
                    if (c.check == Check::platformFree && c.stop)
                    {
                        const auto raw = program.rows[indexOf(program, c.stop)].raw;
                        const auto* station = reinterpret_cast<const Vehicles::Order*>(&raw)->as<Vehicles::OrderStation>();
                        const auto platforms = railPlatforms(enumValue(station->getStation()));
                        if (!platforms.empty())
                        {
                            c.platform = 1;
                            c.platformAnchor = platforms[0].anchor;
                        }
                    }
                }
                if (c.check == Check::year && previousCheck != Check::year)
                {
                    c.value = 2000;
                }
                if ((c.check == Check::loadPercentage || c.check == Check::reliability) && c.value > 100)
                {
                    c.value = 50;
                }
                if (numericCheck(c.check) && (c.compare == Compare::isTrue || c.compare == Compare::isFalse))
                {
                    c.compare = Compare::equal;
                }
                if (!numericCheck(c.check) && c.compare != Compare::isFalse)
                {
                    c.compare = Compare::isTrue;
                }
            }
            else if (id == firstId)
            {
                b.first = stopChoices[index];
            }
            else if (id == lastId)
            {
                b.last = stopChoices[index];
            }
            else if (id == compareId)
            {
                c.compare = static_cast<Compare>(numericCheck(c.check) ? index : choices[index]);
            }
            else if (id == joinId)
            {
                b.join = static_cast<Join>(index);
                if (b.join != Join::none && selectedValue->join == Join::none)
                {
                    b.extra = {};
                }
            }
            else if (id == stopId)
            {
                c.stop = stopChoices[index];
                b.enabled = true;
                c.platformAnchor = 0;
                c.platform = 0;
            }
            else if (id == targetId)
            {
                b.target = stopChoices[index];
                b.enabled = true;
            }
            else if (id == cargoId)
            {
                if (c.check == Check::platformFree)
                {
                    c.platform = index + 1;
                    c.platformAnchor = platformChoices[index].anchor;
                }
                else
                {
                    c.cargo = static_cast<uint8_t>(choices[index]);
                }
            }
            Edit e;
            e.kind = EditKind::updateBlock;
            e.block = b;
            apply(window, e);
        }
        void beginTextInput(Window& window, [[maybe_unused]] WidgetIndex_t widget, BlockField field, bool extraField)
        {
            const auto* b = selected();
            if (!b)
            {
                return;
            }
            const auto value = fieldTextValue(program, selectedBlock, field, extraField);
            inputOwner = selectedOwner;
            inputBlock = selectedBlock;
            inputRevision = program.revision;
            inputExtra = extraField;
            inputField = field;
            Input::state(Input::State::normal);
            Ui::Windows::TextInput::openTextInput(&window, StringIds::rules_title, field == BlockField::name ? StringIds::rules_name_prompt : StringIds::rules_number_prompt, StringIds::line_name_raw, canvas, FormatArguments::common(value.c_str()), field == BlockField::name ? 80 : 9);
        }
    };

    EditorSession::EditorSession()
        : state(std::make_unique<State>())
    {
    }
    EditorSession::~EditorSession() = default;
    bool EditorSession::expanded() const { return state->expanded; }
    bool EditorSession::showingProgram() const { return state->expanded || !state->program.blocks.empty(); }
    const Program& EditorSession::program() const { return state->program; }
    const ProgramLayout& EditorSession::layout() const { return state->canvasLayout; }
    Key EditorSession::selectedBlock() const { return state->selectedBlock; }
    std::optional<std::pair<size_t, size_t>> EditorSession::selection() const
    {
        return state->firstSelected >= 0 ? std::optional<std::pair<size_t, size_t>>{ { state->firstSelected, state->lastSelected } } : std::nullopt;
    }
    void EditorSession::selectRows(size_t first, size_t last)
    {
        if (first > last || last >= state->program.rows.size())
        {
            return;
        }
        state->firstSelected = static_cast<int>(first);
        state->lastSelected = static_cast<int>(last);
    }
    void EditorSession::selectBlock(Key key)
    {
        state->selectedBlock = key;
        if (!state->selected())
        {
            state->selectedBlock = 0;
        }
    }
    void EditorSession::bind(Owner owner, const Program& p, uint16_t vehicle, bool writable)
    {
        auto& s = *state;
        const bool ownerChanged = owner != s.selectedOwner;
        if (ownerChanged || p != s.program || writable != s.writable)
        {
            s.cancel();
            if (ownerChanged)
            {
                s.selectedBlock = 0;
                s.firstSelected = s.lastSelected = -1;
                s.showDetails = false;
                s.status.clear();
            }
            s.selectedOwner = owner;
            s.replaceProgram(p, !ownerChanged);
        }
        s.selectedVehicle = vehicle;
        s.writable = writable;
    }
    void EditorSession::configure(Window& window, Owner owner, uint16_t vehicle, WidgetIndex_t canvas, uint8_t canvasScroll, WidgetIndex_t firstWidget, uint8_t paletteScroll, bool writable)
    {
        auto& s = *state;
        const auto p = snapshot(owner);
        if (p != s.program || owner != s.selectedOwner || writable != s.writable)
        {
            s.cancelMenu(window);
        }
        bind(owner, p, vehicle, writable);
        s.canvas = canvas;
        s.canvasScroll = canvasScroll;
        s.paletteScroll = paletteScroll;
        s.palette = firstWidget;
        s.check = firstWidget + 1;
        s.compare = firstWidget + 2;
        s.stop = firstWidget + 3;
        s.cargo = firstWidget + 4;
        s.number = firstWidget + 5;
        s.join = firstWidget + 6;
        s.extra = firstWidget + 7;
        s.target = firstWidget + 8;
        s.name = firstWidget + 9;
        s.limit = firstWidget + 10;
        s.firstRow = firstWidget + 11;
        s.lastRow = firstWidget + 12;
        s.bin = firstWidget + 13;
        s.details = firstWidget + 14;
        s.actions = firstWidget + 15;
        if (s.messageFrames && --s.messageFrames == 0)
        {
            s.status.clear();
        }
    }
    void EditorSession::prepare(Window& window, int16_t left, int16_t top, int16_t right, int16_t bottom)
    {
        auto& s = *state;
        s.geometry = inlineLayout(left, top, right, bottom, s.expanded, s.showDetails);
        const auto canvasChanged = window.widgets[s.canvas].width() != s.geometry.canvas.width || window.widgets[s.canvas].height() != s.geometry.canvas.height;
        const auto paletteChanged = window.widgets[s.palette].width() != s.geometry.palette.width || window.widgets[s.palette].height() != s.geometry.palette.height;
        auto rect = [&](WidgetIndex_t index, EditorRect r) {
            auto& w = window.widgets[index];
            if ((index == s.canvas || index == s.palette) && (w.width() != r.width || w.height() != r.height))
            {
                // Force native updateScrollWidgets to recalculate thumbs even when content is unchanged.
                window.scrollAreas[index == s.canvas ? s.canvasScroll : s.paletteScroll].contentHeight = -1;
            }
            w.left = r.x;
            w.top = r.y;
            w.right = r.x + r.width - 1;
            w.bottom = r.y + r.height - 1;
        };
        rect(s.canvas, s.geometry.canvas);
        rect(s.palette, s.geometry.palette);
        window.widgets[s.palette].hidden = !s.expanded;
        if (s.layoutWidth != s.geometry.canvas.width - 14)
        {
            s.layoutWidth = std::max<int>(32, s.geometry.canvas.width - 14);
            s.refresh();
        }
        if (s.expanded)
        {
            // Expanded editing is entered only after the host has initialized its scroll areas.
            for (const auto scroll : { s.canvasScroll, s.paletteScroll })
            {
                if ((scroll == s.canvasScroll && canvasChanged) || (scroll == s.paletteScroll && paletteChanged))
                {
                    int32_t contentWidth = 0, contentHeight = 0;
                    scrollSize(scroll, contentWidth, contentHeight);
                    const auto& view = window.widgets[scroll == s.canvasScroll ? s.canvas : s.palette];
                    auto& area = window.scrollAreas[scroll];
                    area.contentOffsetY = static_cast<int16_t>(std::clamp<int>(area.contentOffsetY, 0, std::max<int>(0, contentHeight + 3 - view.height())));
                }
            }
        }
        const auto controlsWidth = s.geometry.controls.width;
        const auto columns = controlsWidth >= 360 ? 6 : 3;
        const int controls[] = { s.check, s.compare, s.stop, s.cargo, s.number, s.firstRow, s.join, s.extra, s.target, s.name, s.limit, s.lastRow };
        for (size_t i = 0; i < std::size(controls); ++i)
        {
            const auto index = controls[i];
            rect(index, { s.geometry.controls.x + int(i % columns) * (controlsWidth / columns), s.geometry.controls.y + int(i / columns) * 18, controlsWidth / columns - 2, 16 });
            window.widgets[index].hidden = !s.expanded || !s.showDetails;
            window.disabledWidgets |= uint64_t{ 1 } << index;
        }
        const auto* b = s.selected();
        if (b && s.writable)
        {
            const auto enable = [&](int index) { window.disabledWidgets &= ~(uint64_t{ 1 } << index); };
            if (b->kind != Kind::skip && b->kind != Kind::goTo && b->kind != Kind::breakLoop)
            {
                enable(s.firstRow);
                enable(s.lastRow);
            }
            if (b->kind == Kind::repeat)
            {
                enable(s.number);
            }
            if (b->kind == Kind::group)
            {
                enable(s.name);
            }
            if (b->kind == Kind::goTo || b->kind == Kind::breakLoop)
            {
                enable(s.target);
            }
            if (hasCondition(*b))
            {
                enable(s.check);
                enable(s.join);
                if (b->join != Join::none)
                {
                    enable(s.extra);
                }
                const auto& c = s.editingExtra ? b->extra : b->condition;
                enable(s.compare);
                if (numericCheck(c.check))
                {
                    enable(s.compare);
                    enable(s.number);
                }
                if (c.check == Check::cargoWaiting || c.check == Check::cargoWaitingFor || c.check == Check::cargoOnboardFor || c.check == Check::platformFree || c.check == Check::late || c.check == Check::onTime)
                {
                    enable(s.stop);
                }
                if (c.check == Check::cargoWaiting || c.check == Check::cargoWaitingFor || c.check == Check::cargoOnboardFor || c.check == Check::platformFree)
                {
                    enable(s.cargo);
                }
                if (b->kind == Kind::waitUntil)
                {
                    enable(s.limit);
                    enable(s.name);
                }
            }
        }
        const int toolWidth = s.geometry.toolbar.width / 3;
        for (int i = 0; i < 3; ++i)
        {
            const int index = i == 0 ? s.actions : i == 1 ? s.details
                                                          : s.bin;
            rect(index, { s.geometry.toolbar.x + i * toolWidth, s.geometry.toolbar.y, toolWidth - 2, 16 });
            window.widgets[index].hidden = !s.expanded;
            window.disabledWidgets &= ~(uint64_t{ 1 } << index);
        }
        if (!b || !s.writable)
        {
            window.disabledWidgets |= uint64_t{ 1 } << s.actions;
        }
        window.disabledWidgets &= ~(uint64_t{ 1 } << s.palette);
        if (!s.writable)
        {
            window.disabledWidgets |= (uint64_t{ 1 } << s.bin) | (uint64_t{ 1 } << s.palette);
        }
        window.activatedWidgets &= ~((uint64_t{ 1 } << s.details) | (uint64_t{ 1 } << s.extra));
        if (s.showDetails)
        {
            window.activatedWidgets |= uint64_t{ 1 } << s.details;
        }
        if (s.editingExtra)
        {
            window.activatedWidgets |= uint64_t{ 1 } << s.extra;
        }
        window.widgets[s.name].text = b && b->kind == Kind::waitUntil ? StringIds::rules_when : StringIds::rules_name;
        window.widgets[s.number].text = b && b->kind == Kind::repeat ? StringIds::rules_times : StringIds::rules_number;
        window.widgets[s.target].text = b && b->kind == Kind::breakLoop ? StringIds::rules_loop : StringIds::rules_target;
        window.widgets[s.cargo].text = b && (s.editingExtra ? b->extra.check : b->condition.check) == Check::platformFree ? StringIds::rules_platform : StringIds::rules_cargo;
    }
    void EditorSession::toggle(Window& window, Ui::Size screen)
    {
        auto& s = *state;
        s.cancelMenu(window);
        s.cancel();
        if (s.expanded)
        {
            s.expanded = false;
            s.showDetails = false;
            window.setSizeBounds({ window.type == WindowType::lineManager ? 420 : 265, window.type == WindowType::lineManager ? 268 : 202 }, { 1000, 700 });
            window.setSize(s.previousSize);
        }
        else
        {
            s.previousSize = { window.width, window.height };
            s.expanded = true;
            const bool line = window.type == WindowType::lineManager;
            if (screen.width <= 0 || screen.height <= 0)
            {
                screen = { Ui::width(), Ui::height() };
            }
            window.setSizeBounds({ std::min<int>(line ? 560 : 400, screen.width), std::min<int>(line ? 268 : 240, screen.height - 28) }, { std::min<int>(1000, screen.width), std::min<int>(700, screen.height - 28) });
            window.setSize(expandedEditorSize(s.previousSize, screen, line));
            window.x = std::clamp<int>(window.x, 0, std::max<int>(0, screen.width - window.width));
            window.y = std::clamp<int>(window.y, 28, std::max<int>(28, screen.height - window.height));
        }
        window.invalidate();
        window.callPrepareDraw();
        window.updateScrollWidgets();
    }
    void EditorSession::suspend(Window& window, bool restore)
    {
        if (state->expanded && restore)
        {
            toggle(window);
        }
        state->expanded = false;
        state->showDetails = false;
        state->cancelMenu(window);
        state->cancel();
    }
    void EditorSession::startDrag(EditorDrag kind, size_t first, size_t last, Key block, Kind paletteKind, bool conditionalJump)
    {
        auto& s = *state;
        s.drag = kind;
        s.dragFirst = static_cast<int>(first);
        s.dragLast = static_cast<int>(last);
        s.draggedBlock = block;
        s.draggedKind = paletteKind;
        s.draggedConditional = conditionalJump;
        s.dragRevision = s.program.revision;
        s.dragOwner = s.selectedOwner;
        s.dragged = false;
        s.dropRow = -1;
    }
    std::optional<Edit> EditorSession::dropProposal(int16_t y) const { return y < 0 || y > state->canvasLayout.height ? std::nullopt : state->routeDrop(state->drag, y); }
    void EditorSession::cancel() { state->cancel(); }
    bool EditorSession::dragging() const { return state->drag != EditorDrag::none; }
    bool EditorSession::scrollSize(uint32_t scroll, int32_t& width, int32_t& height) const
    {
        width = 0;
        if (scroll == state->paletteScroll)
        {
            height = state->expanded ? 2 + std::size(State::paletteKinds) * 18 : 0;
            return true;
        }
        if (scroll == state->canvasScroll && showingProgram())
        {
            height = state->canvasLayout.height + 14;
            return true;
        }
        return false;
    }

    bool EditorSession::drawScroll(Window& window, Gfx::DrawingContext& ctx, uint32_t scroll)
    {
        auto& s = *state;
        if (scroll == s.paletteScroll)
        {
            ctx.clear(Colours::getShade(window.getColour(WindowColour::secondary).c(), 4));
            if (s.expanded)
            {
                for (size_t i = 0; i < std::size(s.paletteKinds); ++i)
                {
                    drawStackBlock(ctx, 2, static_cast<int16_t>(2 + i * 18), 70, 16, s.paletteKinds[i]);
                    text(ctx, 6, static_cast<int16_t>(5 + i * 18), 62, i + 1 == std::size(s.paletteKinds) ? "Jump if" : s.names[static_cast<unsigned>(s.paletteKinds[i])], true);
                }
            }
            return true;
        }
        if (scroll != s.canvasScroll || !showingProgram())
        {
            return false;
        }
        ctx.clear(Colours::getShade(window.getColour(WindowColour::secondary).c(), 4));
        const auto width = static_cast<int16_t>(s.layoutWidth);
        drawProgramBlocks(ctx, s.program, runtimeForVehicle(s.selectedVehicle), s.canvasLayout, width, 0, s.selectedBlock);
        int ordinal = 0;
        auto renderer = Gfx::TextRenderer(ctx);
        renderer.setCurrentFont(Gfx::Font::medium_bold);
        for (const auto& v : s.visuals)
        {
            const auto x = static_cast<int16_t>(4 + visualIndent(v, width));
            if (v.block)
            {
                if (v.closing && v.block == s.selectedBlock)
                {
                    text(ctx, width - 12, v.top - 1, 12, "=");
                }
                if (s.dragged && s.dropRow == int(v.row + (v.closing ? 1 : 0)))
                {
                    const auto position = programDrop(s.program, s.canvasLayout, static_cast<int16_t>(v.top + (v.closing ? v.height - 1 : 0)));
                    if (s.dropInto == position.into || (v.closing && s.dropInto == v.block))
                    {
                        const auto lineY = static_cast<int16_t>(v.top + (v.closing ? (s.dropInto == v.block ? 2 : v.height - 1) : 0));
                        const auto lineX = s.dropInto == v.block ? x + 8 : std::max<int>(4, x - 8);
                        ctx.fillRect(lineX, lineY, width - 3, lineY + 1, Colours::getShade(Colour::yellow, 9), Gfx::RectFlags::none);
                    }
                }
                continue;
            }
            const bool selectedRow = s.firstSelected >= 0 && int(v.row) >= s.firstSelected && int(v.row) <= s.lastSelected;
            if (selectedRow)
            {
                ctx.fillRect(x, v.top, width, v.top + v.height - 1, PaletteIndex::black0, Gfx::RectFlags::none);
            }
            text(ctx, x + 4, v.top + 2, 12, "=", selectedRow);
            auto label = s.orderLabel(v.row);
            label.insert(label.begin(), static_cast<char>(selectedRow ? ControlCodes::Colour::white : ControlCodes::Colour::black));
            renderer.setCurrentFont(Gfx::Font::medium_bold);
            renderer.clipString(std::max<int>(1, width - x - 44), label.data());
            renderer.drawString({ static_cast<int16_t>(x + 20), static_cast<int16_t>(v.top + 2) }, Colour::black, label.c_str());
            const auto labelRight = static_cast<int16_t>(x + 20 + renderer.getStringWidth(label.c_str()));
            if (isRoutable(s.program.rows[v.row].raw))
            {
                ++ordinal;
                if (ordinal <= 63 && (window.type == WindowType::lineManager || ToolManager::isToolActive(window.type, window.number)))
                {
                    ctx.drawImage(ZoomLevel::full, labelRight + 3, v.top + 3, Gfx::recolour(ImageIds::number_circle_01 + static_cast<uint32_t>(ordinal - 1), Colour::white));
                }
            }
            if (s.selectedOwner.kind == OwnerKind::line)
            {
                const auto* line = Lines::findLine(s.selectedOwner.id);
                if (line && v.row < line->orders.size() && !line->orders[v.row].slots.empty())
                {
                    const auto departure = line->orders[v.row].slots.back().departureMinute;
                    auto args = FormatArguments::common(static_cast<uint16_t>(departure / 60), static_cast<uint16_t>(departure % 60));
                    renderer.drawStringLeft({ static_cast<int16_t>(labelRight + 16), static_cast<int16_t>(v.top + 2) }, Colour::black, StringIds::line_slot_note, args);
                }
            }
            if (s.dragged && s.dropRow == int(v.row))
            {
                ctx.fillRect(x, v.top, width, v.top + 1, Colours::getShade(Colour::yellow, 9), Gfx::RectFlags::none);
            }
        }
        drawRouteOrderMarker(ctx, s.program, s.canvasLayout, routeOrderForVehicle(s.selectedVehicle, &s.program), width);
        text(ctx, 4, s.canvasLayout.height, width - 8, "-- End of route list --");
        return true;
    }
    void EditorSession::draw(Window& window, Gfx::DrawingContext& ctx)
    {
        auto& s = *state;
        if (!showingProgram())
        {
            return;
        }
        const int left = window.type == WindowType::lineManager ? 194 : 4;
        const int footerTop = window.type == WindowType::lineManager ? window.height - 58 : window.height - 28;
        std::string status = s.selectedVehicle != UINT16_MAX ? vehicleStatus(s.selectedVehicle) : "Rules: no assigned vehicle";
        if (window.type == WindowType::lineManager && s.selectedVehicle != UINT16_MAX)
        {
            const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(s.selectedVehicle));
            if (head)
            {
                status = "Vehicle " + std::to_string(head->ordinalNumber) + ": " + status;
            }
        }
        if (window.type == WindowType::lineManager)
        {
            const auto preview = Lines::routePreviewStatus(s.selectedOwner.id);
            if (!preview.empty())
            {
                status = preview + " | " + status;
            }
        }
        if (!s.status.empty())
        {
            status = s.status + " | " + status;
        }
        text(ctx, left, footerTop, window.width - left - 5, status);
        text(ctx, left, footerTop + 12, window.width - left - 5, vehicleNextStop(s.selectedVehicle));
        if (s.expanded && s.selectedOwner.kind == OwnerKind::line)
        {
            const auto* line = Lines::findLine(s.selectedOwner.id);
            if (line)
            {
                text(ctx, left, window.widgets[s.canvas].top - 13, window.width - left - 29, "Editing shared line: " + line->name);
            }
        }
        if (s.dragged && s.drag != EditorDrag::none && s.drag != EditorDrag::selection)
        {
            const auto mouse = Input::getMouseLocation();
            const int width = std::min<int>(260, window.width - 8);
            const auto x = static_cast<int16_t>(std::clamp<int>(mouse.x - window.x + 12, 4, window.width - width - 4));
            const auto y = static_cast<int16_t>(std::clamp<int>(mouse.y - window.y + 16, 16, window.height - 25));
            std::string label = s.drag == EditorDrag::palette ? (s.draggedConditional ? "Jump if" : s.names[static_cast<unsigned>(s.draggedKind)]) : s.draggedBlock && s.selected() ? blockSentence(s.program, *s.selected())
                                                                                                                                                                                    : (s.dragFirst == s.dragLast && s.dragFirst >= 0 && s.dragFirst < static_cast<int>(s.program.rows.size()) ? s.orderLabel(s.dragFirst) : "Move " + std::to_string(s.dragLast - s.dragFirst + 1) + " order rows");
            drawStackBlock(ctx, x, y, width, 20, s.drag == EditorDrag::palette ? s.draggedKind : Kind::group, Feedback::idle, true);
            text(ctx, x + 5, y + 4, width - 10, label, true);
        }
    }
    bool EditorSession::mouseDown(Window& window, int16_t x, int16_t y, uint8_t scroll)
    {
        auto& s = *state;
        if (scroll == s.paletteScroll && s.expanded)
        {
            const int item = (y - 2) / 18;
            if (s.writable && y >= 2 && item >= 0 && item < int(std::size(s.paletteKinds)))
            {
                startDrag(EditorDrag::palette, 0, 0, 0, s.paletteKinds[item], item + 1 == int(std::size(s.paletteKinds)));
            }
            s.dragX = x;
            s.dragY = y;
            return true;
        }
        if (scroll != s.canvasScroll || !showingProgram())
        {
            return false;
        }
        const auto* v = s.at(y);
        if (!v)
        {
            s.selectedBlock = 0;
            return true;
        }
        if (!s.writable)
        {
            return true;
        }
        s.status.clear();
        s.dragX = x;
        s.dragY = y;
        if (v->block)
        {
            s.selectedBlock = v->block;
            const auto* selected = s.selected();
            if (selected)
            {
                selectRows(indexOf(s.program, selected->first), indexOf(s.program, selected->last));
            }
            if (v->closing && x >= s.layoutWidth - 16)
            {
                const auto* b = s.selected();
                if (b)
                {
                    startDrag(EditorDrag::resize, indexOf(s.program, b->first), indexOf(s.program, b->last), b->id);
                }
            }
            else if (v->closing)
            {
                // The thin strip selects the section; its right-hand grip resizes it.
            }
            else if (const auto* field = blockFieldHit(s.canvasLayout, x, y, static_cast<int16_t>(s.layoutWidth)))
            {
                s.editInlineField(window, *v, *field);
            }
            else
            {
                const auto* b = s.selected();
                startDrag(EditorDrag::section, indexOf(s.program, b->first), indexOf(s.program, b->last), b->id);
            }
        }
        else if (!Input::hasKeyModifier(Input::KeyModifier::shift))
        {
            const bool hadBlockSelection = s.selectedBlock != 0;
            s.selectedBlock = 0;
            size_t first = v->row, last = v->row;
            if (!hadBlockSelection && s.firstSelected >= 0 && int(v->row) >= s.firstSelected && int(v->row) <= s.lastSelected)
            {
                first = s.firstSelected;
                last = s.lastSelected;
            }
            else
            {
                selectRows(first, last);
            }
            startDrag(EditorDrag::row, first, last);
        }
        else
        {
            s.selectedBlock = 0;
            selectRows(v->row, v->row);
            startDrag(EditorDrag::selection, v->row, v->row);
        }
        window.invalidate();
        return true;
    }
    bool EditorSession::mouseDrag(Window& window, int16_t x, int16_t y, uint8_t scroll)
    {
        auto& s = *state;
        if (s.drag == EditorDrag::none)
        {
            return false;
        }
        s.dragged |= std::abs(x - s.dragX) + std::abs(y - s.dragY) > 4;
        const auto& pressed = window.widgets[scroll == s.paletteScroll ? s.palette : s.canvas];
        const int screenX = window.x + pressed.left + 1 + x;
        const int screenY = window.y + pressed.top + 1 + y - window.scrollAreas[scroll].contentOffsetY;
        const auto& canvas = window.widgets[s.canvas];
        const int localX = screenX - window.x - canvas.left - 1;
        int localY = screenY - window.y - canvas.top - 1 + window.scrollAreas[s.canvasScroll].contentOffsetY;
        const bool inside = localX >= 0 && localX < s.layoutWidth && screenY >= window.y + canvas.top && screenY <= window.y + canvas.bottom;
        if (s.dragged && inside)
        {
            auto& area = window.scrollAreas[s.canvasScroll];
            const int visibleY = screenY - window.y - canvas.top - 1;
            const int delta = visibleY < 12 ? -6 : visibleY > canvas.height() - 15 ? 6
                                                                                   : 0;
            area.contentOffsetY = static_cast<int16_t>(std::clamp<int>(area.contentOffsetY + delta, 0, std::max<int>(0, s.canvasLayout.height + 14 - canvas.height() + 3)));
            ScrollView::updateThumbs(window, s.canvas);
            localY = visibleY + area.contentOffsetY;
        }
        const auto position = programDrop(s.program, s.canvasLayout, static_cast<int16_t>(localY));
        s.dropRow = inside ? static_cast<int>(position.row) : -1;
        s.dropInto = position.into;
        if (s.drag == EditorDrag::selection && inside && !s.program.rows.empty())
        {
            const auto row = std::min<size_t>(programRowAt(s.canvasLayout, localY), s.program.rows.size() - 1);
            selectRows(std::min<size_t>(row, s.dragFirst), std::max<size_t>(row, s.dragFirst));
        }
        else if (!inside || !dropProposal(static_cast<int16_t>(std::min<int>(localY, s.canvasLayout.height))))
        {
            s.dropRow = -1;
        }
        window.invalidate();
        return true;
    }
    bool EditorSession::mouseRelease(Window& window, int16_t screenX, int16_t screenY, uint8_t)
    {
        auto& s = *state;
        if (s.drag == EditorDrag::none)
        {
            return false;
        }
        const auto drag = s.drag;
        const bool moved = s.dragged;
        s.drag = EditorDrag::none;
        s.dragged = false;
        s.dropRow = -1;
        if (!s.writable || s.dragRevision != s.program.revision || s.dragOwner != s.selectedOwner || snapshot(s.selectedOwner) != s.program)
        {
            return true;
        }
        if (WindowManager::findAt(screenX, screenY) != &window)
        {
            return true;
        }
        const int x = screenX - window.x, y = screenY - window.y;
        if (!moved && drag == EditorDrag::palette && contains(s.geometry.palette, x, y) && !s.program.rows.empty())
        {
            const auto first = s.firstSelected >= 0 ? size_t(s.firstSelected) : s.selected() ? indexOf(s.program, s.selected()->first)
                                                                                             : 0;
            const auto last = s.lastSelected >= 0 ? size_t(s.lastSelected) : s.selected() ? indexOf(s.program, s.selected()->last)
                                                                                          : first;
            if (const auto command = blockDrop(s.program, s.draggedKind, first, last, s.selectedBlock, s.draggedConditional))
            {
                const auto key = s.program.nextKey;
                s.apply(window, *command);
                if (s.program.nextKey > key)
                {
                    s.selectedBlock = key;
                }
            }
            return true;
        }
        if (!moved || drag == EditorDrag::selection)
        {
            return true;
        }
        if (s.expanded)
        {
            const auto& bin = window.widgets[s.bin];
            if (x >= bin.left && x <= bin.right && y >= bin.top && y <= bin.bottom)
            {
                Edit command;
                if (s.draggedBlock)
                {
                    command.kind = EditKind::removeBlock;
                    command.block.id = s.draggedBlock;
                }
                else if (drag == EditorDrag::row)
                {
                    command.kind = EditKind::deleteRows;
                    command.first = s.dragFirst;
                    command.last = s.dragLast;
                }
                else
                {
                    return true;
                }
                s.apply(window, command);
                return true;
            }
        }
        const auto& canvas = window.widgets[s.canvas];
        if (x <= canvas.left || x >= canvas.right - 12 || y <= canvas.top || y >= canvas.bottom)
        {
            return true;
        }
        const auto canvasY = static_cast<int16_t>(y - canvas.top - 1 + window.scrollAreas[s.canvasScroll].contentOffsetY);
        const auto command = s.routeDrop(drag, canvasY);
        if (command)
        {
            const auto key = s.program.nextKey;
            s.apply(window, *command);
            if (s.program.nextKey > key)
            {
                s.selectedBlock = key;
            }
        }
        else
        {
            s.status = "Invalid drop: select a valid order section";
            s.messageFrames = 80;
            window.invalidate();
        }
        return true;
    }

    bool EditorSession::buttonDown(Window& window, WidgetIndex_t widget, WidgetId id)
    {
        auto& s = *state;
        if (widget < s.check || widget > s.actions || !s.expanded)
        {
            return false;
        }
        if (s.writable)
        {
            s.onMouseDown(window, widget, id);
        }
        return true;
    }
    bool EditorSession::buttonUp(Window& window, WidgetIndex_t widget, WidgetId id)
    {
        auto& s = *state;
        if (widget < s.check || widget > s.actions || !s.expanded)
        {
            return false;
        }
        if (id == detailsId)
        {
            s.showDetails = !s.showDetails;
            window.callPrepareDraw();
            window.updateScrollWidgets();
            window.invalidate();
            return true;
        }
        if (!s.writable || !s.selected())
        {
            return true;
        }
        if (id == extraId)
        {
            s.editingExtra = !s.editingExtra;
        }
        else if (id == numberId && s.selected()->kind != Kind::repeat)
        {
            s.beginTextInput(window, widget, BlockField::value, s.editingExtra);
        }
        else if (id == nameId && s.selected()->kind == Kind::group)
        {
            s.beginTextInput(window, widget, BlockField::name, false);
        }
        else if (id == limitId)
        {
            s.beginTextInput(window, widget, BlockField::timeout, false);
        }
        else if (id == binId)
        {
            Edit command;
            command.kind = EditKind::removeBlock;
            command.block.id = s.selectedBlock;
            s.apply(window, command);
        }
        window.invalidate();
        return true;
    }
    bool EditorSession::dropdown(Window& window, WidgetIndex_t widget, int16_t item)
    {
        auto& s = *state;
        if (widget != s.canvas || s.inlineDropdownId == WidgetId::none)
        {
            return false;
        }
        s.dropdown(window, widget, s.inlineDropdownId, item);
        return true;
    }
    bool EditorSession::textInput(Window& window, WidgetIndex_t widget, const char* value)
    {
        auto& s = *state;
        if (widget != s.canvas || s.inputField == BlockField::none)
        {
            return false;
        }
        if (value && s.writable && s.selected() && s.inputBlock == s.selectedBlock && fieldEditStillValid(s.inputOwner, s.selectedOwner, s.program, s.inputBlock, s.inputRevision) && fieldEditStillValid(s.inputOwner, s.selectedOwner, snapshot(s.selectedOwner), s.inputBlock, s.inputRevision))
        {
            Edit command;
            const auto result = fieldTextEdit(s.program, s.inputBlock, s.inputField, s.inputExtra, value, command);
            s.inputField = BlockField::none;
            if (result)
            {
                s.apply(window, command);
            }
            else
            {
                s.status = result.message;
                s.messageFrames = 80;
                window.invalidate();
            }
        }
        else
        {
            s.inputField = BlockField::none;
        }
        return true;
    }
    bool EditorSession::escape(Window& window)
    {
        const bool active = dragging() || state->inputBlock || Input::isDropdownActive(window.type, window.number);
        state->cancelMenu(window);
        state->cancel();
        if (active)
        {
            window.invalidate();
        }
        return active;
    }
    std::optional<FormatArguments> EditorSession::tooltip(Window& window)
    {
        auto& s = *state;
        if (!showingProgram())
        {
            return {};
        }
        const auto mouse = Input::getMouseLocation();
        const auto& canvas = window.widgets[s.canvas];
        const int x = mouse.x - window.x - canvas.left - 1, y = mouse.y - window.y - canvas.top - 1 + window.scrollAreas[s.canvasScroll].contentOffsetY;
        if (x < 0 || x > s.layoutWidth || mouse.y < window.y + canvas.top || mouse.y > window.y + canvas.bottom)
        {
            return {};
        }
        if (const auto* v = s.at(y); v && v->block)
        {
            const auto it = std::find_if(s.program.blocks.begin(), s.program.blocks.end(), [&](const Block& b) { return b.id == v->block; });
            s.tooltipText = v->closing ? "Drop above the arm to add inside, below to move outside; drag the end grip to resize" : it != s.program.blocks.end() ? blockSentence(s.program, *it)
                                                                                                                                                               : "";
            return FormatArguments::common(StringIds::line_name_raw, s.tooltipText.c_str());
        }
        if (const auto* v = s.at(y); v && !v->block)
        {
            s.tooltipText = "Drag this order into a block; Shift-drag selects consecutive orders";
            return FormatArguments::common(StringIds::line_name_raw, s.tooltipText.c_str());
        }
        return {};
    }
    namespace
    {
        std::map<std::pair<WindowType, uint16_t>, std::unique_ptr<EditorSession>> sessions;
    }
    EditorSession& editorSession(Window& window)
    {
        auto& session = sessions[{ window.type, window.number }];
        if (!session)
        {
            session = std::make_unique<EditorSession>();
        }
        return *session;
    }
    void closeEditorSession(Window& window)
    {
        const auto it = sessions.find({ window.type, window.number });
        if (it != sessions.end())
        {
            it->second->suspend(window, false);
            sessions.erase(it);
        }
    }
}
