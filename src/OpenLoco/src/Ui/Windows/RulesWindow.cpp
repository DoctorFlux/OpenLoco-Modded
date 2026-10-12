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
#include "Rules/Editor.h"
#include "Rules/Platforms.h"
#include "Ui.h"
#include "Ui/Dropdown.h"
#include "Ui/ScrollView.h"
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

namespace OpenLoco::Ui::Windows::RulesWindow
{
    using namespace OpenLoco::Rules;
    namespace
    {
        constexpr WidgetId closeId{ "close" }, canvasId{ "canvas" }, checkId{ "check" }, compareId{ "compare" }, stopId{ "stop" }, cargoId{ "cargo" }, numberId{ "number" }, joinId{ "join" }, extraId{ "extra" }, targetId{ "target" }, nameId{ "name" }, limitId{ "limit" }, firstId{ "first" }, lastId{ "last" }, binId{ "bin" };
        constexpr WidgetId detailsId{ "details" }, actionsId{ "actions" };
        enum widx
        {
            frame,
            title,
            close,
            panel,
            canvas,
            check,
            compare,
            stop,
            cargo,
            number,
            join,
            extra,
            target,
            name,
            limit,
            firstRow,
            lastRow,
            bin,
            details,
            actions
        };
        static constexpr auto widgets = makeWidgets(
            Widgets::Frame({ 0, 0 }, { 600, 360 }, WindowColour::primary),
            Widgets::Caption({ 1, 1 }, { 598, 13 }, Widgets::Caption::Style::colourText, WindowColour::primary, StringIds::rules_title),
            Widgets::ImageButton(closeId, { 585, 2 }, { 13, 13 }, WindowColour::primary, ImageIds::close_button, StringIds::tooltip_close_window),
            Widgets::Panel({ 0, 15 }, { 600, 345 }, WindowColour::secondary),
            Widgets::ScrollView(canvasId, { 4, 42 }, { 592, 215 }, WindowColour::secondary, Scrollbars::vertical),
            Widgets::Button(checkId, { 4, 262 }, { 116, 20 }, WindowColour::secondary, StringIds::rules_check),
            Widgets::Button(compareId, { 122, 262 }, { 116, 20 }, WindowColour::secondary, StringIds::rules_compare),
            Widgets::Button(stopId, { 240, 262 }, { 116, 20 }, WindowColour::secondary, StringIds::rules_stop),
            Widgets::Button(cargoId, { 358, 262 }, { 116, 20 }, WindowColour::secondary, StringIds::rules_cargo),
            Widgets::Button(numberId, { 476, 262 }, { 120, 20 }, WindowColour::secondary, StringIds::rules_number),
            Widgets::Button(joinId, { 4, 286 }, { 116, 20 }, WindowColour::secondary, StringIds::rules_join),
            Widgets::Button(extraId, { 122, 286 }, { 116, 20 }, WindowColour::secondary, StringIds::rules_extra),
            Widgets::Button(targetId, { 240, 286 }, { 116, 20 }, WindowColour::secondary, StringIds::rules_target),
            Widgets::Button(nameId, { 358, 286 }, { 116, 20 }, WindowColour::secondary, StringIds::rules_name),
            Widgets::Button(limitId, { 476, 286 }, { 120, 20 }, WindowColour::secondary, StringIds::rules_minutes),
            Widgets::Button(firstId, { 4, 262 }, { 90, 20 }, WindowColour::secondary, StringIds::rules_first_row),
            Widgets::Button(lastId, { 4, 286 }, { 90, 20 }, WindowColour::secondary, StringIds::rules_last_row),
            Widgets::Button(binId, { 4, 320 }, { 592, 20 }, WindowColour::secondary, StringIds::rules_bin),
            Widgets::Button(detailsId, { 4, 30 }, { 66, 11 }, WindowColour::secondary, StringIds::rules_details),
            Widgets::Button(actionsId, { 74, 30 }, { 66, 11 }, WindowColour::secondary, StringIds::rules_actions));
        Owner selectedOwner{};
        uint16_t selectedVehicle = UINT16_MAX;
        bool followVehicle = false;
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
        std::optional<std::pair<BlockField, bool>> focusField;
        int firstSelected = -1, lastSelected = -1;
        bool editingExtra = false;
        std::string status;
        uint16_t messageFrames = 0;
        using Visual = ProgramVisual;
        std::vector<Visual> visuals;
        ProgramLayout canvasLayout;
        enum class Drag
        {
            none,
            palette,
            section,
            resize,
            row,
            selection
        };
        Drag drag = Drag::none;
        Kind draggedKind{};
        Key draggedBlock = 0;
        int dragFirst = -1, dragLast = -1, dropRow = -1;
        Key dropInto = 0;
        int16_t dragX = 0, dragY = 0;
        bool dragged = false;
        std::vector<int> choices;
        std::vector<Key> stopChoices;
        std::vector<Platform> platformChoices;
        std::vector<std::string> choiceLabels;
        size_t choicePage = 0;
        std::vector<int> pageItems;
        EditorRect menuAnchor{};
        WidgetIndex_t menuWidget = canvas;
        const char* names[] = { "Group", "If", "Else", "Repeat", "Wait until", "Skip", "Go to", "Else if", "Break" };
        constexpr Kind paletteKinds[] = { Kind::group, Kind::ifBlock, Kind::elseIf, Kind::otherwise, Kind::repeat, Kind::waitUntil, Kind::skip, Kind::goTo, Kind::breakLoop };
        const char* checkNames[] = { "Cargo waiting", "This vehicle is full", "This vehicle is empty", "This vehicle has space", "Platform is free", "This vehicle is late", "This vehicle is on time", "The year" };
        int32_t layoutWidth = 600;
        int16_t layoutHeight = 360;
        std::string blockText(const Block& b);
        void editInlineField(Window& window, const Visual& visual, const FieldVisual& field);
        std::vector<std::string> wrapBlock(std::string value, int16_t width)
        {
            std::vector<std::string> lines;
            while (!value.empty() && lines.size() < 4)
            {
                size_t length = value.size();
                while (length > 1 && Gfx::TextRenderer::getStringWidth(Gfx::Font::medium_bold, value.substr(0, length).c_str()) > width)
                {
                    --length;
                }
                if (length < value.size())
                {
                    const auto space = value.rfind(' ', length);
                    if (space != std::string::npos && space > 0)
                    {
                        length = space;
                    }
                }
                if (lines.size() == 3)
                {
                    lines.push_back(value);
                    break;
                }
                lines.push_back(value.substr(0, length));
                value.erase(0, length);
                while (!value.empty() && value.front() == ' ')
                {
                    value.erase(0, 1);
                }
            }
            return lines;
        }
        int paletteColumns(const Window& window) { return showDetails && window.widgets[canvas].height() < 150 ? 2 : 1; }
        int16_t paletteStep(const Window& window)
        {
            return paletteColumns(window) == 2 ? 15 : static_cast<int16_t>(std::min<int>(22, (window.widgets[canvas].height() - 4) / 9));
        }
        const Block* selected()
        {
            auto it = std::find_if(program.blocks.begin(), program.blocks.end(), [](const Block& b) { return b.id == selectedBlock; });
            return it == program.blocks.end() ? nullptr : &*it;
        }
        void refresh()
        {
            program = snapshot(selectedOwner);
            const auto layout = editorLayout(static_cast<int16_t>(layoutWidth), layoutHeight, showDetails);
            canvasLayout = programLayout(program, static_cast<int16_t>(layoutWidth - layout.paletteWidth - 26), 16);
            visuals = canvasLayout.items;
            if (firstSelected >= static_cast<int>(program.rows.size()))
            {
                firstSelected = lastSelected = -1;
            }
        }
        const Visual* at(int y)
        {
            const auto it = std::find_if(visuals.begin(), visuals.end(), [y](const Visual& v) { return y >= v.top && y < v.top + v.height; });
            return it == visuals.end() ? nullptr : &*it;
        }
        int rowAt(int y)
        {
            if (auto* v = at(y))
            {
                return static_cast<int>(v->row);
            }
            return y > 0 ? static_cast<int>(program.rows.size()) : 0;
        }
        void apply(Window& window, Edit e)
        {
            const auto offset = window.scrollAreas[0].contentOffsetY;
            const auto result = submit(selectedOwner, e);
            status = result ? "Rules updated" : result.message;
            messageFrames = 80;
            refresh();
            window.initScrollWidgets();
            window.scrollAreas[0].contentOffsetY = static_cast<int16_t>(std::clamp<int>(offset, 0, std::max<int>(0, std::max<int>(180, canvasLayout.height + 8) - window.widgets[canvas].height() + 3)));
            ScrollView::updateThumbs(window, canvas);
            window.invalidate();
        }
        std::string conditionText(const Condition& c)
        {
            std::string text = checkNames[static_cast<unsigned>(c.check)];
            if (c.check == Check::cargoWaiting || c.check == Check::year)
            {
                if (c.check == Check::cargoWaiting)
                {
                    char buffer[128]{};
                    const auto* object = ObjectManager::get<CargoObject>(c.cargo);
                    if (object)
                    {
                        StringManager::formatString(buffer, sizeof(buffer), object->name);
                    }
                    text += " (" + std::string(object ? buffer : "cargo") + ")";
                }
                text += c.compare == Compare::less ? " is less than " : c.compare == Compare::greater ? " is more than "
                                                                                                      : " is the same as ";
                text += std::to_string(c.value);
            }
            if (c.stop)
            {
                size_t number = 0;
                for (const auto& row : program.rows)
                {
                    if (isRoutable(row.raw))
                    {
                        ++number;
                    }
                    if (row.key == c.stop)
                    {
                        break;
                    }
                }
                text += " at stop " + std::to_string(number);
            }
            if (c.check == Check::platformFree)
            {
                text += ", platform " + std::to_string(c.platform);
            }
            return text;
        }
        std::string blockText(const Block& b) { return blockSentence(program, b); }
        void text(Gfx::DrawingContext& ctx, int16_t x, int16_t y, int16_t width, std::string value, bool white = false)
        {
            value.insert(value.begin(), static_cast<char>(white ? ControlCodes::Colour::white : ControlCodes::Colour::black));
            auto renderer = Gfx::TextRenderer(ctx);
            renderer.setCurrentFont(Gfx::Font::medium_bold);
            renderer.clipString(width, value.data());
            renderer.drawString({ x, y }, Colour::black, value.c_str());
        }
        void prepare(Window& window)
        {
            if (layoutWidth != window.width || layoutHeight != window.height)
            {
                layoutWidth = window.width;
                layoutHeight = window.height;
                refresh();
            }
            auto layout = editorLayout(window.width, window.height, showDetails);
            window.widgets[frame].right = window.width - 1;
            window.widgets[frame].bottom = window.height - 1;
            window.widgets[title].right = window.width - 2;
            window.widgets[close].left = window.width - 15;
            window.widgets[close].right = window.width - 3;
            window.widgets[panel].right = window.width - 1;
            window.widgets[panel].bottom = window.height - 1;
            window.widgets[canvas].right = window.width - 5;
            window.widgets[canvas].bottom = layout.canvasBottom;
            window.widgets[canvas].top = layout.canvasTop;
            const auto buttonWidth = static_cast<int16_t>((window.width - 10) / 6);
            const int controls[2][6] = { { check, compare, stop, cargo, number, firstRow }, { join, extra, target, name, limit, lastRow } };
            for (int row = 0; row < 2; ++row)
            {
                for (int column = 0; column < 6; ++column)
                {
                    auto& w = window.widgets[controls[row][column]];
                    w.left = 4 + column * buttonWidth;
                    w.right = w.left + buttonWidth - 3;
                    w.top = layout.controlsTop + row * 24;
                    w.bottom = w.top + 19;
                    w.hidden = !showDetails;
                }
            }
            window.widgets[bin].right = window.width - 5;
            window.widgets[bin].top = layout.binTop;
            window.widgets[bin].bottom = layout.binTop + 19;
            const auto* b = selected();
            window.widgets[name].text = b && b->kind != Kind::group ? StringIds::rules_when : StringIds::rules_name;
            window.widgets[number].text = b && b->kind == Kind::repeat ? (b->number ? StringIds::rules_times : StringIds::rules_forever) : StringIds::rules_number;
            window.widgets[target].text = b && b->kind == Kind::breakLoop ? StringIds::rules_loop : StringIds::rules_target;
            window.widgets[cargo].text = b && (editingExtra ? b->extra.check : b->condition.check) == Check::platformFree ? StringIds::rules_platform : StringIds::rules_cargo;
            window.activatedWidgets = (editingExtra ? uint64_t(1) << extra : 0) | (showDetails ? uint64_t(1) << details : 0);
            window.disabledWidgets = 0;
            for (int i = check; i <= lastRow; ++i)
            {
                window.disabledWidgets |= uint64_t(1) << i;
            }
            if (!b)
            {
                window.disabledWidgets |= uint64_t(1) << actions;
            }
            if (b)
            {
                if (b->kind != Kind::skip && b->kind != Kind::goTo && b->kind != Kind::breakLoop)
                {
                    window.disabledWidgets &= ~((uint64_t(1) << firstRow) | (uint64_t(1) << lastRow));
                }
                if (b->kind == Kind::ifBlock || b->kind == Kind::elseIf || b->kind == Kind::waitUntil)
                {
                    for (int i : { check, join })
                    {
                        window.disabledWidgets &= ~(uint64_t(1) << i);
                    }
                    const auto& condition = editingExtra ? b->extra : b->condition;
                    if (condition.check == Check::cargoWaiting || condition.check == Check::year)
                    {
                        for (int i : { compare, number })
                        {
                            window.disabledWidgets &= ~(uint64_t(1) << i);
                        }
                    }
                    if (condition.check == Check::cargoWaiting || condition.check == Check::platformFree || condition.check == Check::late || condition.check == Check::onTime)
                    {
                        window.disabledWidgets &= ~(uint64_t(1) << stop);
                    }
                    if (condition.check == Check::cargoWaiting || condition.check == Check::platformFree)
                    {
                        window.disabledWidgets &= ~(uint64_t(1) << cargo);
                    }
                    if (b->join != Join::none)
                    {
                        window.disabledWidgets &= ~(uint64_t(1) << extra);
                    }
                    if (b->kind == Kind::waitUntil)
                    {
                        window.disabledWidgets &= ~(uint64_t(1) << limit);
                    }
                }
                if (b->kind == Kind::repeat)
                {
                    window.disabledWidgets &= ~(uint64_t(1) << number);
                }
                window.disabledWidgets &= ~(uint64_t(1) << name);
                if (b->kind == Kind::goTo || b->kind == Kind::breakLoop)
                {
                    window.disabledWidgets &= ~(uint64_t(1) << target);
                }
            }
        }
        uint16_t progressVehicle()
        {
            if (selectedVehicle != UINT16_MAX && ownerForVehicle(selectedVehicle) == selectedOwner)
            {
                return selectedVehicle;
            }
            const auto* line = selectedOwner.kind == OwnerKind::line ? Lines::findLine(selectedOwner.id) : nullptr;
            return line && !line->vehicles.empty() ? line->vehicles.front() : UINT16_MAX;
        }
        void draw(Window& window, Gfx::DrawingContext& ctx)
        {
            window.draw(ctx);
            const auto* line = selectedOwner.kind == OwnerKind::line ? Lines::findLine(selectedOwner.id) : nullptr;
            std::string caption = line ? "Shared line: " + line->name : "Vehicle " + std::to_string(selectedOwner.id);
            if (!line)
            {
                if (const auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(selectedOwner.id)))
                {
                    char buffer[256]{};
                    FormatArguments args;
                    args.push(head->ordinalNumber);
                    StringManager::formatString(buffer, sizeof(buffer), head->name, args);
                    caption = std::string(buffer) + " - own orders";
                }
            }
            text(ctx, 5, 19, window.width - 10, caption);
            text(ctx, 146, 30, window.width - 152, "Click fields; drag headers or =.");
            auto footer = progressVehicle() != UINT16_MAX ? vehicleStatus(progressVehicle()) : "Select a vehicle on this line to see its running rule";
            if (!status.empty())
            {
                footer = status + " | " + footer;
            }
            text(ctx, 5, window.height - 28, window.width - 10, footer);
            text(ctx, 5, window.height - 15, window.width - 10, vehicleNextStop(progressVehicle()));
            if (dragged && drag != Drag::none && drag != Drag::selection)
            {
                std::string label;
                if (drag == Drag::palette)
                {
                    label = names[static_cast<unsigned>(draggedKind)];
                }
                else if (draggedBlock)
                {
                    const auto block = std::find_if(program.blocks.begin(), program.blocks.end(), [](const Block& b) { return b.id == draggedBlock; });
                    if (block != program.blocks.end())
                    {
                        label = blockText(*block);
                    }
                }
                else
                {
                    label = "Move " + std::to_string(dragLast - dragFirst + 1) + " order row(s)";
                }
                const auto mouse = Input::getMouseLocation();
                const auto lines = wrapBlock(label, std::min<int16_t>(260, window.width - 24));
                const auto width = std::min<int16_t>(280, window.width - 8);
                const auto height = static_cast<int16_t>(lines.size() * 12 + 8);
                const auto x = static_cast<int16_t>(std::clamp<int>(mouse.x - window.x + 12, 4, window.width - width - 4));
                const auto y = static_cast<int16_t>(std::clamp<int>(mouse.y - window.y + 16, 16, window.height - height - 4));
                drawStackBlock(ctx, x, y, width, height, (drag == Drag::palette ? draggedKind : selected() ? selected()->kind
                                                                                                           : Kind::group),
                               Feedback::idle,
                               true);
                for (size_t i = 0; i < lines.size(); ++i)
                {
                    text(ctx, x + 5, static_cast<int16_t>(y + 4 + i * 12), width - 10, lines[i], true);
                }
            }
        }
        void drawScroll(Window& window, Gfx::DrawingContext& ctx, uint32_t)
        {
            const auto layout = editorLayout(window.width, window.height, showDetails);
            const auto width = window.widgets[canvas].width() - 14;
            const auto* runtime = runtimeForVehicle(progressVehicle());
            ctx.clear(Colours::getShade(Colour::white, 9));

            const auto columns = paletteColumns(window);
            const auto rows = (std::size(paletteKinds) + columns - 1) / columns;
            const auto columnWidth = layout.paletteWidth / columns;
            for (size_t i = 0; i < std::size(paletteKinds); ++i)
            {
                const auto step = paletteStep(window);
                const auto x = static_cast<int16_t>(2 + (i / rows) * columnWidth);
                const auto y = static_cast<int16_t>(window.scrollAreas[0].contentOffsetY + 2 + (i % rows) * step);
                drawStackBlock(ctx, x, y, columnWidth - 4, step - 4, paletteKinds[i]);
                text(ctx, x + 5, y + 2, columnWidth - 12, names[static_cast<unsigned>(paletteKinds[i])], true);
            }
            drawProgramBlocks(ctx, program, runtime, canvasLayout, static_cast<int16_t>(window.width - layout.paletteWidth - 26), layout.paletteWidth + 1, selectedBlock);
            int ordinal = 0;
            for (const auto& v : visuals)
            {
                auto x = static_cast<int16_t>(layout.paletteWidth + 4);
                if (v.block)
                {
                    if (v.closing && dragged && dropRow == static_cast<int>(v.row + 1) && dropInto == v.block)
                    {
                        const auto left = x + visualIndent(v, static_cast<int16_t>(window.width - layout.paletteWidth - 26)) + 12;
                        ctx.fillRect(left, v.top + 10, width - 3, v.top + 11, PaletteIndex::black0, Gfx::RectFlags::none);
                    }
                    if (v.closing && v.block == selectedBlock)
                    {
                        text(ctx, width - 12, v.top + 4, 12, "+");
                    }
                }
                else
                {
                    x += visualIndent(v, static_cast<int16_t>(window.width - layout.paletteWidth - 26));
                    const auto& row = program.rows[v.row];
                    if (firstSelected <= static_cast<int>(v.row) && static_cast<int>(v.row) <= lastSelected && firstSelected >= 0)
                    {
                        drawOrderBlock(ctx, x, v.top, width - x, v.height, true);
                    }
                    text(ctx, x + 5, v.top + 4, 12, "=");
                    const auto* order = reinterpret_cast<const Vehicles::Order*>(&row.raw);
                    char buffer[256]{};
                    const auto converted = Lines::fromVehicleOrder(*order);
                    std::string label;
                    if (isRoutable(row.raw))
                    {
                        label = std::to_string(++ordinal) + ". ";
                    }
                    if (auto* station = order->as<Vehicles::OrderStation>())
                    {
                        const auto* s = StationManager::get(station->getStation());
                        if (s && !s->empty())
                        {
                            FormatArguments args;
                            if (order->getType() == Vehicles::OrderType::StopAt)
                            {
                                static_cast<const Vehicles::OrderStopAt*>(order)->setFormatArguments(args);
                            }
                            else
                            {
                                static_cast<const Vehicles::OrderRouteThrough*>(order)->setFormatArguments(args);
                            }
                            StringManager::formatString(buffer, sizeof(buffer), order->getType() == Vehicles::OrderType::StopAt ? StringIds::orders_stop_at : StringIds::orders_route_through, args);
                        }
                        label += buffer[0] ? buffer : "Station";
                    }
                    else if ((row.raw & 7) == 3)
                    {
                        label += "Waypoint (" + std::to_string(converted.tileX) + ", " + std::to_string(converted.tileY) + ")";
                    }
                    else
                    {
                        label += (row.raw & 7) == 4 ? "Unload all" : "Wait for full load";
                    }
                    text(ctx, x + 22, v.top + 4, width - x - 26, label);
                    if (dragged && dropRow == static_cast<int>(v.row))
                    {
                        ctx.fillRect(x, v.top, width, v.top + 1, PaletteIndex::black0, Gfx::RectFlags::none);
                    }
                }
            }
            drawRouteOrderMarker(ctx, program, canvasLayout, routeOrderForVehicle(progressVehicle()), static_cast<int16_t>(window.width - layout.paletteWidth - 26), layout.paletteWidth + 1);
            if (visuals.empty())
            {
                text(ctx, layout.paletteWidth + 5, 5, width - layout.paletteWidth - 10, "Add stops in the vehicle Route tab or Lines first.");
            }
        }
        void scrollSize(Window&, uint32_t, int32_t& width, int32_t& height)
        {
            width = 0;
            height = std::max<int>(180, canvasLayout.height + 8);
        }
        void mouseDown(Window& window, int16_t x, int16_t y, uint8_t)
        {
            refresh();
            dragged = false;
            dragX = x;
            dragY = y;
            dropRow = -1;
            draggedBlock = 0;
            const auto layout = editorLayout(window.width, window.height, showDetails);
            if (const auto* block = selected(); block && block->kind != Kind::skip && block->kind != Kind::goTo && block->kind != Kind::breakLoop && x >= window.widgets[canvas].width() - 28)
            {
                const auto end = std::find_if(visuals.begin(), visuals.end(), [&](const Visual& visual) { return visual.closing && visual.block == block->id; });
                if (end != visuals.end() && y >= end->top && y < end->top + end->height)
                {
                    drag = Drag::resize;
                    draggedBlock = block->id;
                    return;
                }
            }
            if (x < layout.paletteWidth)
            {
                const auto paletteY = y - window.scrollAreas[0].contentOffsetY - 2;
                const auto columns = paletteColumns(window);
                const auto rows = static_cast<int>((std::size(paletteKinds) + columns - 1) / columns);
                const auto column = x / (layout.paletteWidth / columns);
                const auto item = column * rows + paletteY / paletteStep(window);
                if (paletteY < 0 || item < 0 || item >= static_cast<int>(std::size(paletteKinds)))
                {
                    return;
                }
                drag = Drag::palette;
                draggedKind = paletteKinds[item];
                return;
            }
            const auto* v = at(y);
            if (!v)
            {
                return;
            }
            if (v->block)
            {
                if (const auto* field = blockFieldHit(canvasLayout, x, y, static_cast<int16_t>(window.width - layout.paletteWidth - 26), layout.paletteWidth + 1))
                {
                    selectedBlock = v->block;
                    firstSelected = lastSelected = -1;
                    drag = Drag::none;
                    editInlineField(window, *v, *field);
                    window.invalidate();
                    return;
                }
                selectedBlock = draggedBlock = v->block;
                firstSelected = lastSelected = -1;
                editingExtra = false;
                const auto* b = selected();
                dragFirst = static_cast<int>(indexOf(program, b->first));
                dragLast = static_cast<int>(indexOf(program, b->last));
                drag = Drag::section;
            }
            else if (orderGripHit(*v, x, layout.paletteWidth, static_cast<int16_t>(window.width - layout.paletteWidth - 26)))
            {
                dragFirst = static_cast<int>(v->row);
                dragLast = dragFirst;
                if (firstSelected >= 0 && firstSelected <= dragFirst && dragFirst <= lastSelected)
                {
                    dragFirst = firstSelected;
                    dragLast = lastSelected;
                }
                drag = Drag::row;
            }
            else
            {
                selectedBlock = 0;
                dragFirst = static_cast<int>(v->row);
                drag = Drag::selection;
                firstSelected = lastSelected = dragFirst;
            }
            status.clear();
            window.invalidate();
        }
        std::optional<Edit> routeDrop(Drag previous, int destination, int canvasY)
        {
            if (previous == Drag::resize)
            {
                const auto* block = selected();
                if (!block || destination < 0 || destination >= static_cast<int>(program.rows.size()))
                {
                    return {};
                }
                Edit command;
                command.kind = EditKind::updateBlock;
                command.block = *block;
                command.block.last = program.rows[destination].key;
                return command;
            }
            const auto position = programDrop(program, canvasLayout, static_cast<int16_t>(canvasY));
            if (previous == Drag::row || previous == Drag::section)
            {
                Edit e;
                e.kind = EditKind::moveRows;
                e.first = dragFirst;
                e.last = dragLast;
                e.destination = position.row;
                e.block.id = draggedBlock;
                if (position.into)
                {
                    const auto target = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.id == position.into; });
                    if (!(e.first <= indexOf(program, target->first) && indexOf(program, target->last) <= e.last))
                    {
                        e.into = position.into;
                    }
                }
                return e;
            }
            const auto selection = firstSelected >= 0 ? std::optional<std::pair<size_t, size_t>>{ { static_cast<size_t>(firstSelected), static_cast<size_t>(lastSelected) } } : std::nullopt;
            return paletteDrop(program, canvasLayout, draggedKind, static_cast<int16_t>(canvasY), selection);
        }
        void mouseDrag(Window& window, int16_t x, int16_t y, uint8_t)
        {
            if (drag == Drag::none)
            {
                return;
            }
            dragged |= std::abs(x - dragX) + std::abs(y - dragY) > 4;
            if (dragged && x > editorLayout(window.width, window.height, showDetails).paletteWidth)
            {
                auto& scroll = window.scrollAreas[0];
                const auto visibleY = y - scroll.contentOffsetY;
                const auto height = window.widgets[canvas].height() - 3;
                const auto delta = visibleY < 14 ? -6 : visibleY > height - 14 ? 6
                                                                               : 0;
                int32_t contentWidth = 0, contentHeight = 0;
                scrollSize(window, 0, contentWidth, contentHeight);
                scroll.contentOffsetY = static_cast<int16_t>(std::clamp<int>(scroll.contentOffsetY + delta, 0, std::max<int>(0, contentHeight - height)));
                ScrollView::updateThumbs(window, canvas);
            }
            const auto position = programDrop(program, canvasLayout, y);
            dropRow = drag == Drag::resize ? rowAt(y) : static_cast<int>(position.row);
            dropInto = drag == Drag::resize ? 0 : position.into;
            if (drag != Drag::selection)
            {
                const auto proposed = routeDrop(drag, dropRow, y);
                auto preview = program;
                if (x <= editorLayout(window.width, window.height, showDetails).paletteWidth || !proposed || !edit(preview, *proposed))
                {
                    dropRow = -1;
                }
            }
            if (drag == Drag::selection && !program.rows.empty())
            {
                const auto row = std::clamp<int>(dropRow, 0, static_cast<int>(program.rows.size()) - 1);
                firstSelected = std::min(row, dragFirst);
                lastSelected = std::max(row, dragFirst);
            }
            window.invalidate();
        }
        void mouseRelease(Window& window, int16_t screenX, int16_t screenY, uint8_t)
        {
            const auto previous = drag;
            drag = Drag::none;
            if (!dragged && previous == Drag::palette && !program.rows.empty())
            {
                const auto first = firstSelected >= 0 ? static_cast<size_t>(firstSelected) : selected() ? indexOf(program, selected()->first)
                                                                                                        : 0;
                const auto last = lastSelected >= 0 ? static_cast<size_t>(lastSelected) : selected() ? indexOf(program, selected()->last)
                                                                                                     : first;
                const auto command = blockDrop(program, draggedKind, first, last, selectedBlock);
                if (command)
                {
                    const auto key = program.nextKey;
                    apply(window, *command);
                    if (program.nextKey > key)
                    {
                        selectedBlock = key;
                    }
                }
                return;
            }
            if (!dragged || previous == Drag::none || previous == Drag::selection)
            {
                return;
            }
            if (WindowManager::findAt(screenX, screenY) != &window)
            {
                window.invalidate();
                return;
            }
            const auto y = screenY - window.y, x = screenX - window.x;
            const auto& binWidget = window.widgets[bin];
            if (x >= binWidget.left && x <= binWidget.right && y >= binWidget.top && y <= binWidget.bottom)
            {
                Edit e;
                if (draggedBlock)
                {
                    e.kind = EditKind::removeBlock;
                    e.block.id = draggedBlock;
                }
                else if (previous == Drag::row)
                {
                    e.kind = EditKind::deleteRows;
                    e.first = dragFirst;
                    e.last = dragLast;
                }
                else
                {
                    return;
                }
                apply(window, e);
                return;
            }
            const auto& view = window.widgets[canvas];
            if (x < view.left + editorLayout(window.width, window.height, showDetails).paletteWidth || x >= view.right - 12 || y < view.top || y > view.bottom)
            {
                return;
            }
            const auto canvasY = y - view.top - 1 + window.scrollAreas[0].contentOffsetY;
            const int destination = rowAt(canvasY);
            const auto proposed = routeDrop(previous, destination, canvasY);
            if (!proposed)
            {
                status = "Invalid drop: use an order or a drop-inside slot; Else needs following orders.";
                messageFrames = 80;
                window.invalidate();
                return;
            }
            const auto key = program.nextKey;
            apply(window, *proposed);
            if (find(selectedOwner) && program.nextKey > key)
            {
                selectedBlock = key;
            }
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
            const auto& w = window.widgets[widget];
            menuAnchor = inlineAnchor.value_or(EditorRect{ window.x + w.left, window.y + w.top, w.width(), w.height() });
            menuWidget = inlineAnchor ? canvas : widget;
            showMenuPage(window);
        }
        void onMouseDown(Window& window, WidgetIndex_t widget, WidgetId id)
        {
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
            if (id == nameId && b->kind != Kind::group)
            {
                labels = { b->kind == Kind::waitUntil ? "Automatic: on arrival" : (b->kind == Kind::skip || b->kind == Kind::goTo || b->kind == Kind::breakLoop) && b->parent ? "Automatic: with parent"
                                                                                                                                                                              : "Automatic: before travelling",
                           "Before travelling",
                           "On arrival" };
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
                for (unsigned i = 0; i <= unsigned(Check::year); ++i)
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
                labels = { "Less than", "The same as", "More than" };
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
                    size_t number = 0;
                    for (const auto& numbered : program.rows)
                    {
                        if (isRoutable(numbered.raw))
                        {
                            ++number;
                        }
                        if (numbered.key == row.key)
                        {
                            break;
                        }
                    }
                    stopChoices.push_back(row.key);
                    std::string label = "Stop / waypoint " + std::to_string(number);
                    const auto* order = reinterpret_cast<const Vehicles::Order*>(&row.raw);
                    if (const auto* station = order->as<Vehicles::OrderStation>(); station && StationManager::get(station->getStation()) && !StationManager::get(station->getStation())->empty())
                    {
                        char name[128]{};
                        FormatArguments args;
                        station->setFormatArguments(args);
                        StringManager::formatString(name, sizeof(name), StringIds::orders_stop_at, args);
                        label += ": ";
                        label += name;
                    }
                    else if ((row.raw & 7) == 3)
                    {
                        const auto waypoint = Lines::fromVehicleOrder(*order);
                        label = "Waypoint " + std::to_string(number) + " (" + std::to_string(waypoint.tileX) + ", " + std::to_string(waypoint.tileY) + ")";
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
        void dropdown(Window& window, WidgetIndex_t, WidgetId id, int16_t index)
        {
            if (id == canvasId)
            {
                id = inlineDropdownId;
            }
            const auto* selectedValue = selected();
            if (!selectedValue || inputBlock != selectedBlock || !fieldEditStillValid(inputOwner, selectedOwner, program, inputBlock, inputRevision) || !fieldEditStillValid(inputOwner, selectedOwner, snapshot(selectedOwner), inputBlock, inputRevision) || inputField != BlockField::none || index < 0 || static_cast<size_t>(index) >= pageItems.size())
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
            if (id == nameId && b.kind != Kind::group)
            {
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
                if (c.check == Check::cargoWaiting || c.check == Check::platformFree || c.check == Check::late || c.check == Check::onTime)
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
                c.compare = static_cast<Compare>(index);
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
        void beginTextInput(Window& window, WidgetIndex_t widget, BlockField field, bool extraField)
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
            TextInput::openTextInput(&window, StringIds::rules_title, field == BlockField::name ? StringIds::rules_name_prompt : StringIds::rules_number_prompt, StringIds::line_name_raw, widget, FormatArguments::common(value.c_str()), field == BlockField::name ? 80 : 9);
        }
        void editInlineField(Window& window, const Visual& visual, const FieldVisual& field)
        {
            selectedBlock = visual.block;
            editingExtra = field.extra;
            if (field.field == BlockField::value || field.field == BlockField::repeat || field.field == BlockField::timeout || field.field == BlockField::name)
            {
                beginTextInput(window, field.field == BlockField::timeout ? limit : field.field == BlockField::name ? name
                                                                                                                    : number,
                               field.field,
                               field.extra);
                return;
            }
            WidgetIndex_t widget = check;
            switch (field.field)
            {
                case BlockField::check: widget = check; break;
                case BlockField::compare: widget = compare; break;
                case BlockField::stop: widget = stop; break;
                case BlockField::cargo: widget = cargo; break;
                case BlockField::join: widget = join; break;
                case BlockField::when: widget = name; break;
                case BlockField::target: widget = target; break;
                default: return;
            }
            const auto layout = editorLayout(window.width, window.height, showDetails);
            const auto left = layout.paletteWidth + 4 + visualIndent(visual, static_cast<int16_t>(window.width - layout.paletteWidth - 26)) + field.left;
            inlineAnchor = EditorRect{ window.x + window.widgets[canvas].left + 1 + left, window.y + window.widgets[canvas].top + 1 + field.top - window.scrollAreas[0].contentOffsetY, field.width, field.height };
            inlineDropdownId = window.widgets[widget].id;
            onMouseDown(window, widget, inlineDropdownId);
            inlineAnchor.reset();
        }
        void onMouseUp(Window& window, WidgetIndex_t widget, WidgetId id)
        {
            if (id == closeId)
            {
                WindowManager::close(&window);
                return;
            }
            if (id == detailsId)
            {
                const auto offset = window.scrollAreas[0].contentOffsetY;
                showDetails = !showDetails;
                refresh();
                prepare(window);
                window.initScrollWidgets();
                window.scrollAreas[0].contentOffsetY = static_cast<int16_t>(std::clamp<int>(offset, 0, std::max<int>(0, canvasLayout.height - window.widgets[canvas].height() + 3)));
                ScrollView::updateThumbs(window, canvas);
                window.invalidate();
                return;
            }
            if (id == extraId)
            {
                editingExtra = !editingExtra;
                window.invalidate();
                return;
            }
            const auto* b = selected();
            if (!b)
            {
                return;
            }
            if (id == numberId && b->kind != Kind::repeat)
            {
                beginTextInput(window, widget, BlockField::value, editingExtra);
            }
            if (id == nameId && b->kind == Kind::group)
            {
                beginTextInput(window, widget, BlockField::name, false);
            }
            if (id == limitId)
            {
                beginTextInput(window, widget, BlockField::timeout, false);
            }
        }
        void textInput(Window& window, WidgetIndex_t, WidgetId, const char* value)
        {
            if (!value || !selected() || inputBlock != selectedBlock || !fieldEditStillValid(inputOwner, selectedOwner, program, inputBlock, inputRevision) || !fieldEditStillValid(inputOwner, selectedOwner, snapshot(selectedOwner), inputBlock, inputRevision) || inputField == BlockField::none)
            {
                return;
            }
            Edit command;
            const auto result = fieldTextEdit(program, inputBlock, inputField, inputExtra, value, command);
            inputField = BlockField::none;
            if (result)
            {
                apply(window, command);
            }
            else
            {
                status = result.message;
                messageFrames = 80;
                window.invalidate();
            }
        }
        bool keyUp(Window& window, uint32_t, uint32_t key)
        {
            if (key != 27)
            {
                return false;
            }
            drag = Drag::none;
            dragged = false;
            dropRow = -1;
            focusField.reset();
            inputField = BlockField::none;
            inputBlock = 0;
            if (Input::isDropdownActive(window.type, window.number))
            {
                WindowManager::close(WindowType::dropdown, 0);
                Input::resetFlag(Input::Flags::widgetPressed);
                Input::state(Input::State::normal);
            }
            window.invalidate();
            return true;
        }
        void update(Window& window)
        {
            if (messageFrames != 0 && --messageFrames == 0)
            {
                status.clear();
            }
            if (selectedOwner.kind == OwnerKind::line && !Lines::findLine(selectedOwner.id))
            {
                WindowManager::close(&window);
                return;
            }
            if (selectedOwner.kind == OwnerKind::vehicle && !EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(selectedOwner.id)))
            {
                WindowManager::close(&window);
                return;
            }
            if (followVehicle && selectedVehicle != UINT16_MAX)
            {
                const auto nextOwner = ownerForVehicle(selectedVehicle);
                if (nextOwner != selectedOwner)
                {
                    selectedOwner = nextOwner;
                    selectedBlock = 0;
                    focusField.reset();
                    inputField = BlockField::none;
                    inputBlock = 0;
                    drag = Drag::none;
                    refresh();
                }
            }
            const auto current = snapshot(selectedOwner);
            if (current != program)
            {
                drag = Drag::none;
                refresh();
                window.initScrollWidgets();
            }
            if (focusField && selected())
            {
                const auto focus = *focusField;
                focusField.reset();
                for (const auto& visual : visuals)
                {
                    if (visual.block != selectedBlock || visual.closing)
                    {
                        continue;
                    }
                    const auto field = std::find_if(visual.fields.begin(), visual.fields.end(), [&](const FieldVisual& f) { return f.field == focus.first && f.extra == focus.second; });
                    if (field == visual.fields.end())
                    {
                        break;
                    }
                    window.scrollAreas[0].contentOffsetY = static_cast<int16_t>(std::clamp<int>(visual.top, 0, std::max<int>(0, canvasLayout.height - window.widgets[canvas].height() + 3)));
                    ScrollView::updateThumbs(window, canvas);
                    editInlineField(window, visual, *field);
                    break;
                }
            }
            window.invalidate();
        }
        static const WindowEventList events = {
            .onClose = [](Window& window) {
                drag = Drag::none;
                inputField = BlockField::none;
                inputBlock = 0;
                focusField.reset();
                if (Input::isDropdownActive(window.type, window.number))
                {
                    WindowManager::close(WindowType::dropdown, 0);
                    Input::resetFlag(Input::Flags::widgetPressed);
                    Input::state(Input::State::normal);
                } },
            .onMouseUp = onMouseUp,
            .onResize = [](Window& w) { w.setSizeBounds({ 360, 240 }, { 1000, 700 }); prepare(w); },
            .onMouseDown = onMouseDown,
            .onDropdown = dropdown,
            .onUpdate = update,
            .getScrollSize = scrollSize,
            .scrollMouseDown = mouseDown,
            .scrollMouseDrag = mouseDrag,
            .scrollMouseUp = mouseRelease,
            .textInput = textInput,
            .prepareDraw = prepare,
            .draw = draw,
            .drawScroll = drawScroll,
            .keyUp = keyUp,
        };
    }
    Window* open(Owner owner, Window& opener, uint16_t vehicle, std::optional<size_t> row, Key focusedBlock, BlockField field, bool extraField)
    {
        selectedOwner = owner;
        selectedVehicle = vehicle;
        followVehicle = opener.type == WindowType::vehicle;
        selectedBlock = 0;
        firstSelected = lastSelected = -1;
        status.clear();
        drag = Drag::none;
        editingExtra = false;
        inputField = BlockField::none;
        inputBlock = 0;
        inlineDropdownId = WidgetId::none;
        inlineAnchor.reset();
        focusField = field == BlockField::none ? std::nullopt : std::optional{ std::pair{ field, extraField } };
        refresh();
        if (row && *row < program.rows.size())
        {
            firstSelected = lastSelected = static_cast<int>(*row);
            for (const auto& block : program.blocks)
            {
                if (indexOf(program, block.first) <= *row && *row <= indexOf(program, block.last))
                {
                    selectedBlock = block.id;
                }
            }
        }
        if (focusedBlock && std::any_of(program.blocks.begin(), program.blocks.end(), [focusedBlock](const Block& b) { return b.id == focusedBlock; }))
        {
            selectedBlock = focusedBlock;
        }
        std::vector<EditorRect> obstacles;
        for (size_t i = 0; i < WindowManager::count(); ++i)
        {
            const auto* w = WindowManager::get(i);
            if (w->type == WindowType::vehicle || w->type == WindowType::lineManager || w->type == WindowType::lineColourSchemes)
            {
                obstacles.push_back({ w->x, w->y, w->width, w->height });
            }
        }
        const auto placement = placeEditor({ opener.x, opener.y, opener.width, opener.height }, static_cast<int16_t>(Ui::width()), static_cast<int16_t>(Ui::height()), obstacles);
        if (auto* existing = WindowManager::bringToFront(WindowType::rules))
        {
            existing->invalidate();
            existing->x = placement.x;
            existing->y = placement.y;
            existing->setSize({ placement.width, placement.height });
            prepare(*existing);
            existing->initScrollWidgets();
            existing->invalidate();
            return existing;
        }
        auto* window = WindowManager::createWindow(WindowType::rules, { placement.x, placement.y }, { placement.width, placement.height }, WindowFlags::resizable, events);
        if (!window)
        {
            return nullptr;
        }
        window->setWidgets(widgets);
        std::copy(std::begin(opener.colours), std::end(opener.colours), std::begin(window->colours));
        window->setSizeBounds({ 360, 240 }, { 1000, 700 });
        prepare(*window);
        window->initScrollWidgets();
        return window;
    }
}
