#include "Audio/Audio.h"
#include "Entities/EntityManager.h"
#include "Graphics/Colour.h"
#include "Graphics/Gfx.h"
#include "Graphics/ImageIds.h"
#include "Economy/Currency.h"
#include "Graphics/TextRenderer.h"
#include "Input.h"
#include "Lines/Line.h"
#include "Localisation/FormatArguments.hpp"
#include "Localisation/Formatting.h"
#include "Localisation/StringIds.h"
#include "Localisation/StringManager.h"
#include "Map/MapSelection.h"
#include "Map/RoadElement.h"
#include "Map/StationElement.h"
#include "Map/TileElementEntry.h"
#include "Map/TileManager.h"
#include "Map/Track/TrackData.h"
#include "Map/TrackElement.h"
#include "Scenario/ScenarioManager.h"
#include "Objects/CargoObject.h"
#include "Objects/InterfaceSkinObject.h"
#include "Objects/ObjectManager.h"
#include "Ui/Dropdown.h"
#include "Ui/ScrollFlags.hpp"
#include "Ui/ToolManager.h"
#include "Ui/ViewportInteraction.h"
#include "Ui/Widget.h"
#include "Ui/Widgets/ButtonWidget.h"
#include "Ui/Widgets/CheckboxWidget.h"
#include "Ui/Widgets/ColourButtonWidget.h"
#include "Ui/Widgets/CaptionWidget.h"
#include "Ui/Widgets/FrameWidget.h"
#include "Ui/Widgets/ImageButtonWidget.h"
#include "Ui/Widgets/PanelWidget.h"
#include "Ui/Widgets/ScrollViewWidget.h"
#include "Ui/Widgets/TabWidget.h"
#include "Ui/WindowManager.h"
#include "Vehicles/OrderManager.h"
#include "Vehicles/Orders.h"
#include "Vehicles/Vehicle.h"
#include "Vehicles/Vehicle1.h"
#include "Vehicles/Vehicle2.h"
#include "Vehicles/VehicleHead.h"
#include "ViewportManager.h"
#include "World/CompanyManager.h"
#include "World/Station.h"
#include "World/StationManager.h"
#include <OpenLoco/Math/Vector.hpp>
#include <algorithm>
#include <cstring>
#include <vector>

namespace OpenLoco::Ui::Windows::LineSchemes
{
    Window* open(uint16_t lineId);
}

namespace OpenLoco::Ui::Windows::LineManager
{
    static constexpr Ui::Size kWindowSize = { 420, 268 };
    static constexpr int16_t kRowHeight = 10;
    static constexpr int16_t kLineRowHeight = 27;

    static uint16_t _selectedLine = 0;
    static int16_t _selectedOrder = -1;
    static int16_t _lineHover = -1;
    static int16_t _orderHover = -1;
    static std::vector<uint8_t> _cargoIds;
    static std::vector<uint16_t> _vehicleChoices;
    static std::vector<VehicleType> _buyTypes;
    static std::vector<StringId> _dropdownUserStrings;
    static Lines::LineKind _filter = Lines::kEveryLineKind;
    static bool _showPath = true;
    static bool _showAllLines = false;
    static bool _colourSecondary = false;

    enum widx
    {
        frame,
        title,
        close,
        panel,
        lines,
        orders,
        create,
        remove,
        loop,
        orderForceUnload,
        orderWait,
        orderDelete,
        orderUp,
        orderDown,
        tool,
        tabOrders,
        tabVehicles,
        localMode,
        expressMode,
        addVehicle,
        buyVehicle,
        addSlot,
        autoUnbunch,
        filterAll,
        filterRail,
        filterRoad,
        filterTram,
        filterAir,
        filterWater,
        colourPrimary,
        colourSecondary,
        showPath,
        showAllLines,
        paintVehicles,
        schemes,
    };

    namespace Widx
    {
        constexpr WidgetId kTitle{ "title" };
        constexpr WidgetId kClose{ "close" };
        constexpr WidgetId kLines{ "lines" };
        constexpr WidgetId kOrders{ "orders" };
        constexpr WidgetId kCreate{ "create" };
        constexpr WidgetId kRemove{ "remove" };
        constexpr WidgetId kLoop{ "loop" };
        constexpr WidgetId kOrderForceUnload{ "order_force_unload" };
        constexpr WidgetId kOrderWait{ "order_wait" };
        constexpr WidgetId kOrderDelete{ "order_delete" };
        constexpr WidgetId kOrderUp{ "order_up" };
        constexpr WidgetId kOrderDown{ "order_down" };
        constexpr WidgetId kTool{ "tool" };
        constexpr WidgetId kTabOrders{ "tab_orders" };
        constexpr WidgetId kTabVehicles{ "tab_vehicles" };
        constexpr WidgetId kLocalMode{ "local_mode" };
        constexpr WidgetId kExpressMode{ "express_mode" };
        constexpr WidgetId kAddVehicle{ "add_vehicle" };
        constexpr WidgetId kBuyVehicle{ "buy_vehicle" };
        constexpr WidgetId kAddSlot{ "add_slot" };
        constexpr WidgetId kAutoUnbunch{ "auto_unbunch" };
        constexpr WidgetId kFilterAll{ "filter_all" };
        constexpr WidgetId kFilterRail{ "filter_rail" };
        constexpr WidgetId kFilterRoad{ "filter_road" };
        constexpr WidgetId kFilterTram{ "filter_tram" };
        constexpr WidgetId kFilterAir{ "filter_air" };
        constexpr WidgetId kFilterWater{ "filter_water" };
        constexpr WidgetId kColourPrimary{ "colour_primary" };
        constexpr WidgetId kColourSecondary{ "colour_secondary" };
        constexpr WidgetId kShowPath{ "show_path" };
        constexpr WidgetId kShowAllLines{ "show_all_lines" };
        constexpr WidgetId kPaintVehicles{ "paint_vehicles" };
        constexpr WidgetId kSchemes{ "schemes" };
    }

