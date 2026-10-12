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
        std::optional<Edit> finish(Owner owner, const Program& program, std::optional<size_t> destination);
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
    Window* open(Rules::Owner owner, Window& opener, uint16_t vehicle = UINT16_MAX, std::optional<size_t> row = {});
}
