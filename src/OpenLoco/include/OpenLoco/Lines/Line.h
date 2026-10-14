#pragma once

#include "Objects/VehicleObject.h"
#include "Types.hpp"
#include "Vehicles/Orders.h"
#include "ZoomLevel.hpp"
namespace OpenLoco::Gfx
{
    class DrawingContext;
}
#include <OpenLoco/Core/EnumFlags.hpp>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace OpenLoco::Lines
{
    enum class DepartureRule : uint8_t
    {
        loadIfAvailable,
        fullLoadAny,
        fullLoadAll,
    };

    struct CargoFilter
    {
        uint8_t cargoType = 0;
        uint8_t percent = 100;
        bool load = true;
        bool unload = true;
    };

    struct TimeSlot
    {
        uint16_t arrivalMinute = 0;
        uint16_t departureMinute = 0;
    };

    struct LineOrder
    {
        Vehicles::OrderType type = Vehicles::OrderType::StopAt;
        uint16_t station = 0;
        uint8_t cargo = 0;
        uint8_t platform = 0;
        DepartureRule departure = DepartureRule::loadIfAvailable;
        uint16_t minWait = 0;
        uint16_t maxWait = 0;
        int16_t tileX = 0;
        int16_t tileY = 0;
        uint8_t baseZ = 0;
        uint8_t direction = 0;
        uint8_t trackId = 0;
        std::vector<CargoFilter> filters;
        std::vector<TimeSlot> slots;
    };

    struct LineCursor
    {
        size_t index = 0;
        bool backward = false;
    };

    struct DepartureDecision
    {
        bool wait = false;
        uint16_t waitMinutes = 0;
        bool late = false;
    };

    // Rail, road (bus and truck), tram, air, and water. A line starts as every kind and each stop narrows that set.
    enum class LineKind : uint8_t
    {
        none = 0U,
        rail = 1U << 0,
        road = 1U << 1,
        tram = 1U << 2,
        air = 1U << 3,
        water = 1U << 4,
    };
    OPENLOCO_ENABLE_ENUM_OPERATORS(LineKind);

    inline constexpr LineKind kEveryLineKind = LineKind::rail | LineKind::road | LineKind::tram | LineKind::air | LineKind::water;

    inline bool lineKindIsOne(LineKind kinds)
    {
        const auto value = static_cast<uint8_t>(kinds);
        return value != 0U && (value & static_cast<uint8_t>(value - 1U)) == 0U;
    }

    inline TransportMode transportModeForKind(LineKind kind)
    {
        if (kind == LineKind::air)
        {
            return TransportMode::air;
        }
        if (kind == LineKind::water)
        {
            return TransportMode::water;
        }
        if (kind == LineKind::rail)
        {
            return TransportMode::rail;
        }
        return TransportMode::road;
    }

    inline bool vehicleFitsLine(VehicleType type, LineKind kinds)
    {
        if (kinds == kEveryLineKind || kinds == LineKind::none)
        {
            return true;
        }
        auto bit = LineKind::none;
        switch (type)
        {
            case VehicleType::train:
                bit = LineKind::rail;
                break;
            case VehicleType::bus:
            case VehicleType::truck:
                bit = LineKind::road;
                break;
            case VehicleType::tram:
                bit = LineKind::tram;
                break;
            case VehicleType::aircraft:
                bit = LineKind::air;
                break;
            case VehicleType::ship:
                bit = LineKind::water;
                break;
        }
        return (kinds & bit) != LineKind::none;
    }

    struct Line
    {
        uint16_t id = 0;
        std::string name;
        uint8_t colour = 1;
        uint8_t secondaryColour = 1;
        bool paintVehicles = false;
        // Bit 0 is steam locomotives through bit 9 ships, matching the company colour schemes.
        uint16_t schemeEnabled = 0;
        uint8_t schemePrimary[10]{};
        uint8_t schemeSecondary[10]{};
        std::vector<uint16_t> ownColourVehicles;
        TransportMode mode = TransportMode::rail;
        LineKind kinds = kEveryLineKind;
        bool loop = false;
        bool express = false;
        bool autoUnbunch = false;
        uint16_t unbunchGapMinutes = 0;
        uint16_t roundTripMinutes = 0;
        std::vector<LineOrder> orders;
        std::vector<uint16_t> vehicles;
    };

    inline bool lineAcceptsVehicle(const Line& line, VehicleType type, TransportMode mode)
    {
        if (line.kinds == kEveryLineKind)
        {
            return mode == line.mode;
        }
        return vehicleFitsLine(type, line.kinds);
    }

    inline bool lineAcceptsMode(const Line& line, TransportMode mode)
    {
        if (line.kinds == kEveryLineKind)
        {
            return mode == line.mode;
        }
        switch (mode)
        {
            case TransportMode::rail:
                return (line.kinds & LineKind::rail) != LineKind::none;
            case TransportMode::road:
                return (line.kinds & (LineKind::road | LineKind::tram)) != LineKind::none;
            case TransportMode::air:
                return (line.kinds & LineKind::air) != LineKind::none;
            case TransportMode::water:
                return (line.kinds & LineKind::water) != LineKind::none;
        }
        return false;
    }

    struct LineError
    {
        bool ok = false;
        uint16_t id = 0;
        std::string message;
    };

    LineError createLine(std::string name, TransportMode mode, uint8_t colour);
    LineError createCompanyLine(std::string name, TransportMode mode, uint8_t primary, uint8_t secondary);
    LineError renameLine(uint16_t lineId, std::string name);
    LineError recolourLine(uint16_t lineId, uint8_t colour);
    LineError setLineColours(uint16_t lineId, uint8_t primary, uint8_t secondary);
    LineError setLineSchemeEnabled(uint16_t lineId, size_t index, bool enabled);
    LineError setLineSchemeColours(uint16_t lineId, size_t index, uint8_t primary, uint8_t secondary);
    bool vehicleUsesOwnColours(uint16_t lineId, uint16_t vehicleId);
    LineError setVehicleOwnColours(uint16_t lineId, uint16_t vehicleId, bool own);
    LineError setPaintVehicles(uint16_t lineId, bool paint);
    static constexpr size_t kVehicleColourSchemes = 10;
    bool lineSchemeEnabled(const Line& line, size_t index);
    // The vehicle paint tool keeps this vehicle off the line colours.
    void noteVehiclePainted(uint16_t vehicleId);
    LineError setLoop(uint16_t lineId, bool loop);
    LineError setExpress(uint16_t lineId, bool express);
    LineError deleteLine(uint16_t lineId);
    LineError addOrder(uint16_t lineId, LineOrder order);
    LineError insertOrder(uint16_t lineId, size_t index, LineOrder order);
    LineError removeOrder(uint16_t lineId, size_t index);
    LineError moveOrder(uint16_t lineId, size_t index, int delta);
    LineError reverseOrders(uint16_t lineId);
    LineError replaceOrders(uint16_t lineId, std::vector<LineOrder> orders);
    LineOrder fromVehicleOrder(const Vehicles::Order& order);
    LineError assignVehicle(uint16_t lineId, uint16_t vehicleId, TransportMode mode);
    // Switches the line when it has no placed vehicles of another mode and its stops do not disagree.
    LineError setLineMode(uint16_t lineId, TransportMode mode);
    // Kinds a station still allows. A normal road station is road. Track with RoadObjectFlags::isRoad clear is tram.
    LineKind stationKinds(uint16_t stationId);
    LineKind roadObjectKind(uint8_t roadObjectId);
    bool stopFits(uint16_t lineId, LineKind stopKinds);
    // Intersects the line with one stop. An empty intersection is refused. One remaining kind sets the transport mode.
    LineError acceptStopKinds(uint16_t lineId, LineKind stopKinds);
    std::optional<TransportMode> stationTransportMode(uint16_t stationId);
    void reconcileLineMode(uint16_t lineId);
    LineError unassignVehicle(uint16_t vehicleId);
    void setTimetable(uint16_t lineId, bool autoUnbunch, uint16_t gapMinutes, uint16_t roundTripMinutes);
    void setStopSettings(uint16_t lineId, size_t index, DepartureRule rule, uint16_t minWait, uint16_t maxWait, uint8_t platform, std::vector<CargoFilter> filters, std::vector<TimeSlot> slots);

    const Line* findLine(uint16_t lineId);
    const Line* lineForVehicle(uint16_t vehicleId);
    const std::vector<Line>& allLines();
    const std::vector<LineOrder>* ordersForVehicle(uint16_t vehicleId);
    bool canRun(const Line& line);
    LineCursor nextStop(const Line& line, LineCursor cursor);
    LineCursor cursorForVehicle(uint16_t vehicleId);
    LineCursor advanceAssignedVehicle(uint16_t vehicleId);
    uint16_t orderByteOffset(const Line& line, size_t index);
    size_t orderIndexAtOffset(const Line& line, uint16_t offset);

    struct StopPosition
    {
        int32_t x = 0;
        int32_t y = 0;
    };

    // Where a vehicle sits on the numbered stops. perThousand is 0 at fromOrdinal and 1000 at toOrdinal.
    // wraps is the loop leg from the last stop back to the first, or the other way, so the marker does not cross the stops in between.
    struct LineProgress
    {
        size_t fromOrdinal = 0;
        size_t toOrdinal = 0;
        uint16_t perThousand = 0;
        bool atStop = false;
        bool wraps = false;
    };

    LineProgress progressAlongLine(const Line& line, uint16_t currentOrder, uint16_t stationId, bool atStation, int32_t vehicleX, int32_t vehicleY, const std::vector<StopPosition>& positions);

    // A loop of N stops has N equal gaps, and the last gap is the return to stop 1. A line that does not loop ends on the last stop.
    size_t diagramGapCount(size_t stopCount, bool loop);
    int32_t diagramStopX(int32_t left, int32_t width, size_t stopCount, bool loop, size_t ordinal);
    int32_t diagramMarkerX(int32_t left, int32_t width, size_t stopCount, bool loop, const LineProgress& progress);
    uint16_t timetableMinute(uint32_t scenarioTicks);

    // A running vehicle's distance along the line, in the same units as the line length.
    struct SpreadPlace
    {
        uint16_t id = 0;
        int32_t distance = 0;
    };

    struct EvenSpread
    {
        bool hold = false;
        uint32_t capTicks = 0;
    };

    // Hold while the forward gap is shorter than an even share of lineLength. A vehicle behind is not an input.
    // Vehicles at the same distance: the lowest id is the lead and looks past the others.
    EvenSpread chooseEvenSpread(int32_t lineLength, uint16_t myId, int32_t myDistance, const SpreadPlace* others, size_t otherCount, uint32_t headwayTicks);
    bool holdForTimetable(uint16_t vehicleId, uint16_t currentOrder, uint16_t stationId);
    DepartureDecision decideDeparture(const LineOrder& stop, uint16_t arrivalMinute, uint16_t previousDepartureMinute, uint16_t gapMinutes, bool autoUnbunch, uint16_t roundTripMinutes, uint16_t vehicleCount);

    void armPurchase(uint16_t lineId);
    void clearPurchase();
    uint16_t purchaseLine();

    bool saveCompanion(const std::filesystem::path& savePath);
    bool loadCompanion(const std::filesystem::path& savePath);
    void clearLines();

    std::filesystem::path companionPath(const std::filesystem::path& savePath);

    struct RouteLeg
    {
        size_t fromStop = 0;
        size_t toStop = 0;
    };

    // Always includes the leg from the last stop back to the first when there are two or more stops.
    size_t loopLegs(size_t stopCount, RouteLeg* legs, size_t capacity);

    struct OverlayClaim
    {
        uint16_t lineId = 0;
        uint8_t colour = 0;
        bool selected = false;
    };

    // The selected line's colour wins when several lines share a piece.
    uint8_t chosenOverlayColour(const OverlayClaim* claims, size_t count, uint16_t selectedLine);

    void invalidateRouteCache();
    // Line metadata/visibility changed without changing the world graph.
    void invalidateRouteView();
    void setRouteView(uint16_t selectedLine, bool showSelected, bool showAll, uint16_t vehicle = UINT16_MAX);
    void updateRouteView(); // One bounded path search per UI update; live rules sampled on a runtime copy.
    void invalidateRuleRouteCache(uint16_t line);
    struct RouteViewStats
    {
        uint32_t rulePolls = 0;
        uint32_t pathSearches = 0;
        uint32_t routePublishes = 0;
        size_t cachedLegs = 0;
        size_t cachedUnits = 0;
        size_t pendingLegs = 0;
        size_t publishedLegs = 0;
        size_t publishedUnits = 0;
    };
    RouteViewStats routeViewStats();
    std::string routePreviewStatus(uint16_t line);
    int32_t routeLaneOffset(uint16_t line); // Stable while visibility is unchanged, including pending/empty rule paths.
    void clearRouteView();
    std::optional<uint8_t> routeOverlayColour(int32_t x, int32_t y, uint8_t baseZ, uint8_t pieceId, uint8_t rotation, uint8_t kind);
    std::optional<uint8_t> surfaceOverlayColour(int32_t x, int32_t y);

    // Lane 0 and lane 1 of two lines are equal distances on opposite sides of the path.
    int32_t laneOffsetPixels(int lane, int laneCount);
    // Repeated visits by one line still count as that one line.
    int countDistinctLines(const uint16_t* lineIds, size_t count);

    struct StrokeEnds
    {
        int32_t x0 = 0;
        int32_t y0 = 0;
        int32_t x1 = 0;
        int32_t y1 = 0;
    };

    StrokeEnds shiftStroke(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t pixels);

    // A real piece is a short step. A jump from one stop to another is not drawn.
    bool strokeContinues(int32_t x0, int32_t y0, int32_t x1, int32_t y1);

    struct CurveSample
    {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
    };

    // Samples one track or road piece. A curve leaves the straight line between its ends.
    size_t trackPieceCurve(int32_t x, int32_t y, int32_t z, uint16_t trackAndDirection, bool road, CurveSample* out, size_t capacity);

    // The same line already covers this piece. The opposite direction is the same pass.
    bool samePieceSameWay(uint16_t line, int32_t x, int32_t y, int32_t z, uint16_t tad, uint16_t otherLine, int32_t otherX, int32_t otherY, int32_t otherZ, uint16_t otherTad);

    void drawRouteStrokes(Gfx::DrawingContext& drawingCtx, ZoomLevel zoom, uint8_t rotation);
}