    static constexpr auto _widgets = makeWidgets(
        Widgets::Frame({ 0, 0 }, kWindowSize, WindowColour::primary),
        Widgets::Caption(Widx::kTitle, { 1, 1 }, { kWindowSize.width - 2, 13 }, Widgets::Caption::Style::colourText, WindowColour::primary, StringIds::line_manager_caption),
        Widgets::ImageButton(Widx::kClose, { kWindowSize.width - 15, 2 }, { 13, 13 }, WindowColour::primary, ImageIds::close_button, StringIds::tooltip_close_window),
        Widgets::Panel({ 0, 15 }, { kWindowSize.width, kWindowSize.height - 15 }, WindowColour::secondary),
        Widgets::ScrollView(Widx::kLines, { 4, 44 }, { 182, 148 }, WindowColour::secondary, Scrollbars::vertical),
        Widgets::ScrollView(Widx::kOrders, { 194, 50 }, { 192, 156 }, WindowColour::secondary, Scrollbars::vertical, StringIds::tooltip_route_scrollview),
        Widgets::Button(Widx::kCreate, { 4, 236 }, { 88, 15 }, WindowColour::secondary, StringIds::line_manager_new),
        Widgets::Button(Widx::kRemove, { 96, 236 }, { 90, 15 }, WindowColour::secondary, StringIds::line_manager_delete),
        Widgets::Button(Widx::kLoop, { 194, 236 }, { 70, 15 }, WindowColour::secondary, StringIds::line_manager_loop),
        Widgets::ImageButton(Widx::kOrderForceUnload, { 392, 50 }, { 24, 24 }, WindowColour::secondary, ImageIds::route_force_unload, StringIds::tooltip_route_insert_force_unload),
        Widgets::ImageButton(Widx::kOrderWait, { 392, 74 }, { 24, 24 }, WindowColour::secondary, ImageIds::route_wait, StringIds::tooltip_route_insert_wait_full_cargo),
        Widgets::ImageButton(Widx::kOrderDelete, { 392, 98 }, { 24, 24 }, WindowColour::secondary, ImageIds::route_delete, StringIds::tooltip_route_delete_order),
        Widgets::ImageButton(Widx::kOrderUp, { 392, 122 }, { 24, 12 }, WindowColour::secondary, ImageIds::red_arrow_up, StringIds::tooltip_route_move_order_up),
        Widgets::ImageButton(Widx::kOrderDown, { 392, 134 }, { 24, 12 }, WindowColour::secondary, ImageIds::red_arrow_down, StringIds::tooltip_route_move_order_down),
        makeWidget({ 0, 0 }, { 1, 1 }, WidgetType::empty, WindowColour::primary),
        Widgets::Button(Widx::kTabOrders, { 194, 18 }, { 90, 14 }, WindowColour::secondary, StringIds::line_tab_orders),
        Widgets::Button(Widx::kTabVehicles, { 286, 18 }, { 100, 14 }, WindowColour::secondary, StringIds::line_tab_vehicles),
        Widgets::Button(Widx::kLocalMode, { 194, 34 }, { 100, 12 }, WindowColour::secondary, StringIds::local_mode_button),
        Widgets::Button(Widx::kExpressMode, { 296, 34 }, { 90, 12 }, WindowColour::secondary, StringIds::express_mode_button),
        Widgets::Button(Widx::kAddVehicle, { 266, 236 }, { 70, 15 }, WindowColour::secondary, StringIds::line_add_vehicle),
        Widgets::Button(Widx::kBuyVehicle, { 338, 236 }, { 76, 15 }, WindowColour::secondary, StringIds::line_buy_vehicle),
        Widgets::Button(Widx::kAddSlot, { 266, 236 }, { 70, 15 }, WindowColour::secondary, StringIds::line_add_slot),
        Widgets::Button(Widx::kAutoUnbunch, { 338, 236 }, { 76, 15 }, WindowColour::secondary, StringIds::line_auto_unbunch),
        Widgets::Tab(Widx::kFilterAll, { 3, 15 }, { 31, 27 }, WindowColour::secondary, ImageIds::tab),
        Widgets::Tab(Widx::kFilterRail, { 34, 15 }, { 31, 27 }, WindowColour::secondary, ImageIds::tab, StringIds::train),
        Widgets::Tab(Widx::kFilterRoad, { 65, 15 }, { 31, 27 }, WindowColour::secondary, ImageIds::tab, StringIds::bus),
        Widgets::Tab(Widx::kFilterTram, { 96, 15 }, { 31, 27 }, WindowColour::secondary, ImageIds::tab, StringIds::tram),
        Widgets::Tab(Widx::kFilterAir, { 127, 15 }, { 31, 27 }, WindowColour::secondary, ImageIds::tab, StringIds::aircraft),
        Widgets::Tab(Widx::kFilterWater, { 158, 15 }, { 31, 27 }, WindowColour::secondary, ImageIds::tab, StringIds::ship),
        Widgets::ColourButton(Widx::kColourPrimary, { 4, 196 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_main_colour),
        Widgets::ColourButton(Widx::kColourSecondary, { 20, 196 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_secondary_colour),
        Widgets::Button(Widx::kShowPath, { 36, 196 }, { 46, 14 }, WindowColour::secondary, StringIds::line_show_path),
        Widgets::Button(Widx::kShowAllLines, { 84, 196 }, { 40, 14 }, WindowColour::secondary, StringIds::line_show_all),
        Widgets::Button(Widx::kPaintVehicles, { 126, 196 }, { 60, 14 }, WindowColour::secondary, StringIds::line_paint_vehicles),
        Widgets::Button(Widx::kSchemes, { 4, 214 }, { 70, 14 }, WindowColour::secondary, StringIds::line_colour_schemes)

    );

    static const WindowEventList& getEvents();

    static const std::array<StringId, 6> kOrderString = {
        StringIds::orders_end,
        StringIds::orders_stop_at,
        StringIds::orders_route_through,
        StringIds::orders_route_thought_waypoint,
        StringIds::orders_unload_all,
        StringIds::orders_wait_for_full_load_of,
    };

    static const Lines::Line* selectedLine()
    {
        return Lines::findLine(_selectedLine);
    }

    static bool ordersTab(const Ui::Window& window)
    {
        return window.currentTab == 0;
    }

    static void drawLineDiagram(Ui::Window& window, Gfx::DrawingContext& drawingCtx);
    static void drawFilterIcons(Ui::Window& window, Gfx::DrawingContext& drawingCtx, Gfx::TextRenderer& tr);

    static void clearDropdownUserStrings()
    {
        for (const auto id : _dropdownUserStrings)
        {
            if (StringManager::isUserString(id))
            {
                StringManager::emptyUserString(id);
            }
        }
        _dropdownUserStrings.clear();
    }

    static void openRename(Ui::Window& window)
    {
        const auto* line = selectedLine();
        if (line == nullptr)
        {
            return;
        }
        char* buffer = StringManager::getBufferString(StringIds::buffer_2039);
        std::strncpy(buffer, line->name.c_str(), StringManager::kUserStringSize - 1);
        buffer[StringManager::kUserStringSize - 1] = '\0';
        TextInput::openTextInput(&window, StringIds::line_rename_title, StringIds::line_rename_prompt, StringIds::buffer_2039, widx::title, {});
    }

    static bool numberedOrder(Vehicles::OrderType type)
    {
        return type == Vehicles::OrderType::StopAt || type == Vehicles::OrderType::RouteThrough || type == Vehicles::OrderType::RouteWaypoint;
    }

    static currency32_t monthlyProfit(const Vehicles::VehicleHead& head)
    {
        Vehicles::Vehicle train(head);
        if (train.veh2 == nullptr)
        {
            return 0;
        }
        return train.veh2->totalRecentProfit() / 4;
    }

    static void refreshAssignedVehicles()
    {
        const auto* line = selectedLine();
        if (line == nullptr)
        {
            return;
        }
        for (const auto vehicleId : line->vehicles)
        {
            if (auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId)))
            {
                Vehicles::OrderManager::generateNumDisplayFrames(head);
            }
            WindowManager::invalidateOrderPageByVehicleNumber(vehicleId);
        }
    }

    static size_t insertIndex()
    {
        const auto* line = selectedLine();
        if (line == nullptr)
        {
            return 0;
        }
        if (_selectedOrder < 0 || static_cast<size_t>(_selectedOrder) > line->orders.size())
        {
            return line->orders.size();
        }
        return static_cast<size_t>(_selectedOrder);
    }

    static bool lineVisible(const Lines::Line& line)
    {
        if (_filter == Lines::kEveryLineKind)
        {
            return true;
        }
        if (line.kinds == Lines::kEveryLineKind)
        {
            return false;
        }
        return (line.kinds & _filter) != Lines::LineKind::none;
    }

    static std::vector<uint16_t> visibleLineIds()
    {
        std::vector<uint16_t> ids;
        for (const auto& line : Lines::allLines())
        {
            if (lineVisible(line))
            {
                ids.push_back(line.id);
            }
        }
        return ids;
    }

    static void insertLineOrder(Ui::Window& window, Lines::LineOrder order, Lines::LineKind stopKinds)
    {
        const auto* line = selectedLine();
        if (line == nullptr)
        {
            return;
        }
        if (stopKinds != Lines::LineKind::none && !Lines::stopFits(line->id, stopKinds))
        {
            window.invalidate();
            return;
        }
        const auto before = line->orders.size();
        const auto lineId = line->id;
        const auto added = Lines::insertOrder(lineId, insertIndex(), std::move(order));
        if (!added.ok)
        {
            window.invalidate();
            return;
        }
        if (stopKinds != Lines::LineKind::none)
        {
            Lines::acceptStopKinds(lineId, stopKinds);
        }
        Lines::reconcileLineMode(lineId);
        const auto* updated = Lines::findLine(lineId);
        if (updated != nullptr && _selectedOrder >= 0 && updated->orders.size() > before)
        {
            _selectedOrder++;
        }
        _selectedLine = lineId;
        refreshAssignedVehicles();
        window.invalidate();
    }

    static bool stationOwned(StationId stationId)
    {
        const auto* station = StationManager::get(stationId);
        return station != nullptr && station->owner == CompanyManager::getControllingId();
    }

    static ViewportInteraction::InteractionArg stationInteraction(ViewportInteraction::InteractionArg interaction, StationId stationId)
    {
        if (!stationOwned(stationId))
        {
            return ViewportInteraction::kNoInteractionArg;
        }
        Input::setHoveredStationId(stationId);
        World::setMapSelectionFlags(World::MapSelectionFlags::hoveringOverStation);
        if (auto* station = StationManager::get(stationId))
        {
            ViewportManager::invalidate(station);
            auto args = FormatArguments::mapToolTip(StringIds::click_to_insert_new_order_stop_at);
            args.push(station->name);
            args.push(station->town);
        }
        return ViewportInteraction::InteractionArg(interaction.pos, enumValue(stationId), ViewportInteraction::InteractionItem::stationLabel, interaction.modId);
    }

    static ViewportInteraction::InteractionArg pick(TransportMode mode, const int16_t x, const int16_t y)
    {
        auto blocked = ViewportInteraction::InteractionItemFlags::track | ViewportInteraction::InteractionItemFlags::roadAndTram | ViewportInteraction::InteractionItemFlags::water | ViewportInteraction::InteractionItemFlags::station | ViewportInteraction::InteractionItemFlags::stationLabel;
        (void)mode;
        auto [interaction, viewport] = ViewportInteraction::getMapCoordinatesFromPos(x, y, ~blocked);
        switch (interaction.type)
        {
            case ViewportInteraction::InteractionItem::stationLabel:
                return stationInteraction(interaction, static_cast<StationId>(interaction.value));

            case ViewportInteraction::InteractionItem::trainStation:
            case ViewportInteraction::InteractionItem::roadStation:
            case ViewportInteraction::InteractionItem::airport:
            case ViewportInteraction::InteractionItem::dock:
            {
                auto* entry = static_cast<World::TileElementEntry*>(interaction.object);
                auto* stationElement = entry == nullptr ? nullptr : entry->as<World::StationElement>();
                if (stationElement == nullptr || stationElement->isAiAllocated() || stationElement->isGhost() || stationElement->owner() != CompanyManager::getControllingId())
                {
                    return ViewportInteraction::kNoInteractionArg;
                }
                return stationInteraction(interaction, stationElement->stationId());
            }

            case ViewportInteraction::InteractionItem::track:
            {
                auto* entry = static_cast<World::TileElementEntry*>(interaction.object);
                auto* track = entry == nullptr ? nullptr : entry->as<World::TrackElement>();
                if (track == nullptr || track->isAiAllocated() || track->isGhost() || track->owner() != CompanyManager::getControllingId())
                {
                    return ViewportInteraction::kNoInteractionArg;
                }
                if (track->hasStationElement())
                {
                    auto* next = entry->next();
                    auto* stationElement = next == nullptr ? nullptr : next->as<World::StationElement>();
                    if (stationElement != nullptr && !stationElement->isAiAllocated() && !stationElement->isGhost())
                    {
                        return stationInteraction(interaction, stationElement->stationId());
                    }
                }
                FormatArguments::mapToolTip(StringIds::click_to_insert_new_order_route_through);
                return interaction;
            }

            case ViewportInteraction::InteractionItem::road:
            {
                auto* entry = static_cast<World::TileElementEntry*>(interaction.object);
                auto* road = entry == nullptr ? nullptr : entry->as<World::RoadElement>();
                if (road == nullptr || road->isAiAllocated() || road->isGhost() || road->owner() != CompanyManager::getControllingId())
                {
                    return ViewportInteraction::kNoInteractionArg;
                }
                if (road->hasStationElement())
                {
                    auto* stationElement = getStationElement({ interaction.pos, road->baseHeight() });
                    if (stationElement != nullptr && !stationElement->isAiAllocated() && !stationElement->isGhost())
                    {
                        return stationInteraction(interaction, stationElement->stationId());
                    }
                }
                FormatArguments::mapToolTip(StringIds::click_to_insert_new_order_route_through);
                return interaction;
            }

            case ViewportInteraction::InteractionItem::water:
                FormatArguments::mapToolTip(StringIds::click_to_insert_new_order_route_through);
                return interaction;

            default:
                return ViewportInteraction::kNoInteractionArg;
        }
    }

