#include "Rules/Editor.h"
#include "Graphics/Colour.h"
#include "Graphics/Gfx.h"
#include "Graphics/TextRenderer.h"
#include "Localisation/Formatting.h"
#include "Localisation/StringManager.h"
#include "Objects/CargoObject.h"
#include "Objects/ObjectManager.h"
#include <algorithm>

namespace OpenLoco::Rules
{

    Feedback blockFeedback(const Program& program, const Runtime* runtime, Key key)
    {
        if (!runtime || runtime->revision != program.revision)
        {
            return Feedback::idle;
        }
        const auto block = std::find_if(program.blocks.begin(), program.blocks.end(), [key](const Block& block) { return block.id == key; });
        if (block != program.blocks.end() && (block->kind == Kind::skip || block->kind == Kind::goTo || block->kind == Kind::breakLoop))
        {
            key = block->parent;
        }
        const auto it = runtime->branches.find(key);
        if (it != runtime->branches.end())
        {
            return it->second ? Feedback::passed : Feedback::failed;
        }
        return blockIsCurrent(program, runtime, key) ? Feedback::active : Feedback::idle;
    }
    bool blockIsCurrent(const Program& program, const Runtime* runtime, Key key)
    {
        if (!runtime || runtime->revision != program.revision)
        {
            return false;
        }
        const auto it = std::find_if(program.blocks.begin(), program.blocks.end(), [key](const Block& b) { return b.id == key; });
        if (it == program.blocks.end())
        {
            return false;
        }
        const auto branch = runtime->branches.find(key);
        if (branch != runtime->branches.end() && !branch->second)
        {
            return runtime->waiting == key;
        }
        const auto row = indexOf(program, runtime->current);
        return row < program.rows.size() && indexOf(program, it->first) <= row && row <= indexOf(program, it->last);
    }
    static std::string checkSentence(const Program& program, const Condition& condition)
    {
        const char* compares[] = { "<", "=", ">" };
        switch (condition.check)
        {
            case Check::cargoWaiting:
            {
                std::string cargo = "cargo";
                if (const auto* object = ObjectManager::get<CargoObject>(condition.cargo))
                {
                    char name[128]{};
                    StringManager::formatString(name, sizeof(name), object->name);
                    cargo = name;
                }
                return cargo + " " + compares[static_cast<unsigned>(condition.compare)] + " " + std::to_string(condition.value) + " at row " + std::to_string(indexOf(program, condition.stop) + 1);
            }
            case Check::full: return "full";
            case Check::empty: return "empty";
            case Check::hasSpace: return "space available";
            case Check::platformFree: return "platform " + std::to_string(condition.platform) + " free at row " + std::to_string(indexOf(program, condition.stop) + 1);
            case Check::late: return "late at row " + std::to_string(indexOf(program, condition.stop) + 1);
            case Check::onTime: return "on time at row " + std::to_string(indexOf(program, condition.stop) + 1);
            case Check::year: return "year " + std::string(compares[static_cast<unsigned>(condition.compare)]) + " " + std::to_string(condition.value);
        }
        return {};
    }
    std::string blockSentence(const Program& program, const Block& block)
    {
        std::string sentence = kindName(block.kind);
        if (block.kind == Kind::ifBlock || block.kind == Kind::elseIf || block.kind == Kind::waitUntil)
        {
            sentence += " " + checkSentence(program, block.condition);
            if (block.join != Join::none)
            {
                sentence += (block.join == Join::andCheck ? " and " : " or ") + checkSentence(program, block.extra);
            }
        }
        if (block.kind == Kind::repeat)
        {
            sentence += block.number ? " " + std::to_string(block.number) + " times" : " forever";
        }
        if (block.kind == Kind::waitUntil)
        {
            sentence += " (up to " + std::to_string(block.number) + " min)";
        }
        if (block.kind == Kind::group)
        {
            sentence += " " + block.name;
        }
        if (block.kind == Kind::goTo)
        {
            sentence += " row " + std::to_string(indexOf(program, block.target) + 1);
        }
        if (block.kind == Kind::breakLoop)
        {
            sentence += " out of Repeat";
        }
        if (!block.enabled)
        {
            sentence += " (unavailable)";
        }
        if (block.kind != Kind::group)
        {
            sentence += timingOf(program, block) == Timing::onArrival ? " [on arrival]" : " [before travelling]";
        }
        return sentence;
    }
    static bool bracket(const Block& block)
    {
        return block.kind != Kind::skip && block.kind != Kind::goTo && block.kind != Kind::breakLoop;
    }
    int16_t visualIndent(const ProgramVisual& visual, int16_t width)
    {
        return static_cast<int16_t>(std::min<int>(visual.depth * 12, std::max<int>(0, width - 92)));
    }
    bool orderGripHit(const ProgramVisual& visual, int16_t x, int16_t origin, int16_t width)
    {
        const auto left = origin + visualIndent(visual, width) + 4;
        return !visual.block && left <= x && x < left + 18;
    }
    ProgramLayout programLayout(const Program& program, int16_t width, int16_t orderHeight)
    {
        ProgramLayout layout;
        int16_t y = 4;
        orderHeight = std::max<int16_t>(18, orderHeight);
        for (size_t row = 0; row < program.rows.size(); ++row)
        {
            std::vector<const Block*> opening;
            for (const auto& block : program.blocks)
            {
                if (indexOf(program, block.first) == row)
                {
                    opening.push_back(&block);
                }
            }
            std::sort(opening.begin(), opening.end(), [&](const Block* a, const Block* b) {
                const auto al = indexOf(program, a->last), bl = indexOf(program, b->last);
                return al != bl ? al > bl : a->id < b->id;
            });
            for (const auto* block : opening)
            {
                uint8_t depth = 0;
                for (const auto& outer : program.blocks)
                {
                    if (bracket(outer) && outer.id != block->id && indexOf(program, outer.first) <= row && indexOf(program, block->last) <= indexOf(program, outer.last)
                        && (outer.first != block->first || outer.last != block->last || outer.id < block->id))
                    {
                        depth = std::min<int>(8, depth + 1);
                    }
                }
                ProgramVisual v{ y, 0, row, block->id, depth, 0, {} };
                const auto available = std::max<int>(40, width - 24 - visualIndent(v, width));
                auto sentence = blockSentence(program, *block);
                sentence.erase(0, std::min(sentence.size(), std::string(kindName(block->kind)).size() + 1));
                const bool condition = block->kind == Kind::ifBlock || block->kind == Kind::elseIf || block->kind == Kind::waitUntil;
                const auto prefixWidth = static_cast<int>(std::string(kindName(block->kind)).size()) * 6 + 14;
                const bool inlineValue = condition && static_cast<int>(sentence.size()) * 6 < available - prefixWidth;
                while (!sentence.empty() && v.lines.size() < 6)
                {
                    size_t length = std::min(sentence.size(), static_cast<size_t>(available / 6));
                    while (length > 1 && std::max<int>(Gfx::TextRenderer::getStringWidth(Gfx::Font::medium_bold, sentence.substr(0, length).c_str()), static_cast<int>(length) * 6) > available)
                    {
                        --length;
                    }
                    if (length < sentence.size())
                    {
                        const auto space = sentence.rfind(' ', length);
                        if (space != std::string::npos && space > 0)
                        {
                            length = space;
                        }
                    }
                    if (v.lines.size() == 5)
                    {
                        length = sentence.size();
                    }
                    v.lines.push_back(sentence.substr(0, length));
                    sentence.erase(0, length);
                    while (!sentence.empty() && sentence.front() == ' ')
                    {
                        sentence.erase(0, 1);
                    }
                }
                v.height = inlineValue ? 26 : static_cast<int16_t>(std::max<int>(22, 24 + static_cast<int>(v.lines.size()) * 12));
                layout.items.push_back(std::move(v));
                y += layout.items.back().height;
            }
            uint8_t depth = 0;
            for (const auto& block : program.blocks)
            {
                if (bracket(block) && indexOf(program, block.first) <= row && row <= indexOf(program, block.last))
                {
                    depth = std::min<int>(8, depth + 1);
                }
            }
            layout.orderTops.push_back(y);
            layout.items.push_back({ y, orderHeight, row, 0, depth, static_cast<int16_t>(y + orderHeight), {} });
            y += orderHeight;
            std::vector<size_t> closing;
            for (size_t i = 0; i < layout.items.size(); ++i)
            {
                const auto& v = layout.items[i];
                if (!v.block || v.closing)
                {
                    continue;
                }
                const auto b = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.id == v.block; });
                if (bracket(*b) && indexOf(program, b->last) == row)
                {
                    closing.push_back(i);
                }
            }
            // Inner cavities close first, each with its own reachable append slot.
            std::reverse(closing.begin(), closing.end());
            for (auto i : closing)
            {
                const auto v = layout.items[i];
                layout.items[i].bottom = y + 21;
                layout.items.push_back({ y, 26, row, v.block, v.depth, static_cast<int16_t>(y + 21), {}, true });
                y += 26;
            }
        }
        layout.height = y;
        return layout;
    }
    const ProgramVisual* programHit(const ProgramLayout& layout, int16_t y)
    {
        const auto it = std::find_if(layout.items.begin(), layout.items.end(), [y](const ProgramVisual& v) { return v.top <= y && y < v.top + v.height; });
        return it == layout.items.end() ? nullptr : &*it;
    }
    size_t programRowAt(const ProgramLayout& layout, int16_t y)
    {
        if (const auto* hit = programHit(layout, y))
        {
            return hit->row;
        }
        for (size_t row = 0; row < layout.orderTops.size(); ++row)
        {
            if (y < layout.orderTops[row])
            {
                return row;
            }
        }
        return layout.orderTops.size();
    }
    DropPosition programDrop(const Program& program, const ProgramLayout& layout, int16_t y)
    {
        const auto* hit = programHit(layout, y);
        if (!hit)
        {
            return { programRowAt(layout, y), 0 };
        }
        if (hit->block)
        {
            const auto block = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.id == hit->block; });
            if (!bracket(*block))
            {
                return { hit->row, block->parent };
            }
            return { hit->row + (hit->closing ? 1 : 0), hit->block };
        }
        Key into = 0;
        size_t smallest = program.rows.size() + 1;
        for (const auto& block : program.blocks)
        {
            const auto a = indexOf(program, block.first), z = indexOf(program, block.last);
            if (bracket(block) && a <= hit->row && hit->row <= z && z - a <= smallest)
            {
                into = block.id;
                smallest = z - a;
            }
        }
        return { hit->row, into };
    }
    std::optional<Edit> blockDrop(const Program& program, Kind kind, size_t first, size_t last, Key hovered)
    {
        if (first > last || last >= program.rows.size())
        {
            return {};
        }
        if ((kind == Kind::otherwise || kind == Kind::elseIf) && hovered)
        {
            const auto parent = std::find_if(program.blocks.begin(), program.blocks.end(), [hovered](const Block& b) { return b.id == hovered; });
            if (parent != program.blocks.end() && first <= indexOf(program, parent->last))
            {
                const bool paired = std::any_of(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.parent == parent->id && !bracket(b); });
                if (!paired)
                {
                    first = last = indexOf(program, parent->last) + 1;
                    if (last >= program.rows.size())
                    {
                        return {};
                    }
                }
            }
        }
        Edit editCommand;
        auto& block = editCommand.block;
        block.kind = kind;
        block.first = program.rows[first].key;
        block.last = program.rows[last].key;
        block.number = kind == Kind::repeat ? 2 : 5;
        block.name = "My section";
        if (kind == Kind::skip || kind == Kind::goTo || kind == Kind::breakLoop)
        {
            size_t smallest = program.rows.size() + 1;
            for (const auto& parent : program.blocks)
            {
                const auto a = indexOf(program, parent.first), z = indexOf(program, parent.last);
                if ((parent.kind == Kind::ifBlock || parent.kind == Kind::elseIf || parent.kind == Kind::otherwise) && a <= first && last <= z && (z - a <= smallest || parent.id == hovered))
                {
                    block.parent = parent.id;
                    smallest = z - a;
                    if (parent.id == hovered)
                    {
                        break;
                    }
                }
            }
            if (block.parent)
            {
                const auto parent = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.id == block.parent; });
                block.first = parent->first;
                block.last = parent->last;
            }
        }
        if (kind == Kind::otherwise || kind == Kind::elseIf)
        {
            for (const auto& parent : program.blocks)
            {
                if (parent.kind != Kind::ifBlock && parent.kind != Kind::elseIf)
                {
                    continue;
                }
                if (std::any_of(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.parent == parent.id && (b.kind == Kind::otherwise || b.kind == Kind::elseIf); }))
                {
                    continue;
                }
                const bool paired = parent.first == block.first && parent.last == block.last && std::any_of(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.parent == parent.id && !bracket(b); });
                if (indexOf(program, parent.last) < first || paired)
                {
                    block.parent = parent.id;
                    if (parent.id == hovered)
                    {
                        break;
                    }
                }
            }
        }
        if (kind == Kind::goTo)
        {
            const auto row = std::find_if(program.rows.begin(), program.rows.end(), [](const Row& row) { return isRoutable(row.raw); });
            if (row != program.rows.end())
            {
                block.target = row->key;
            }
        }
        if (kind == Kind::breakLoop)
        {
            size_t smallest = program.rows.size() + 1;
            for (const auto& loop : program.blocks)
            {
                const auto a = indexOf(program, loop.first), z = indexOf(program, loop.last);
                if (loop.kind == Kind::repeat && a <= indexOf(program, block.first) && indexOf(program, block.last) <= z && z - a <= smallest)
                {
                    block.target = loop.id;
                    smallest = z - a;
                }
            }
        }
        auto preview = program;
        return edit(preview, editCommand) ? std::optional<Edit>{ editCommand } : std::nullopt;
    }
    std::optional<Edit> paletteDrop(const Program& program, const ProgramLayout& layout, Kind kind, int16_t y, std::optional<std::pair<size_t, size_t>> selection)
    {
        const auto* hit = programHit(layout, y);
        if (!hit || hit->row >= program.rows.size())
        {
            return {};
        }
        size_t first = hit->row, last = first;
        if (selection && selection->first <= first && first <= selection->second && selection->second < program.rows.size())
        {
            first = selection->first;
            last = selection->second;
        }
        if (hit->block)
        {
            const auto block = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.id == hit->block; });
            const auto a = indexOf(program, block->first), z = indexOf(program, block->last);
            if (!hit->closing)
            {
                first = a;
                last = z;
            }
            else if (kind == Kind::otherwise || kind == Kind::elseIf)
            {
                first = last = z + 1;
            }
            else if (!selection || first < a || z < last)
            {
                first = last = z;
            }
        }
        return blockDrop(program, kind, first, last, hit->block);
    }
    static Colour blockColour(Kind kind, Feedback feedback)
    {
        if (feedback == Feedback::passed)
        {
            return Colour::green;
        }
        if (feedback == Feedback::failed)
        {
            return Colour::red;
        }
        if (kind == Kind::repeat || kind == Kind::breakLoop)
        {
            return Colour::darkGreen;
        }
        if (kind == Kind::group)
        {
            return Colour::purple;
        }
        if (kind == Kind::waitUntil)
        {
            return Colour::darkOrange;
        }
        return Colour::blue;
    }
    static void stackShape(Gfx::DrawingContext& context, int16_t x, int16_t y, int16_t width, int16_t height, uint8_t colour, bool notch = true)
    {
        if (width < 8 || height < 6)
        {
            return;
        }
        for (int row = 0; row < height; ++row)
        {
            const int inset = row == 0 || row == height - 1 ? 3 : row == 1 || row == height - 2 ? 1
                                                                                                : 0;
            if (notch && row < 3 && width > 42)
            {
                const int left = 17 + row * 2, right = 33 - row * 2;
                context.fillRect(x + inset, y + row, x + left, y + row, colour, Gfx::RectFlags::none);
                context.fillRect(x + right, y + row, x + width - inset - 1, y + row, colour, Gfx::RectFlags::none);
            }
            else
            {
                context.fillRect(x + inset, y + row, x + width - inset - 1, y + row, colour, Gfx::RectFlags::none);
            }
        }
        if (width > 42)
        {
            context.fillRect(x + 20, y + height, x + 30, y + height + 1, colour, Gfx::RectFlags::none);
            context.fillRect(x + 22, y + height + 2, x + 28, y + height + 2, colour, Gfx::RectFlags::none);
        }
    }
    void drawStackBlock(Gfx::DrawingContext& context, int16_t x, int16_t y, int16_t width, int16_t height, Kind kind, Feedback feedback, bool selected)
    {
        const auto colour = blockColour(kind, feedback);
        stackShape(context, x, y, width, height, Colours::getShade(colour, 5));
        if (selected)
        {
            context.fillRect(x + 4, y + 4, x + 6, y + height - 3, Colours::getShade(Colour::yellow, 9), Gfx::RectFlags::none);
        }
    }
    void drawOrderBlock(Gfx::DrawingContext& context, int16_t x, int16_t y, int16_t width, int16_t height, bool selected)
    {
        stackShape(context, x, y, width, height - 3, Colours::getShade(selected ? Colour::yellow : Colour::purple, 9));
    }
    static void socketShape(Gfx::DrawingContext& context, int16_t x, int16_t y, int16_t width, int16_t height, uint8_t colour, bool boolean)
    {
        const auto radius = std::min<int>(boolean ? 7 : 5, height / 2);
        for (int row = 0; row < height; ++row)
        {
            const auto inset = std::max<int>(0, radius - std::min(row, height - row - 1));
            context.fillRect(x + inset, y + row, x + width - inset - 1, y + row, colour, Gfx::RectFlags::none);
        }
    }
    static void blockText(Gfx::DrawingContext& context, int16_t x, int16_t y, int16_t width, std::string text, bool white)
    {
        text.insert(text.begin(), static_cast<char>(white ? ControlCodes::Colour::white : ControlCodes::Colour::black));
        auto renderer = Gfx::TextRenderer(context);
        renderer.setCurrentFont(Gfx::Font::medium_bold);
        renderer.clipString(width, text.data());
        renderer.drawString({ x, y }, Colour::black, text.c_str());
    }
    void drawProgramBlocks(Gfx::DrawingContext& context, const Program& program, const Runtime* runtime, const ProgramLayout& layout, int16_t width, int16_t origin, Key selected, bool labels)
    {
        const auto currentBlock = runtime ? executingBlock(program, *runtime) : 0;
        for (const auto& visual : layout.items)
        {
            const auto x = static_cast<int16_t>(origin + 3 + visualIndent(visual, width));
            const auto right = origin + width - 2;
            if (!visual.block)
            {
                drawOrderBlock(context, x, visual.top, static_cast<int16_t>(right - x), visual.height);
                continue;
            }
            const auto b = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.id == visual.block; });
            const auto feedback = blockFeedback(program, runtime, b->id);
            const auto colour = blockColour(b->kind, feedback);
            const auto nextClause = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& next) { return next.parent == b->id && (next.kind == Kind::otherwise || next.kind == Kind::elseIf) && indexOf(program, next.first) == indexOf(program, b->last) + 1; });
            if (visual.closing)
            {
                if (labels)
                {
                    blockText(context, x + 14, visual.top + 1, static_cast<int16_t>(right - x - 18), "+ drop inside", false);
                }
                if (nextClause == program.blocks.end())
                {
                    stackShape(context, x, visual.top + 12, static_cast<int16_t>(right - x), 10, Colours::getShade(colour, 5));
                }
                else
                {
                    context.fillRect(x, visual.top, x + 7, visual.top + visual.height - 1, Colours::getShade(colour, 5), Gfx::RectFlags::none);
                }
                continue;
            }
            const auto previous = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& previous) { return previous.id == b->parent; });
            const bool joined = (b->kind == Kind::otherwise || b->kind == Kind::elseIf) && previous != program.blocks.end() && indexOf(program, previous->last) + 1 == indexOf(program, b->first);
            stackShape(context, x, visual.top, static_cast<int16_t>(right - x), visual.height - 3, Colours::getShade(colour, 5), !joined);
            if (joined)
            {
                context.fillRect(x, visual.top, x + 7, visual.top + 3, Colours::getShade(colour, 5), Gfx::RectFlags::none);
            }
            if (selected == b->id)
            {
                context.fillRect(x + 4, visual.top + 4, x + 6, visual.top + visual.height - 6, Colours::getShade(Colour::yellow, 9), Gfx::RectFlags::none);
            }
            if (labels)
            {
                blockText(context, x + 9, visual.top + 6, static_cast<int16_t>(right - x - 15), kindName(b->kind), true);
            }
            if (bracket(*b))
            {
                context.fillRect(x, visual.top + visual.height - 3, x + 7, visual.bottom, Colours::getShade(colour, 5), Gfx::RectFlags::none);
            }
            const bool condition = b->kind == Kind::ifBlock || b->kind == Kind::elseIf || b->kind == Kind::waitUntil;
            if (!visual.lines.empty())
            {
                const auto inlineValue = condition && visual.height == 26;
                const auto left = static_cast<int16_t>(inlineValue ? x + std::string(kindName(b->kind)).size() * 6 + 16 : x + 8);
                const auto top = static_cast<int16_t>(visual.top + (inlineValue ? 4 : 19));
                socketShape(context, left, top, static_cast<int16_t>(right - left - 7), static_cast<int16_t>(visual.lines.size() * 12 + 4), Colours::getShade(condition ? colour : Colour::white, condition ? 2 : 9), condition);
                for (size_t i = 0; labels && i < visual.lines.size(); ++i)
                {
                    blockText(context, left + 8, static_cast<int16_t>(top + 2 + i * 12), static_cast<int16_t>(right - left - 22), visual.lines[i], condition);
                }
            }
            if (currentBlock == b->id)
            {
                context.fillRect(x + 1, visual.top + 3, x + 3, visual.top + visual.height - 4, Colours::getShade(Colour::yellow, 9), Gfx::RectFlags::none);
            }
        }
    }
    std::optional<Ui::Point> routeOrderMarkerPosition(const Program& program, const ProgramLayout& layout, Key order, int16_t width, int16_t origin)
    {
        const auto row = indexOf(program, order);
        if (row >= program.rows.size() || !isRoutable(program.rows[row].raw))
        {
            return {};
        }
        const auto item = std::find_if(layout.items.begin(), layout.items.end(), [row](const ProgramVisual& item) { return !item.block && item.row == row; });
        if (item == layout.items.end())
        {
            return {};
        }
        return Ui::Point{ static_cast<int16_t>(origin + 1 + visualIndent(*item, width)), static_cast<int16_t>(item->top + 4) };
    }
    void drawRouteOrderMarker(Gfx::DrawingContext& context, const Program& program, const ProgramLayout& layout, Key order, int16_t width, int16_t origin)
    {
        const auto point = routeOrderMarkerPosition(program, layout, order, width, origin);
        if (!point)
        {
            return;
        }
        // A white right-pointing arrow with a dark edge remains visible over
        // category colours, selected rows and the inner mouth of a C block.
        for (int row = 0; row < 9; ++row)
        {
            const auto length = 7 - std::abs(4 - row);
            context.fillRect(point->x, point->y + row, point->x + length - 1, point->y + row, PaletteIndex::black0, Gfx::RectFlags::none);
        }
        for (int row = 0; row < 7; ++row)
        {
            const auto length = 5 - std::abs(3 - row);
            context.fillRect(point->x + 1, point->y + row + 1, point->x + length, point->y + row + 1, Colours::getShade(Colour::white, 9), Gfx::RectFlags::none);
        }
    }
    OrderFeedback orderFeedback(const Program& program, const Runtime* runtime, size_t row)
    {
        OrderFeedback feedback;
        if (row >= program.rows.size())
        {
            return feedback;
        }
        feedback.current = runtime && runtime->revision == program.revision && runtime->current == program.rows[row].key;
        for (const auto& block : program.blocks)
        {
            const auto first = indexOf(program, block.first), last = indexOf(program, block.last);
            if (row < first || row > last)
            {
                continue;
            }
            if (!feedback.sentence.empty())
            {
                feedback.sentence += " | ";
            }
            feedback.sentence += kindName(block.kind);
            if (block.kind == Kind::group)
            {
                feedback.sentence += " " + block.name;
            }
            else if (block.kind == Kind::ifBlock || block.kind == Kind::elseIf || block.kind == Kind::waitUntil)
            {
                feedback.sentence += " " + checkSentence(program, block.condition);
                if (block.join != Join::none)
                {
                    feedback.sentence += (block.join == Join::andCheck ? " and " : " or ") + checkSentence(program, block.extra);
                }
            }
            else if (block.kind == Kind::repeat)
            {
                feedback.sentence += block.number ? " " + std::to_string(block.number) : " forever";
            }
            else if (block.kind == Kind::goTo)
            {
                feedback.sentence += " row " + std::to_string(indexOf(program, block.target) + 1);
            }
            if (first != last)
            {
                feedback.sentence += " [" + std::to_string(first + 1) + "-" + std::to_string(last + 1) + "]";
            }
            if (!block.enabled)
            {
                feedback.sentence += " (unavailable)";
            }
            const auto state = blockFeedback(program, runtime, block.id);
            if (state == Feedback::failed || feedback.state == Feedback::idle || (feedback.state == Feedback::active && state == Feedback::passed))
            {
                feedback.state = state;
            }
        }
        if (feedback.current)
        {
            feedback.sentence = "> " + feedback.sentence;
        }
        return feedback;
    }
    void drawOrderFeedback(Gfx::DrawingContext& context, int16_t x, int16_t y, int16_t width, const OrderFeedback& feedback)
    {
        const auto colour = feedback.state == Feedback::passed ? Colours::getShade(Colour::green, 7) : feedback.state == Feedback::failed ? Colours::getShade(Colour::red, 7)
            : feedback.current                                                                                                            ? Colours::getShade(Colour::yellow, 7)
                                                                                                                                          : PaletteIndex::grey1;
        context.fillRect(x, y, x + width - 1, y + 9, colour, Gfx::RectFlags::none);
        std::string text(1, static_cast<char>(ControlCodes::Colour::black));
        text += feedback.sentence;
        auto renderer = Gfx::TextRenderer(context);
        renderer.setCurrentFont(Gfx::Font::medium_bold);
        renderer.clipString(width - 4, text.data());
        renderer.drawString({ static_cast<int16_t>(x + 2), static_cast<int16_t>(y - 1) }, Colour::black, text.c_str());
    }
    int16_t orderRowHeight(const Program* program) { return program && !program->blocks.empty() ? 20 : 10; }
    void OrderDrag::begin(Owner nextOwner, const Program& program, size_t first, int16_t x, int16_t y)
    {
        owner = nextOwner;
        revision = program.revision;
        row = first;
        startX = x;
        startY = y;
        active = first < program.rows.size();
        moved = false;
    }
    void OrderDrag::motion(int16_t x, int16_t y) { moved |= active && std::abs(x - startX) + std::abs(y - startY) > 4; }
    void OrderDrag::cancel() { active = moved = false; }
    std::optional<Edit> OrderDrag::finish(Owner nextOwner, const Program& program, std::optional<size_t> destination, Key into)
    {
        const auto allowed = active && moved && owner == nextOwner && revision == program.revision && destination.has_value();
        cancel();
        if (!allowed)
        {
            return {};
        }
        Edit editCommand;
        editCommand.kind = EditKind::moveRows;
        editCommand.first = editCommand.last = row;
        editCommand.destination = *destination;
        if (into)
        {
            const auto target = std::find_if(program.blocks.begin(), program.blocks.end(), [into](const Block& b) { return b.id == into; });
            if (target != program.blocks.end() && !(indexOf(program, target->first) == row && indexOf(program, target->last) == row))
            {
                editCommand.into = into;
            }
        }
        auto preview = program;
        return edit(preview, editCommand) ? std::optional<Edit>{ editCommand } : std::nullopt;
    }
    EditorLayout editorLayout(int16_t width, int16_t height)
    {
        return { static_cast<int16_t>((width < 500 || height < 308) ? 138 : 112), 42, static_cast<int16_t>(height - 117), static_cast<int16_t>(height - 112), static_cast<int16_t>(height - 54), static_cast<int16_t>(height - 17) };
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
