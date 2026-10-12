#pragma once
#include "Rules.h"
#include <OpenLoco/Engine/Ui/Point.hpp>
#include <OpenLoco/Engine/Ui/Size.hpp>

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
    EditorLayout editorLayout(int16_t width, int16_t height);
    struct EditorRect
    {
        int32_t x, y, width, height;
    };
    EditorRect placeEditor(EditorRect opener, int16_t screenWidth, int16_t screenHeight, const std::vector<EditorRect>& obstacles);
}
namespace OpenLoco::Ui::Windows::RulesWindow
{
    Window* open(Rules::Owner owner, Window& opener, uint16_t vehicle = UINT16_MAX);
}
