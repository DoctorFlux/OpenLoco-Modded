#include "Rules/Editor.h"
#include <algorithm>

namespace OpenLoco::Rules
{
    EditorLayout editorLayout(int16_t width, int16_t height)
    {
        return { static_cast<int16_t>(width < 500 ? 90 : 112), 42, static_cast<int16_t>(height - 103), static_cast<int16_t>(height - 98), static_cast<int16_t>(height - 40), static_cast<int16_t>(height - 17) };
    }
    EditorRect placeEditor(EditorRect opener, int16_t screenWidth, int16_t screenHeight, const std::vector<EditorRect>& obstacles)
    {
        auto overlaps = [&](EditorRect r) {
            for (auto o : obstacles)
            {
                if (r.x < o.x + o.width && o.x < r.x + r.width && r.y < o.y + o.height && o.y < r.y + r.height)
                {
                    return true;
                }
            }
            return false;
        };
        for (auto size : { Ui::Size{ 600, 360 }, Ui::Size{ 360, 240 } })
        {
            if (size.width > screenWidth || size.height > screenHeight - 28)
            {
                continue;
            }
            const int32_t xs[] = { static_cast<int16_t>(opener.x + opener.width), opener.x, static_cast<int16_t>(opener.x - size.width), 0 };
            const int32_t ys[] = { opener.y, static_cast<int16_t>(opener.y + opener.height), static_cast<int16_t>(screenHeight - size.height), 28 };
            for (auto y : ys)
            {
                for (auto x : xs)
                {
                    EditorRect r{ x, y, size.width, size.height };
                    if (x >= 0 && y >= 28 && x + r.width <= screenWidth && y + r.height <= screenHeight && !overlaps(r))
                    {
                        return r;
                    }
                }
            }
        }
        return { static_cast<int16_t>(std::clamp<int>(opener.x + opener.width, 0, std::max<int>(0, screenWidth - 360))), static_cast<int16_t>(std::clamp<int>(opener.y, 28, std::max<int>(28, screenHeight - 240))), 360, 240 };
    }
}
