#pragma once
#include "Rules.h"
#include <OpenLoco/Engine/Ui/Point.hpp>
#include <OpenLoco/Engine/Ui/Size.hpp>
#include <optional>

namespace OpenLoco::Gfx
{
    class DrawingContext;
}
namespace OpenLoco::Ui
{
    struct Window;
}
namespace OpenLoco::Rules
{
    struct EditorLayout
    {
        int16_t paletteWidth;
        int16_t canvasTop;
        int16_t canvasBottom;
        int16_t controlsTop;
        int16_t binTop;
        int16_t footerTop;
    };
    enum class Feedback : uint8_t
    {
        idle,
        passed,
        failed,
        active
    };
    Feedback blockFeedback(const Program& program, const Runtime* runtime, Key block);
    bool blockIsCurrent(const Program& program, const Runtime* runtime, Key block);
    struct ProgramVisual
    {
        int16_t top = 0, height = 0;
        size_t row = 0;
        Key block = 0;
        uint8_t depth = 0;
        int16_t bottom = 0;
        std::vector<std::string> lines;
        bool closing = false; // Interior append slot and closing arm of a C block.
    };
    struct ProgramLayout
    {
        std::vector<ProgramVisual> items;
        std::vector<int16_t> orderTops;
        int16_t height = 0;
    };
    std::string blockSentence(const Program& program, const Block& block);
    ProgramLayout programLayout(const Program& program, int16_t width, int16_t orderHeight = 10);
    const ProgramVisual* programHit(const ProgramLayout& layout, int16_t y);
    size_t programRowAt(const ProgramLayout& layout, int16_t y);
    struct DropPosition
    {
        size_t row;
        Key into = 0;
    };
    DropPosition programDrop(const Program& program, const ProgramLayout& layout, int16_t y);
    int16_t visualIndent(const ProgramVisual& visual, int16_t width);
    bool orderGripHit(const ProgramVisual& visual, int16_t x, int16_t origin, int16_t width);
    std::optional<Edit> blockDrop(const Program& program, Kind kind, size_t first, size_t last, Key hovered = 0);
    std::optional<Edit> paletteDrop(const Program& program, const ProgramLayout& layout, Kind kind, int16_t y, std::optional<std::pair<size_t, size_t>> selection = {});
    void drawStackBlock(Gfx::DrawingContext& context, int16_t x, int16_t y, int16_t width, int16_t height, Kind kind, Feedback feedback = Feedback::idle, bool selected = false);
    void drawOrderBlock(Gfx::DrawingContext& context, int16_t x, int16_t y, int16_t width, int16_t height, bool selected = false);
    void drawProgramBlocks(Gfx::DrawingContext& context, const Program& program, const Runtime* runtime, const ProgramLayout& layout, int16_t width, int16_t origin = 0, Key selected = 0, bool labels = true);
    std::optional<Ui::Point> routeOrderMarkerPosition(const Program& program, const ProgramLayout& layout, Key order, int16_t width, int16_t origin = 0);
    void drawRouteOrderMarker(Gfx::DrawingContext& context, const Program& program, const ProgramLayout& layout, Key order, int16_t width, int16_t origin = 0);
    struct OrderFeedback
    {
        std::string sentence;
        Feedback state = Feedback::idle;
        bool current = false;
    };
    OrderFeedback orderFeedback(const Program& program, const Runtime* runtime, size_t row);
    void drawOrderFeedback(Gfx::DrawingContext& context, int16_t x, int16_t y, int16_t width, const OrderFeedback& feedback);
    int16_t orderRowHeight(const Program* program);
    // Drag preview holds only UI state. Dropping returns a validated edit for the caller to submit.
    struct OrderDrag
    {
        Owner owner{};
        uint32_t revision = 0;
        size_t row = 0;
        int16_t startX = 0, startY = 0;
        bool active = false, moved = false;
        void begin(Owner owner, const Program& program, size_t row, int16_t x, int16_t y);
        void motion(int16_t x, int16_t y);
        std::optional<Edit> finish(Owner owner, const Program& program, std::optional<size_t> destination, Key into = 0);
        void cancel();
    };
    EditorLayout editorLayout(int16_t width, int16_t height);
    struct EditorRect
    {
        int32_t x, y, width, height;
    };
    EditorRect placeEditor(EditorRect opener, int16_t screenWidth, int16_t screenHeight, const std::vector<EditorRect>& obstacles);
}
namespace OpenLoco::Ui::Windows::RulesWindow
{
    Window* open(Rules::Owner owner, Window& opener, uint16_t vehicle = UINT16_MAX, std::optional<size_t> row = {}, Rules::Key block = 0);
}