    static Lines::LineOrder waypointOrder(const World::TilePos2& pos, uint8_t baseZ, uint8_t direction, uint8_t trackId)
    {
        Lines::LineOrder order;
        order.type = Vehicles::OrderType::RouteWaypoint;
        order.tileX = pos.x;
        order.tileY = pos.y;
        order.baseZ = baseZ;
        order.direction = direction;
        order.trackId = trackId;
        return order;
    }

    static void onClose(Ui::Window& self)
    {
        if (ToolManager::isToolActive(self.type, self.number))
        {
            ToolManager::toolCancel();
        }
        clearDropdownUserStrings();
        Lines::clearRouteView();
    }

    static Vehicles::VehicleHead* routeToolVehicle()
    {
        auto* toolWindow = ToolManager::toolGetActiveWindow();
        if (toolWindow == nullptr || toolWindow->type != WindowType::vehicle || toolWindow->currentTab != 4)
        {
            return nullptr;
        }
        auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(toolWindow->number));
        if (head == nullptr || !head->isVehicleHead())
        {
            return nullptr;
        }
        return head;
    }

    static void onUpdate(Ui::Window& self)
    {
        if (!ordersTab(self))
        {
            if (ToolManager::isToolActive(self.type, self.number))
            {
                ToolManager::toolCancel();
            }
            self.invalidate();
            return;
        }
        if (!WindowManager::isInFront(&self))
        {
            return;
        }
        if (routeToolVehicle() != nullptr)
        {
            return;
        }
        if (!ToolManager::isToolActive(self.type, self.number))
        {
            ToolManager::toolSet(self, widx::tool, CursorId::crosshair);
        }
    }

    static void onToolAbort(Ui::Window& self, [[maybe_unused]] const WidgetIndex_t widgetIndex, [[maybe_unused]] const WidgetId id)
    {
        self.invalidate();
        World::resetMapSelectionFlag(World::MapSelectionFlags::unk_04);
        Gfx::invalidateScreen();
    }

    static Ui::CursorId toolCursor([[maybe_unused]] Ui::Window& self, const int16_t x, const int16_t y, const Ui::CursorId fallback, bool& out)
    {
        const auto* line = selectedLine();
        if (line == nullptr)
        {
            out = false;
            return fallback;
        }
        const auto interaction = pick(line->mode, x, y);
        out = interaction.type != ViewportInteraction::InteractionItem::noInteraction;
        return out ? CursorId::inwardArrows : fallback;
    }

    static void onToolDown(Ui::Window& self, [[maybe_unused]] const WidgetIndex_t widgetIndex, [[maybe_unused]] const WidgetId id, const int16_t x, const int16_t y)
    {
        const auto* line = selectedLine();
        if (line == nullptr)
        {
            return;
        }
        const auto interaction = pick(line->mode, x, y);
        switch (interaction.type)
        {
            case ViewportInteraction::InteractionItem::stationLabel:
            {
                Audio::playSound(Audio::SoundId::waypoint, Audio::ChannelId::ui, Input::getDragLastLocation().x);
                Lines::LineOrder order;
                order.type = Vehicles::OrderType::StopAt;
                order.station = static_cast<uint16_t>(interaction.value);
                order.departure = Lines::DepartureRule::loadIfAvailable;
                insertLineOrder(self, std::move(order), Lines::stationKinds(order.station));
                break;
            }
            case ViewportInteraction::InteractionItem::track:
            {
                auto* entry = static_cast<World::TileElementEntry*>(interaction.object);
                auto* track = entry == nullptr ? nullptr : entry->as<World::TrackElement>();
                if (track == nullptr)
                {
                    break;
                }
                const auto& piece = World::TrackData::getTrackPiece(track->trackId());
                if (track->sequenceIndex() >= piece.size())
                {
                    break;
                }
                const auto& part = piece[track->sequenceIndex()];
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, track->rotation());
                const auto firstTile = interaction.pos - offset;
                auto height = static_cast<int32_t>(track->baseHeight()) - part.z;
                Audio::playSound(Audio::SoundId::waypoint, Audio::ChannelId::ui, Input::getDragLastLocation().x);
                insertLineOrder(self, waypointOrder(World::toTileSpace(firstTile), static_cast<uint8_t>(height / 8), track->rotation(), track->trackId()), Lines::LineKind::rail);
                break;
            }
            case ViewportInteraction::InteractionItem::road:
            {
                auto* entry = static_cast<World::TileElementEntry*>(interaction.object);
                auto* road = entry == nullptr ? nullptr : entry->as<World::RoadElement>();
                if (road == nullptr)
                {
                    break;
                }
                const auto& piece = World::TrackData::getRoadPiece(road->roadId());
                if (road->sequenceIndex() >= piece.size())
                {
                    break;
                }
                const auto& part = piece[road->sequenceIndex()];
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, road->rotation());
                const auto firstTile = interaction.pos - offset;
                auto height = static_cast<int32_t>(road->baseHeight()) - part.z;
                Audio::playSound(Audio::SoundId::waypoint, Audio::ChannelId::ui, Input::getDragLastLocation().x);
                insertLineOrder(self, waypointOrder(World::toTileSpace(firstTile), static_cast<uint8_t>(height / 8), road->rotation(), road->roadId()), Lines::roadObjectKind(road->roadObjectId()));
                break;
            }
            case ViewportInteraction::InteractionItem::water:
            {
                const auto heights = World::TileManager::getHeight(interaction.pos);
                auto height = heights.landHeight;
                if (heights.waterHeight != 0)
                {
                    height = heights.waterHeight;
                }
                Audio::playSound(Audio::SoundId::waypoint, Audio::ChannelId::ui, Input::getDragLastLocation().x);
                insertLineOrder(self, waypointOrder(World::toTileSpace(interaction.pos), static_cast<uint8_t>(height / 8), 0, 0), Lines::LineKind::water);
                break;
            }
            default:
                break;
        }
    }

    static void showCargoDropdown(Ui::Window& self, WidgetIndex_t widgetIndex, StringId caption)
    {
        _cargoIds.clear();
        auto index = 0;
        ObjectManager::forEachLoaded<CargoObject>([&](LoadedObjectId id, const CargoObject& cargo) {
            FormatArguments args{};
            args.push(cargo.name);
            args.push(cargo.unitInlineSprite);
            Dropdown::add(index, caption, args);
            _cargoIds.push_back(static_cast<uint8_t>(id));
            index++;
        });
        if (index == 0)
        {
            return;
        }
        const auto& widget = self.widgets[widgetIndex];
        Dropdown::showText(self.x + widget.left, self.y + widget.top, widget.width(), widget.height(), self.getColour(WindowColour::secondary), index, 0);
        Dropdown::setHighlightedItem(0);
    }

    static void showAddVehicleDropdown(Ui::Window& self, WidgetIndex_t widgetIndex)
    {
        const auto* line = selectedLine();
        if (line == nullptr)
        {
            return;
        }
        clearDropdownUserStrings();
        _vehicleChoices.clear();
        auto index = 0;
        for (auto* head : EntityManager::EntityList<EntityManager::EntityListIterator<Vehicles::VehicleHead>, EntityManager::EntityListType::vehicleHead>())
        {
            if (head == nullptr || head->owner != CompanyManager::getControllingId() || !Lines::lineAcceptsVehicle(*line, head->vehicleType, head->mode))
            {
                continue;
            }
            if ((head->var_38 & Vehicles::Flags38::isGhost) != Vehicles::Flags38::none)
            {
                continue;
            }
            if (const auto* assigned = Lines::lineForVehicle(enumValue(head->id)); assigned != nullptr && assigned->id == line->id)
            {
                continue;
            }
            _vehicleChoices.push_back(enumValue(head->id));
            FormatArguments args{};
            args.push(head->ordinalNumber);
            Dropdown::add(index, head->name, args);
            index++;
        }
        if (index == 0)
        {
            return;
        }
        const auto& widget = self.widgets[widgetIndex];
        Dropdown::showText(self.x + widget.left, self.y + widget.top, widget.width(), widget.height(), self.getColour(WindowColour::secondary), index, 0);
        Dropdown::setHighlightedItem(0);
    }

    static void onMouseDown(Ui::Window& self, WidgetIndex_t widgetIndex, const WidgetId id)
    {
        if (id == Widx::kAddVehicle)
        {
            showAddVehicleDropdown(self, widgetIndex);
            return;
        }
        if (id == Widx::kBuyVehicle)
        {
            const auto* line = selectedLine();
            if (line == nullptr)
            {
                return;
            }
            _buyTypes.clear();
            const std::pair<VehicleType, TransportMode> choices[] = {
                { VehicleType::train, TransportMode::rail },
                { VehicleType::bus, TransportMode::road },
                { VehicleType::truck, TransportMode::road },
                { VehicleType::tram, TransportMode::road },
                { VehicleType::aircraft, TransportMode::air },
                { VehicleType::ship, TransportMode::water },
            };
            for (const auto [type, mode] : choices)
            {
                if (Lines::lineAcceptsVehicle(*line, type, mode))
                {
                    _buyTypes.push_back(type);
                }
            }
            if (_buyTypes.empty())
            {
                return;
            }
            if (_buyTypes.size() == 1)
            {
                Lines::armPurchase(line->id);
                BuildVehicle::openByType(_buyTypes[0]);
                return;
            }
            for (size_t i = 0; i < _buyTypes.size(); ++i)
            {
                Dropdown::add(static_cast<int16_t>(i), StringIds::getVehicleType(_buyTypes[i]));
            }
            const auto& widget = self.widgets[widgetIndex];
            Dropdown::showText(self.x + widget.left, self.y + widget.top, widget.width(), widget.height(), self.getColour(WindowColour::secondary), static_cast<int16_t>(_buyTypes.size()), 0);
            Dropdown::setHighlightedItem(0);
            return;
        }
        if (id == Widx::kColourPrimary || id == Widx::kColourSecondary)
        {
            _colourSecondary = id == Widx::kColourSecondary;
            const auto* line = selectedLine();
            const auto selected = line == nullptr ? Colour::grey : static_cast<Colour>(_colourSecondary ? line->secondaryColour : line->colour);
            Dropdown::showColour(&self, &self.widgets[widgetIndex], 0x7FFFFFFF, selected, self.getColour(WindowColour::secondary));
            return;
        }
        if (id == Widx::kOrderForceUnload)
        {
            showCargoDropdown(self, widgetIndex, StringIds::orders_unload_all2);
            return;
        }
        if (id == Widx::kOrderWait)
        {
            showCargoDropdown(self, widgetIndex, StringIds::orders_wait_for_full_load_of2);
        }
    }

    static void onDropdown(Ui::Window& self, [[maybe_unused]] WidgetIndex_t widgetIndex, const WidgetId id, int16_t itemIndex)
    {
        const auto item = itemIndex == -1 ? Dropdown::getHighlightedItem() : itemIndex;
        if (item < 0)
        {
            return;
        }
        if (id == Widx::kColourPrimary || id == Widx::kColourSecondary)
        {
            const auto* line = selectedLine();
            if (line == nullptr)
            {
                return;
            }
            const auto chosen = static_cast<uint8_t>(Dropdown::getItemArgument(static_cast<uint8_t>(item), 2));
            const auto primary = id == Widx::kColourPrimary ? chosen : line->colour;
            const auto secondary = id == Widx::kColourSecondary ? chosen : line->secondaryColour;
            Lines::setLineColours(line->id, primary, secondary);
            self.invalidate();
            return;
        }
        if (id == Widx::kAddVehicle)
        {
            if (item < 0 || static_cast<size_t>(item) >= _vehicleChoices.size() || selectedLine() == nullptr)
            {
                return;
            }
            const auto* line = selectedLine();
            Lines::assignVehicle(line->id, _vehicleChoices[static_cast<size_t>(item)], line->mode);
            refreshAssignedVehicles();
            self.invalidate();
            return;
        }
        if (id == Widx::kBuyVehicle)
        {
            const auto* line = selectedLine();
            if (line == nullptr || item < 0 || static_cast<size_t>(item) >= _buyTypes.size())
            {
                return;
            }
            Lines::armPurchase(line->id);
            BuildVehicle::openByType(_buyTypes[static_cast<size_t>(item)]);
            return;
        }
        if (item >= static_cast<int16_t>(_cargoIds.size()))
        {
            return;
        }
        Lines::LineOrder order;
        order.cargo = _cargoIds[static_cast<size_t>(item)];
        if (id == Widx::kOrderForceUnload)
        {
            order.type = Vehicles::OrderType::UnloadAll;
        }
        else if (id == Widx::kOrderWait)
        {
            order.type = Vehicles::OrderType::WaitFor;
        }
        else
        {
            return;
        }
        insertLineOrder(self, std::move(order), Lines::LineKind::none);
    }

    static void onMouseUp(Ui::Window& window, [[maybe_unused]] WidgetIndex_t widgetIndex, const WidgetId id)
    {
        if (id == Widx::kClose)
        {
            WindowManager::close(window.type);
            return;
        }
        if (id == Widx::kTitle)
        {
            openRename(window);
            return;
        }
        if (id == Widx::kTabOrders)
        {
            window.currentTab = 0;
            window.invalidate();
            return;
        }
        if (id == Widx::kTabVehicles)
        {
            window.currentTab = 1;
            window.invalidate();
            return;
        }
        if (id == Widx::kShowPath)
        {
            _showPath = !_showPath;
            window.invalidate();
            return;
        }
        if (id == Widx::kShowAllLines)
        {
            _showAllLines = !_showAllLines;
            window.invalidate();
            return;
        }
        if (id == Widx::kPaintVehicles)
        {
            if (const auto* line = selectedLine())
            {
                Lines::setPaintVehicles(line->id, !line->paintVehicles);
            }
            window.invalidate();
            return;
        }
        if (id == Widx::kSchemes)
        {
            if (const auto* line = selectedLine())
            {
                LineSchemes::open(line->id);
            }
            return;
        }
        if (id == Widx::kCreate)
        {
            const auto* company = CompanyManager::get(CompanyManager::getControllingId());
            const auto primary = company == nullptr ? static_cast<uint8_t>(Colour::grey) : static_cast<uint8_t>(company->mainColours.primary);
            const auto secondary = company == nullptr ? static_cast<uint8_t>(Colour::white) : static_cast<uint8_t>(company->mainColours.secondary);
            const auto created = Lines::createCompanyLine({}, TransportMode::rail, primary, secondary);
            if (created.ok)
            {
                _selectedLine = created.id;
                _selectedOrder = -1;
            }
            window.invalidate();
            return;
        }
        if (id == Widx::kFilterAll || id == Widx::kFilterRail || id == Widx::kFilterRoad || id == Widx::kFilterTram || id == Widx::kFilterAir || id == Widx::kFilterWater)
        {
            _filter = id == Widx::kFilterRail ? Lines::LineKind::rail : id == Widx::kFilterRoad ? Lines::LineKind::road
                : id == Widx::kFilterTram                                                      ? Lines::LineKind::tram
                : id == Widx::kFilterAir                                                       ? Lines::LineKind::air
                : id == Widx::kFilterWater                                                     ? Lines::LineKind::water
                                                                                               : Lines::kEveryLineKind;
            _lineHover = -1;
            window.invalidate();
            return;
        }
        const auto* line = selectedLine();
        if (line == nullptr)
        {
            return;
        }
        if (id == Widx::kRemove)
        {
            WindowManager::close(WindowType::lineColourSchemes, line->id);
            Lines::deleteLine(line->id);
            _selectedLine = 0;
            _selectedOrder = -1;
            if (!Lines::allLines().empty())
            {
                _selectedLine = Lines::allLines().front().id;
            }
            window.invalidate();
            return;
        }
        if (id == Widx::kLoop)
        {
            Lines::setLoop(line->id, !line->loop);
            window.invalidate();
            return;
        }
        if (id == Widx::kLocalMode)
        {
            Lines::setExpress(line->id, false);
            window.invalidate();
            return;
        }
        if (id == Widx::kExpressMode)
        {
            Lines::setExpress(line->id, true);
            window.invalidate();
            return;
        }
        if (id == Widx::kAutoUnbunch)
        {
            const auto roundTrip = line->roundTripMinutes == 0 ? static_cast<uint16_t>(60) : line->roundTripMinutes;
            Lines::setTimetable(line->id, !line->autoUnbunch, line->unbunchGapMinutes, roundTrip);
            window.invalidate();
            return;
        }
        if (id == Widx::kAddSlot)
        {
            if (_selectedOrder < 0 || static_cast<size_t>(_selectedOrder) >= line->orders.size())
            {
                return;
            }
            auto order = line->orders[static_cast<size_t>(_selectedOrder)];
            if (!numberedOrder(order.type))
            {
                return;
            }
            auto minute = static_cast<uint16_t>(((Lines::timetableMinute(ScenarioManager::getScenarioTicks()) / 10) + 1) * 10);
            if (minute >= 24 * 60)
            {
                minute = 0;
            }
            order.slots.push_back({ 0, minute });
            Lines::setStopSettings(line->id, static_cast<size_t>(_selectedOrder), order.departure, order.minWait, order.maxWait, order.platform, order.filters, order.slots);
            window.invalidate();
            return;
        }
        if (id == Widx::kOrderDelete)
        {
            if (line->orders.empty())
            {
                return;
            }
            const auto index = _selectedOrder < 0 || static_cast<size_t>(_selectedOrder) >= line->orders.size() ? line->orders.size() - 1 : static_cast<size_t>(_selectedOrder);
            Lines::removeOrder(line->id, index);
            const auto* updated = Lines::findLine(line->id);
            if (updated == nullptr || _selectedOrder < 0 || static_cast<size_t>(_selectedOrder) >= updated->orders.size())
            {
                _selectedOrder = -1;
            }
            refreshAssignedVehicles();
            window.invalidate();
            return;
        }
        if (id == Widx::kOrderUp && _selectedOrder > 0)
        {
            if (Lines::moveOrder(line->id, static_cast<size_t>(_selectedOrder), -1).ok)
            {
                _selectedOrder--;
            }
            refreshAssignedVehicles();
            window.invalidate();
            return;
        }
        if (id == Widx::kOrderDown && _selectedOrder >= 0)
        {
            if (Lines::moveOrder(line->id, static_cast<size_t>(_selectedOrder), 1).ok)
            {
                _selectedOrder++;
            }
            refreshAssignedVehicles();
            window.invalidate();
        }
    }

    static void prepareDraw(Ui::Window& window)
    {
        const auto* line = selectedLine();
        const bool showingOrders = ordersTab(window);
        window.activatedWidgets &= ~((1ULL << widx::loop) | (1ULL << widx::localMode) | (1ULL << widx::expressMode) | (1ULL << widx::tabOrders) | (1ULL << widx::tabVehicles) | (1ULL << widx::filterAll) | (1ULL << widx::filterRail) | (1ULL << widx::filterRoad) | (1ULL << widx::filterTram) | (1ULL << widx::filterAir) | (1ULL << widx::filterWater));
        window.activatedWidgets |= showingOrders ? (1ULL << widx::tabOrders) : (1ULL << widx::tabVehicles);
        const auto filterWidget = _filter == Lines::LineKind::rail ? widx::filterRail : _filter == Lines::LineKind::road ? widx::filterRoad
            : _filter == Lines::LineKind::tram                                                                          ? widx::filterTram
            : _filter == Lines::LineKind::air                                                                           ? widx::filterAir
            : _filter == Lines::LineKind::water                                                                         ? widx::filterWater
                                                                                                                        : widx::filterAll;
        window.activatedWidgets |= 1ULL << filterWidget;
        window.disabledWidgets &= ~((1ULL << widx::remove) | (1ULL << widx::loop) | (1ULL << widx::orderDelete) | (1ULL << widx::orderUp) | (1ULL << widx::orderDown) | (1ULL << widx::orderForceUnload) | (1ULL << widx::orderWait) | (1ULL << widx::localMode) | (1ULL << widx::expressMode) | (1ULL << widx::addVehicle) | (1ULL << widx::buyVehicle));
        if (line == nullptr)
        {
            window.disabledWidgets |= (1ULL << widx::remove) | (1ULL << widx::loop) | (1ULL << widx::orderDelete) | (1ULL << widx::orderUp) | (1ULL << widx::orderDown) | (1ULL << widx::orderForceUnload) | (1ULL << widx::orderWait) | (1ULL << widx::localMode) | (1ULL << widx::expressMode) | (1ULL << widx::addVehicle) | (1ULL << widx::buyVehicle);
        }
        else
        {
            if (line->loop)
            {
                window.activatedWidgets |= 1ULL << widx::loop;
            }
            if (line->express)
            {
                window.activatedWidgets |= 1ULL << widx::expressMode;
            }
            else
            {
                window.activatedWidgets |= 1ULL << widx::localMode;
            }
        }
        window.widgets[widx::loop].text = line != nullptr && line->loop ? StringIds::line_manager_loop : StringIds::line_manager_return;
        if (line == nullptr || line->orders.empty())
        {
            window.disabledWidgets |= (1ULL << widx::orderDelete);
        }
        if (_selectedOrder < 0)
        {
            window.disabledWidgets |= (1ULL << widx::orderUp) | (1ULL << widx::orderDown);
        }

        window.widgets[widx::localMode].hidden = !showingOrders;
        window.widgets[widx::expressMode].hidden = !showingOrders;
        window.widgets[widx::orderForceUnload].hidden = !showingOrders;
        window.widgets[widx::orderWait].hidden = !showingOrders;
        window.widgets[widx::orderDelete].hidden = !showingOrders;
        window.widgets[widx::orderUp].hidden = !showingOrders;
        window.widgets[widx::orderDown].hidden = !showingOrders;
        window.widgets[widx::addVehicle].hidden = showingOrders || line == nullptr;
        window.widgets[widx::buyVehicle].hidden = showingOrders || line == nullptr;
        window.widgets[widx::addSlot].hidden = !showingOrders || line == nullptr;
        window.widgets[widx::autoUnbunch].hidden = !showingOrders || line == nullptr;
        window.activatedWidgets &= ~(1ULL << widx::autoUnbunch);
        if (line != nullptr && line->autoUnbunch)
        {
            window.activatedWidgets |= 1ULL << widx::autoUnbunch;
        }
        if (_selectedOrder < 0 || line == nullptr || static_cast<size_t>(_selectedOrder) >= line->orders.size() || !numberedOrder(line->orders[static_cast<size_t>(_selectedOrder)].type))
        {
            window.disabledWidgets |= 1ULL << widx::addSlot;
        }
        else
        {
            window.disabledWidgets &= ~(1ULL << widx::addSlot);
        }
        for (const auto& existing : Lines::allLines())
        {
            Lines::reconcileLineMode(existing.id);
        }
        if (line != nullptr && line->kinds == Lines::kEveryLineKind)
        {
            window.disabledWidgets |= (1ULL << widx::addVehicle) | (1ULL << widx::buyVehicle);
        }
        window.widgets[widx::orders].right = showingOrders ? 386 : kWindowSize.width - 8;
        window.widgets[widx::orders].top = showingOrders ? 50 : 84;
        window.widgets[widx::orders].bottom = 205;
        window.activatedWidgets &= ~((1ULL << widx::showPath) | (1ULL << widx::showAllLines) | (1ULL << widx::paintVehicles));
        if (_showPath)
        {
            window.activatedWidgets |= 1ULL << widx::showPath;
        }
        if (_showAllLines)
        {
            window.activatedWidgets |= 1ULL << widx::showAllLines;
        }
        if (line != nullptr && line->paintVehicles)
        {
            window.activatedWidgets |= 1ULL << widx::paintVehicles;
        }
        const auto primary = line == nullptr ? Colour::grey : static_cast<Colour>(line->colour);
        const auto secondary = line == nullptr ? Colour::white : static_cast<Colour>(line->secondaryColour);
        window.widgets[widx::colourPrimary].image = Widget::kImageIdColourSet | ImageId::fromUInt32(ImageIds::colour_swatch_recolourable).withPrimary(primary).toUInt32();
        window.widgets[widx::colourSecondary].image = Widget::kImageIdColourSet | ImageId::fromUInt32(ImageIds::colour_swatch_recolourable).withPrimary(secondary).toUInt32();
        if (line == nullptr)
        {
            window.disabledWidgets |= (1ULL << widx::colourPrimary) | (1ULL << widx::colourSecondary) | (1ULL << widx::paintVehicles) | (1ULL << widx::schemes);
        }
        else
        {
            window.disabledWidgets &= ~((1ULL << widx::colourPrimary) | (1ULL << widx::colourSecondary) | (1ULL << widx::paintVehicles) | (1ULL << widx::schemes));
        }
        Lines::setRouteView(line == nullptr ? 0 : line->id, _showPath && line != nullptr, _showAllLines);
    }

    static void draw(Ui::Window& window, Gfx::DrawingContext& drawingCtx)
    {
        window.draw(drawingCtx);
        auto tr = Gfx::TextRenderer(drawingCtx);
        if (ordersTab(window))
        {
            const auto minute = Lines::timetableMinute(ScenarioManager::getScenarioTicks());
            FormatArguments args{};
            args.push(static_cast<uint16_t>(minute / 60));
            args.push(static_cast<uint16_t>(minute % 60));
            tr.drawStringLeft(Point(4, 214), Colour::black, StringIds::line_timetable_clock, args);
        }
        if (selectedLine() != nullptr && ToolManager::isToolActive(window.type, window.number))
        {
            tr.drawStringLeft(Point(194, 214), Colour::black, StringIds::route_click_on_waypoint);
        }
        if (!ordersTab(window))
        {
            drawLineDiagram(window, drawingCtx);
        }
        drawFilterIcons(window, drawingCtx, tr);
    }

    static uint32_t kindImageFrame(Lines::LineKind kind)
    {
        switch (kind)
        {
            case Lines::LineKind::rail:
                return InterfaceSkin::ImageIds::vehicle_train_frame_0;
            case Lines::LineKind::road:
                return InterfaceSkin::ImageIds::vehicle_buses_frame_0;
            case Lines::LineKind::tram:
                return InterfaceSkin::ImageIds::vehicle_trams_frame_0;
            case Lines::LineKind::air:
                return InterfaceSkin::ImageIds::vehicle_aircraft_frame_0;
            case Lines::LineKind::water:
                return InterfaceSkin::ImageIds::vehicle_ships_frame_0;
            default:
                return 0;
        }
    }

    static void drawKindImage(Gfx::DrawingContext& drawingCtx, const Ui::Rect& clip, uint32_t frame)
    {
        if (frame == 0)
        {
            return;
        }
        const auto* skin = ObjectManager::get<InterfaceSkinObject>();
        if (skin == nullptr || !drawingCtx.pushClip(clip))
        {
            return;
        }
        const auto colour = CompanyManager::getCompanyColour(CompanyManager::getControllingId());
        drawingCtx.drawImage(ZoomLevel::full, 0, 0, Gfx::recolour(skin->img + frame, colour));
        drawingCtx.popClip();
    }

    static void drawFilterIcons(Ui::Window& window, Gfx::DrawingContext& drawingCtx, Gfx::TextRenderer& tr)
    {
        const auto* skin = ObjectManager::get<InterfaceSkinObject>();
        const auto colour = CompanyManager::getCompanyColour(CompanyManager::getControllingId());
        const std::pair<WidgetIndex_t, uint32_t> filters[] = {
            { widx::filterRail, InterfaceSkin::ImageIds::vehicle_train_frame_0 },
            { widx::filterRoad, InterfaceSkin::ImageIds::vehicle_buses_frame_0 },
            { widx::filterTram, InterfaceSkin::ImageIds::vehicle_trams_frame_0 },
            { widx::filterAir, InterfaceSkin::ImageIds::vehicle_aircraft_frame_0 },
            { widx::filterWater, InterfaceSkin::ImageIds::vehicle_ships_frame_0 },
        };
        if (skin != nullptr)
        {
            for (const auto [index, frame] : filters)
            {
                Widget::drawTab(window, drawingCtx, Gfx::recolour(skin->img + frame, colour), index);
            }
        }
        const auto& all = window.widgets[widx::filterAll];
        FormatArguments args{};
        args.push(StringIds::line_name_raw);
        static constexpr char kAll[] = "All";
        args.push(kAll);
        const auto format = window.isActivated(widx::filterAll) ? StringIds::white_stringid : StringIds::black_stringid;
        tr.drawStringLeft(Point(static_cast<int16_t>(all.left + 4), static_cast<int16_t>(all.top + 8)), Colour::black, format, args);
    }

    static std::vector<Lines::StopPosition> stopPositions(const Lines::Line& line)
    {
        std::vector<Lines::StopPosition> positions;
        for (const auto& order : line.orders)
        {
            if (!numberedOrder(order.type))
            {
                continue;
            }
            Lines::StopPosition position;
            if (order.type == Vehicles::OrderType::RouteWaypoint)
            {
                position.x = static_cast<int32_t>(order.tileX) * World::kTileSize;
                position.y = static_cast<int32_t>(order.tileY) * World::kTileSize;
            }
            else if (const auto* station = StationManager::get(static_cast<StationId>(order.station)))
            {
                position.x = station->x;
                position.y = station->y;
            }
            positions.push_back(position);
        }
        return positions;
    }

    static Lines::LineProgress vehicleProgress(const Lines::Line& line, const Vehicles::VehicleHead& head, const std::vector<Lines::StopPosition>& positions)
    {
        Vehicles::Vehicle train(head);
        const auto* body = train.veh2 != nullptr && train.veh2->tileX != -1 ? static_cast<const Vehicles::VehicleBase*>(train.veh2) : static_cast<const Vehicles::VehicleBase*>(&head);
        const bool placed = body->tileX != -1;
        const bool atStation = head.status == Vehicles::Status::loading || head.status == Vehicles::Status::unloading;
        return Lines::progressAlongLine(line, head.currentOrder, static_cast<uint16_t>(head.stationId), atStation, placed ? body->tileX : 0, placed ? body->tileY : 0, placed ? positions : std::vector<Lines::StopPosition>{});
    }

    static void drawLineDiagram(Ui::Window& window, Gfx::DrawingContext& drawingCtx)
    {
        const auto* line = selectedLine();
        if (line == nullptr)
        {
            return;
        }
        std::vector<size_t> stops;
        for (size_t i = 0; i < line->orders.size(); ++i)
        {
            if (numberedOrder(line->orders[i].type))
            {
                stops.push_back(i);
            }
        }
        if (stops.empty())
        {
            return;
        }
        const auto left = window.widgets[widx::orders].left + 10;
        const auto right = window.widgets[widx::orders].right - 10;
        const auto y = 68;
        const auto width = std::max<int32_t>(1, right - left);
        // Always keep one equal gap after the last station. That gap is the trip back to station 1.
        const bool loop = stops.size() > 1;
        drawingCtx.fillRect(left, y, left + width, y + 2, PaletteIndex::black0, Gfx::RectFlags::none);
        for (size_t ordinal = 0; ordinal < stops.size() && ordinal < 63; ++ordinal)
        {
            const auto image = ImageIds::number_circle_01 + static_cast<uint32_t>(ordinal);
            const auto x = Lines::diagramStopX(left, width, stops.size(), loop, ordinal);
            drawingCtx.drawImage(ZoomLevel::full, x - 4, y - 4, Gfx::recolour(image, Colour::white));
        }
        const auto positions = stopPositions(*line);
        constexpr int kStackBuckets = 48;
        int pile[kStackBuckets] = {};
        for (const auto vehicleId : line->vehicles)
        {
            auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId));
            if (head == nullptr || head->sizeOfOrderTable == 0 || !head->isVehicleHead() || head->tileX == -1)
            {
                continue;
            }
            const auto progress = vehicleProgress(*line, *head, positions);
            const auto markerX = std::clamp(Lines::diagramMarkerX(left, width, stops.size(), loop, progress), left, left + width);
            const auto bucket = std::clamp((markerX - left) / 8, 0, kStackBuckets - 1);
            const auto stack = pile[bucket]++;
            auto colour = Colour::black;
            if (const auto* company = CompanyManager::get(head->owner))
            {
                colour = company->mainColours.primary;
            }
            const auto markerIndex = std::clamp<int32_t>(head->ordinalNumber <= 0 ? 0 : head->ordinalNumber - 1, 0, 62);
            const auto image = ImageIds::number_circle_01 + static_cast<uint32_t>(markerIndex);
            drawingCtx.drawImage(ZoomLevel::full, markerX - 4, y - 16 - stack * 5, Gfx::recolour(image, colour));
        }
    }

    static void drawOrderRow(Gfx::DrawingContext& drawingCtx, const Ui::Window& window, const Lines::LineOrder& order, int16_t y, int16_t row, int16_t& orderNumber)
    {
        auto tr = Gfx::TextRenderer(drawingCtx);
        const auto width = window.widgets[widx::orders].width();
        auto format = StringIds::black_stringid;
        if (_selectedOrder == row)
        {
            drawingCtx.fillRect(0, y, width, y + kRowHeight - 1, PaletteIndex::black0, Gfx::RectFlags::none);
            format = StringIds::white_stringid;
        }
        else if (_orderHover == row)
        {
            drawingCtx.fillRect(0, y, width, y + kRowHeight - 1, enumValue(ExtColour::unk30), Gfx::RectFlags::transparent);
            format = StringIds::wcolour2_stringid;
        }

        FormatArguments args{};
        const auto typeIndex = static_cast<uint8_t>(order.type);
        args.push(typeIndex < kOrderString.size() ? kOrderString[typeIndex] : StringIds::empty);
        if (order.type == Vehicles::OrderType::StopAt || order.type == Vehicles::OrderType::RouteThrough)
        {
            const auto stationId = static_cast<StationId>(order.station);
            if (StationManager::get(stationId) != nullptr)
            {
                if (order.type == Vehicles::OrderType::StopAt)
                {
                    Vehicles::OrderStopAt stop(stationId);
                    stop.setFormatArguments(args);
                }
                else
                {
                    Vehicles::OrderRouteThrough route(stationId);
                    route.setFormatArguments(args);
                }
            }
            else
            {
                args.push(StringIds::empty);
            }
        }
        else if (order.type == Vehicles::OrderType::UnloadAll || order.type == Vehicles::OrderType::WaitFor)
        {
            if (ObjectManager::get<CargoObject>(order.cargo) != nullptr)
            {
                if (order.type == Vehicles::OrderType::UnloadAll)
                {
                    Vehicles::OrderUnloadAll unload(order.cargo);
                    unload.setFormatArguments(args);
                }
                else
                {
                    Vehicles::OrderWaitFor wait(order.cargo);
                    wait.setFormatArguments(args);
                }
            }
        }
        char buffer[512];
        StringManager::formatString(buffer, std::size(buffer), format, args);
        tr.setCurrentFont(Gfx::Font::medium_bold);
        tr.drawString({ 2, static_cast<int16_t>(y - 1) }, Colour::black, buffer);
        const auto labelWidth = tr.getStringWidth(buffer);
        if (!order.slots.empty())
        {
            const auto slot = order.slots.back().departureMinute;
            FormatArguments slotArgs{};
            slotArgs.push(static_cast<uint16_t>(slot / 60));
            slotArgs.push(static_cast<uint16_t>(slot % 60));
            tr.drawStringLeft({ static_cast<int16_t>(labelWidth + 18), static_cast<int16_t>(y - 1) }, Colour::black, StringIds::line_slot_note, slotArgs);
        }
        if (numberedOrder(order.type) && orderNumber >= 1 && orderNumber <= 63)
        {
            const auto image = ImageIds::number_circle_01 + static_cast<uint32_t>(orderNumber - 1);
            drawingCtx.drawImage(ZoomLevel::full, tr.getStringWidth(buffer) + 6, y, Gfx::recolour(image, Colour::white));
            orderNumber++;
        }
    }

    static void drawVehicleScroll(Ui::Window& window, Gfx::DrawingContext& drawingCtx)
    {
        auto tr = Gfx::TextRenderer(drawingCtx);
        const auto* line = selectedLine();
        currency32_t profit = 0;
        currency32_t runningCost = 0;
        if (line != nullptr)
        {
            for (const auto vehicleId : line->vehicles)
            {
                auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId));
                if (head == nullptr)
                {
                    continue;
                }
                profit += monthlyProfit(*head);
                runningCost += head->calculateRunningCost();
            }
        }
        {
            FormatArguments args{};
            args.push(profit);
            tr.drawStringLeft({ 2, 0 }, Colour::black, StringIds::vehicle_monthly_profit, args);
        }
        {
            FormatArguments args{};
            args.push(runningCost);
            tr.drawStringLeft({ 2, kRowHeight }, Colour::black, StringIds::vehicle_monthly_running_cost, args);
        }
        if (line == nullptr)
        {
            return;
        }
        int16_t row = 2;
        const auto positions = stopPositions(*line);
        for (const auto vehicleId : line->vehicles)
        {
            auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId));
            if (head == nullptr)
            {
                continue;
            }
            const auto y = static_cast<int16_t>(row * kRowHeight);
            if (_orderHover == row)
            {
                drawingCtx.fillRect(0, y, window.widgets[widx::orders].width(), y + kRowHeight - 1, enumValue(ExtColour::unk30), Gfx::RectFlags::transparent);
            }
            const auto progress = head->sizeOfOrderTable == 0 ? Lines::LineProgress{} : vehicleProgress(*line, *head, positions);
            const auto fromStop = static_cast<int16_t>(progress.fromOrdinal + 1);
            const auto toStop = static_cast<int16_t>(progress.toOrdinal + 1);
            const auto stopString = head->sizeOfOrderTable == 0 ? StringIds::no_route_defined : (progress.atStop ? StringIds::line_stop_number : StringIds::line_between_stops);
            FormatArguments args{};
            args.push(StringIds::line_vehicle_row);
            args.push(head->name);
            args.push(head->ordinalNumber);
            args.push(stopString);
            if (head->sizeOfOrderTable != 0)
            {
                if (progress.atStop)
                {
                    args.push(fromStop);
                }
                else
                {
                    args.push(fromStop);
                    args.push(toStop);
                    args.push(static_cast<int16_t>(progress.perThousand / 10));
                }
            }
            args.push(monthlyProfit(*head));
            auto format = _orderHover == row ? StringIds::wcolour2_stringid : StringIds::black_stringid;
            tr.drawStringLeft({ 2, static_cast<int16_t>(y - 1) }, Colour::black, format, args);
            const auto choice = Lines::vehicleUsesOwnColours(line->id, vehicleId) ? StringIds::line_own_colours : StringIds::line_use_line_colours;
            const auto choiceX = static_cast<int16_t>(window.widgets[widx::orders].width() - 36);
            tr.drawStringLeft({ choiceX, static_cast<int16_t>(y - 1) }, _orderHover == row ? Colour::white : Colour::black, choice);
            row++;
        }
    }

    static void drawScroll(Ui::Window& window, Gfx::DrawingContext& drawingCtx, const uint32_t scrollIndex)
    {
        const auto shade = Colours::getShade(window.getColour(WindowColour::secondary).c(), 4);
        drawingCtx.clear(shade);
        auto tr = Gfx::TextRenderer(drawingCtx);
        if (scrollIndex == 0)
        {
            int16_t row = 0;
            for (const auto& line : Lines::allLines())
            {
                if (!lineVisible(line))
                {
                    continue;
                }
                const auto y = static_cast<int16_t>(row * kLineRowHeight);
                auto format = StringIds::black_stringid;
                const auto iconKind = Lines::lineKindIsOne(line.kinds) ? line.kinds : (_filter != Lines::kEveryLineKind && (line.kinds & _filter) != Lines::LineKind::none ? _filter : Lines::LineKind::none);
                auto textX = static_cast<int16_t>(iconKind == Lines::LineKind::none ? 2 : 28);
                if (line.id == _selectedLine)
                {
                    drawingCtx.fillRect(0, y, window.widgets[widx::lines].width(), y + kLineRowHeight - 1, PaletteIndex::black0, Gfx::RectFlags::none);
                    format = StringIds::white_stringid;
                }
                else if (_lineHover == row)
                {
                    drawingCtx.fillRect(0, y, window.widgets[widx::lines].width(), y + kLineRowHeight - 1, enumValue(ExtColour::unk30), Gfx::RectFlags::transparent);
                    format = StringIds::wcolour2_stringid;
                }
                if (iconKind != Lines::LineKind::none)
                {
                    drawKindImage(drawingCtx, Ui::Rect(0, y, 26, kLineRowHeight), kindImageFrame(iconKind));
                }
                FormatArguments args{};
                args.push(StringIds::line_name_raw);
                args.push(line.name.c_str());
                tr.drawStringLeft({ textX, static_cast<int16_t>(y + 8) }, Colour::black, format, args);
                row++;
            }
            return;
        }

        if (!ordersTab(window))
        {
            drawVehicleScroll(window, drawingCtx);
            return;
        }

        const auto* line = selectedLine();
        if (line == nullptr || line->orders.empty())
        {
            tr.drawStringLeft({ 2, 0 }, Colour::black, StringIds::no_route_defined);
            return;
        }
        int16_t row = 0;
        int16_t orderNumber = 1;
        for (const auto& order : line->orders)
        {
            drawOrderRow(drawingCtx, window, order, static_cast<int16_t>(row * kRowHeight), row, orderNumber);
            row++;
        }
    }

    static void getScrollSize([[maybe_unused]] Ui::Window& window, uint32_t scrollIndex, [[maybe_unused]] int32_t& scrollWidth, int32_t& scrollHeight)
    {
        if (scrollIndex == 0)
        {
            scrollHeight = kLineRowHeight * static_cast<int32_t>(visibleLineIds().size());
            return;
        }
        const auto* line = selectedLine();
        if (!ordersTab(window))
        {
            const auto vehicles = line == nullptr ? 0 : static_cast<int32_t>(line->vehicles.size());
            scrollHeight = kRowHeight * (2 + vehicles);
            return;
        }
        const auto rows = line == nullptr ? 1 : static_cast<int32_t>(std::max<size_t>(line->orders.size(), 1));
        scrollHeight = kRowHeight * rows;
    }

    static void scrollMouseDown(Ui::Window& window, [[maybe_unused]] int16_t x, int16_t y, uint8_t scrollIndex)
    {
        if (y < 0)
        {
            return;
        }
        const auto row = static_cast<int16_t>(y / (scrollIndex == 0 ? kLineRowHeight : kRowHeight));
        if (scrollIndex == 0)
        {
            const auto ids = visibleLineIds();
            if (row < 0 || static_cast<size_t>(row) >= ids.size())
            {
                return;
            }
            const auto clicked = ids[static_cast<size_t>(row)];
            if (auto* head = routeToolVehicle())
            {
                const auto* chosen = Lines::findLine(clicked);
                if (chosen != nullptr && Lines::assignVehicle(clicked, enumValue(head->id), head->mode).ok)
                {
                    Audio::playSound(Audio::SoundId::waypoint, Audio::ChannelId::ui, Input::getDragLastLocation().x);
                    WindowManager::invalidateOrderPageByVehicleNumber(enumValue(head->id));
                    if (auto* toolWindow = ToolManager::toolGetActiveWindow())
                    {
                        WindowManager::bringToFront(*toolWindow);
                    }
                }
                _selectedLine = clicked;
                _selectedOrder = -1;
                window.invalidate();
                return;
            }
            if (clicked == _selectedLine)
            {
                openRename(window);
                return;
            }
            _selectedLine = clicked;
            _selectedOrder = -1;
            window.invalidate();
            return;
        }
        const auto* line = selectedLine();
        if (!ordersTab(window))
        {
            if (line == nullptr || row < 2)
            {
                return;
            }
            int16_t vehicleRow = 2;
            for (const auto vehicleId : line->vehicles)
            {
                auto* head = EntityManager::get<Vehicles::VehicleHead>(static_cast<EntityId>(vehicleId));
                if (head == nullptr)
                {
                    continue;
                }
                if (vehicleRow == row)
                {
                    const auto choiceX = window.widgets[widx::orders].width() - 46;
                    if (x >= choiceX)
                    {
                        Lines::setVehicleOwnColours(line->id, vehicleId, !Lines::vehicleUsesOwnColours(line->id, vehicleId));
                        window.invalidate();
                        return;
                    }
                    Vehicle::Main::open(head);
                    return;
                }
                vehicleRow++;
            }
            return;
        }
        if (line == nullptr)
        {
            return;
        }
        if (row < 0 || static_cast<size_t>(row) >= line->orders.size())
        {
            _selectedOrder = -1;
            window.invalidate();
            return;
        }
        if (_selectedOrder == row)
        {
            const auto& order = line->orders[static_cast<size_t>(row)];
            if (order.type == Vehicles::OrderType::StopAt || order.type == Vehicles::OrderType::RouteThrough)
            {
                if (const auto* station = StationManager::get(static_cast<StationId>(order.station)))
                {
                    if (auto* main = WindowManager::getMainWindow())
                    {
                        main->viewportCentreOnTile({ station->x, station->y, static_cast<coord_t>(station->z + 32) });
                    }
                }
            }
            return;
        }
        _selectedOrder = row;
        window.invalidate();
    }

    static void onTextInput(Ui::Window& window, [[maybe_unused]] WidgetIndex_t widgetIndex, [[maybe_unused]] const WidgetId id, const char* input)
    {
        if (input == nullptr || input[0] == '\0' || selectedLine() == nullptr)
        {
            return;
        }
        Lines::renameLine(_selectedLine, input);
        window.invalidate();
    }

    static void scrollMouseOver(Ui::Window& window, [[maybe_unused]] int16_t x, int16_t y, uint8_t scrollIndex)
    {
        const auto row = y < 0 ? static_cast<int16_t>(-1) : static_cast<int16_t>(y / (scrollIndex == 0 ? kLineRowHeight : kRowHeight));
        if (scrollIndex == 0)
        {
            if (_lineHover != row)
            {
                _lineHover = row;
                window.invalidate();
            }
            return;
        }
        if (_orderHover != row)
        {
            _orderHover = row;
            window.invalidate();
        }
    }

    static constexpr WindowEventList kEvents = {
        .onClose = onClose,
        .onMouseUp = onMouseUp,
        .onMouseDown = onMouseDown,
        .onDropdown = onDropdown,
        .onUpdate = onUpdate,
        .onToolDown = onToolDown,
        .onToolAbort = onToolAbort,
        .toolCursor = toolCursor,
        .getScrollSize = getScrollSize,
        .scrollMouseDown = scrollMouseDown,
        .scrollMouseOver = scrollMouseOver,
        .textInput = onTextInput,
        .prepareDraw = prepareDraw,
        .draw = draw,
        .drawScroll = drawScroll,
    };

    static const WindowEventList& getEvents()
    {
        return kEvents;
    }

    Window* open()
    {
        auto* existing = WindowManager::bringToFront(WindowType::lineManager);
        if (existing != nullptr)
        {
            existing->invalidate();
            return existing;
        }

        auto* window = WindowManager::createWindowCentred(WindowType::lineManager, kWindowSize, WindowFlags::none, getEvents());
        window->setWidgets(_widgets);
        window->initScrollWidgets();
        if (const auto* skin = ObjectManager::get<InterfaceSkinObject>())
        {
            window->setColour(WindowColour::primary, skin->windowTitlebarColour);
            window->setColour(WindowColour::secondary, skin->windowColour);
        }
        if (selectedLine() == nullptr && !Lines::allLines().empty())
        {
            _selectedLine = Lines::allLines().front().id;
        }
        window->invalidate();
        return window;
    }
}

