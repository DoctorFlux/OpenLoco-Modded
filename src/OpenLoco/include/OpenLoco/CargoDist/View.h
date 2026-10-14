#pragma once
#include "CargoDist.h"
#include <OpenLoco/Graphics/DrawingContext.h>
#include <OpenLoco/Ui/Window.h>
namespace OpenLoco::CargoDist
{
    enum class ViewKind : uint8_t
    {
        settings,
        station,
        vehicle,
        line
    };
    struct ViewRect
    {
        int16_t x, y, width, height;
        bool operator==(const ViewRect&) const = default;
    };
    struct ViewLayout
    {
        ViewRect list, filters, controls, footer;
    };
    ViewLayout viewLayout(int16_t width, int16_t height);
    struct ViewEntry
    {
        uint8_t cargo = 0;
        uint16_t origin = kUnknownStation, next = kUnknownStation, destination = kUnknownStation;
        uint32_t quantity = 0;
        int64_t credit = 0;
    };
    struct ViewRow
    {
        std::string key;
        uint8_t cargo = 0, depth = 0, field = 0;
        uint16_t station = kUnknownStation;
        uint32_t quantity = 0;
        int64_t credit = 0;
        bool expanded = false;
    };
    std::vector<ViewRow> groupedRows(const std::vector<ViewEntry>& entries, uint8_t grouping, const std::set<std::string>& expanded, bool largestFirst);
    Ui::Window* openView(ViewKind kind = ViewKind::settings, uint16_t id = 0, const Ui::Window* origin = nullptr);
    void drawWorldLinks(Gfx::DrawingContext& context, ZoomLevel zoom, uint8_t rotation);
    void drawMapLinks(Gfx::DrawingContext& context, const std::function<Ui::Point(int16_t, int16_t)>& project);
    bool overlayEnabled();
    uint64_t generation();
}