namespace OpenLoco::Ui::Windows::LineSchemes
{
    static constexpr Ui::Size kWindowSize = { 250, 196 };
    static int8_t _scheme = 0;
    static bool _secondary = false;

    enum widx
    {
        frame,
        caption,
        close,
        panel,
        check0,
        primary0 = 14,
        secondary0 = 24,
    };

    namespace Widx
    {
        constexpr WidgetId kClose{ "close" };
    }

    static constexpr StringId kSchemeNames[Lines::kVehicleColourSchemes] = {
        StringIds::colour_steam_locomotives,
        StringIds::colour_diesel_locomotives,
        StringIds::colour_electric_locomotives,
        StringIds::colour_multiple_units,
        StringIds::colour_passenger_vehicles,
        StringIds::colour_freight_vehicles,
        StringIds::colour_buses,
        StringIds::colour_trucks,
        StringIds::colour_aircraft,
        StringIds::colour_ships,
    };

    static constexpr auto _widgets = makeWidgets(
        Widgets::Frame({ 0, 0 }, kWindowSize, WindowColour::primary),
        Widgets::Caption({ 1, 1 }, { kWindowSize.width - 2, 13 }, Widgets::Caption::Style::colourText, WindowColour::primary, StringIds::line_colour_schemes),
        Widgets::ImageButton(Widx::kClose, { kWindowSize.width - 15, 2 }, { 13, 13 }, WindowColour::primary, ImageIds::close_button, StringIds::tooltip_close_window),
        Widgets::Panel({ 0, 15 }, { kWindowSize.width, kWindowSize.height - 15 }, WindowColour::secondary),
        Widgets::Checkbox({ 4, 18 }, { 176, 12 }, WindowColour::secondary, kSchemeNames[0], StringIds::tooltip_toggle_vehicle_colour_scheme),
        Widgets::Checkbox({ 4, 34 }, { 176, 12 }, WindowColour::secondary, kSchemeNames[1], StringIds::tooltip_toggle_vehicle_colour_scheme),
        Widgets::Checkbox({ 4, 50 }, { 176, 12 }, WindowColour::secondary, kSchemeNames[2], StringIds::tooltip_toggle_vehicle_colour_scheme),
        Widgets::Checkbox({ 4, 66 }, { 176, 12 }, WindowColour::secondary, kSchemeNames[3], StringIds::tooltip_toggle_vehicle_colour_scheme),
        Widgets::Checkbox({ 4, 82 }, { 176, 12 }, WindowColour::secondary, kSchemeNames[4], StringIds::tooltip_toggle_vehicle_colour_scheme),
        Widgets::Checkbox({ 4, 98 }, { 176, 12 }, WindowColour::secondary, kSchemeNames[5], StringIds::tooltip_toggle_vehicle_colour_scheme),
        Widgets::Checkbox({ 4, 114 }, { 176, 12 }, WindowColour::secondary, kSchemeNames[6], StringIds::tooltip_toggle_vehicle_colour_scheme),
        Widgets::Checkbox({ 4, 130 }, { 176, 12 }, WindowColour::secondary, kSchemeNames[7], StringIds::tooltip_toggle_vehicle_colour_scheme),
        Widgets::Checkbox({ 4, 146 }, { 176, 12 }, WindowColour::secondary, kSchemeNames[8], StringIds::tooltip_toggle_vehicle_colour_scheme),
        Widgets::Checkbox({ 4, 162 }, { 176, 12 }, WindowColour::secondary, kSchemeNames[9], StringIds::tooltip_toggle_vehicle_colour_scheme),
        Widgets::ColourButton({ 186, 17 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_main_colour),
        Widgets::ColourButton({ 186, 33 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_main_colour),
        Widgets::ColourButton({ 186, 49 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_main_colour),
        Widgets::ColourButton({ 186, 65 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_main_colour),
        Widgets::ColourButton({ 186, 81 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_main_colour),
        Widgets::ColourButton({ 186, 97 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_main_colour),
        Widgets::ColourButton({ 186, 113 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_main_colour),
        Widgets::ColourButton({ 186, 129 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_main_colour),
        Widgets::ColourButton({ 186, 145 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_main_colour),
        Widgets::ColourButton({ 186, 161 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_main_colour),
        Widgets::ColourButton({ 204, 17 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_secondary_colour),
        Widgets::ColourButton({ 204, 33 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_secondary_colour),
        Widgets::ColourButton({ 204, 49 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_secondary_colour),
        Widgets::ColourButton({ 204, 65 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_secondary_colour),
        Widgets::ColourButton({ 204, 81 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_secondary_colour),
        Widgets::ColourButton({ 204, 97 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_secondary_colour),
        Widgets::ColourButton({ 204, 113 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_secondary_colour),
        Widgets::ColourButton({ 204, 129 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_secondary_colour),
        Widgets::ColourButton({ 204, 145 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_secondary_colour),
        Widgets::ColourButton({ 204, 161 }, { 14, 14 }, WindowColour::secondary, ImageIds::colour_swatch_recolourable, StringIds::tooltip_select_secondary_colour));

    static void prepareDraw(Ui::Window& window)
    {
        const auto* line = Lines::findLine(static_cast<uint16_t>(window.number));
        if (line == nullptr)
        {
            return;
        }
        for (size_t index = 0; index < Lines::kVehicleColourSchemes; ++index)
        {
            const auto check = static_cast<WidgetIndex_t>(widx::check0 + index);
            const auto primary = static_cast<WidgetIndex_t>(widx::primary0 + index);
            const auto secondary = static_cast<WidgetIndex_t>(widx::secondary0 + index);
            const auto enabled = Lines::lineSchemeEnabled(*line, index);
            if (enabled)
            {
                window.activatedWidgets |= 1ULL << check;
            }
            else
            {
                window.activatedWidgets &= ~(1ULL << check);
            }
            window.widgets[primary].hidden = !enabled;
            window.widgets[secondary].hidden = !enabled;
            const auto primaryColour = static_cast<Colour>(line->schemePrimary[index]);
            const auto secondaryColour = static_cast<Colour>(line->schemeSecondary[index]);
            window.widgets[primary].image = Widget::kImageIdColourSet | ImageId::fromUInt32(ImageIds::colour_swatch_recolourable).withPrimary(primaryColour).toUInt32();
            window.widgets[secondary].image = Widget::kImageIdColourSet | ImageId::fromUInt32(ImageIds::colour_swatch_recolourable).withPrimary(secondaryColour).toUInt32();
        }
    }

    static void draw(Ui::Window& window, Gfx::DrawingContext& drawingCtx)
    {
        window.draw(drawingCtx);
    }

    static void onMouseUp(Ui::Window& window, WidgetIndex_t widgetIndex, const WidgetId id)
    {
        if (id == Widx::kClose)
        {
            WindowManager::close(window.type, window.number);
            return;
        }
        if (widgetIndex < widx::check0 || widgetIndex >= widx::primary0)
        {
            return;
        }
        const auto* line = Lines::findLine(static_cast<uint16_t>(window.number));
        if (line == nullptr)
        {
            return;
        }
        const auto index = static_cast<size_t>(widgetIndex - widx::check0);
        Lines::setLineSchemeEnabled(line->id, index, !Lines::lineSchemeEnabled(*line, index));
        window.invalidate();
    }

    static void onMouseDown(Ui::Window& window, WidgetIndex_t widgetIndex, [[maybe_unused]] const WidgetId id)
    {
        if (widgetIndex < widx::primary0 || widgetIndex >= widx::secondary0 + Lines::kVehicleColourSchemes)
        {
            return;
        }
        _secondary = widgetIndex >= widx::secondary0;
        _scheme = static_cast<int8_t>(widgetIndex - (_secondary ? widx::secondary0 : widx::primary0));
        const auto* line = Lines::findLine(static_cast<uint16_t>(window.number));
        if (line == nullptr || _scheme < 0 || static_cast<size_t>(_scheme) >= Lines::kVehicleColourSchemes)
        {
            return;
        }
        const auto selected = static_cast<Colour>(_secondary ? line->schemeSecondary[_scheme] : line->schemePrimary[_scheme]);
        Dropdown::showColour(&window, &window.widgets[widgetIndex], 0x7FFFFFFF, selected, window.getColour(WindowColour::secondary));
    }

    static void onDropdown(Ui::Window& window, [[maybe_unused]] WidgetIndex_t widgetIndex, [[maybe_unused]] const WidgetId id, int16_t itemIndex)
    {
        const auto item = itemIndex == -1 ? Dropdown::getHighlightedItem() : itemIndex;
        if (item < 0 || _scheme < 0)
        {
            return;
        }
        const auto* line = Lines::findLine(static_cast<uint16_t>(window.number));
        if (line == nullptr)
        {
            return;
        }
        const auto index = static_cast<size_t>(_scheme);
        const auto chosen = static_cast<uint8_t>(Dropdown::getItemArgument(static_cast<uint8_t>(item), 2));
        const auto primary = _secondary ? line->schemePrimary[index] : chosen;
        const auto secondary = _secondary ? chosen : line->schemeSecondary[index];
        Lines::setLineSchemeColours(line->id, index, primary, secondary);
        window.invalidate();
    }

    static void onUpdate(Ui::Window& window)
    {
        if (Lines::findLine(static_cast<uint16_t>(window.number)) == nullptr)
        {
            WindowManager::close(&window);
            return;
        }
        window.invalidate();
    }

    static constexpr WindowEventList kEvents = {
        .onMouseUp = onMouseUp,
        .onMouseDown = onMouseDown,
        .onDropdown = onDropdown,
        .onUpdate = onUpdate,
        .prepareDraw = prepareDraw,
        .draw = draw,
    };

    Window* open(uint16_t lineId)
    {
        if (Lines::findLine(lineId) == nullptr)
        {
            return nullptr;
        }
        if (auto* existing = WindowManager::find(WindowType::lineColourSchemes))
        {
            existing->number = lineId;
            existing->invalidate();
            return WindowManager::bringToFront(*existing);
        }
        auto* window = WindowManager::createWindowCentred(WindowType::lineColourSchemes, kWindowSize, WindowFlags::none, kEvents);
        window->setWidgets(_widgets);
        window->number = lineId;
        if (const auto* skin = ObjectManager::get<InterfaceSkinObject>())
        {
            window->setColour(WindowColour::primary, skin->windowTitlebarColour);
            window->setColour(WindowColour::secondary, skin->windowColour);
        }
        window->invalidate();
        return window;
    }
}
