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
        return sentence;
    }
    static bool bracket(const Block& block)
    {
        return block.kind != Kind::skip && block.kind != Kind::goTo && block.kind != Kind::breakLoop;
    }
    ProgramLayout programLayout(const Program& program, int16_t width, int16_t orderHeight)
    {
        ProgramLayout layout;
        int16_t y = 2;
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
                    if (!bracket(outer) || outer.id == block->id)
                    {
                        continue;
                    }
                    if (indexOf(program, outer.first) <= row && indexOf(program, block->last) <= indexOf(program, outer.last)
                        && (outer.first != block->first || outer.last != block->last || outer.id < block->id))
                    {
                        if (depth < 8)
                        {
                            ++depth;
                        }
                    }
                }
                const auto available = std::max<int>(44, width - 20 - std::min<int>(depth, 8) * 8);
                std::vector<std::string> lines;
                auto sentence = blockSentence(program, *block);
                while (!sentence.empty() && lines.size() < 8)
                {
                    size_t length = sentence.size();
                    while (length > 1 && std::max<int>(Gfx::TextRenderer::getStringWidth(Gfx::Font::medium_bold, sentence.substr(0, length).c_str()), static_cast<int>(length) * 5) > available)
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
                    if (lines.size() == 7)
                    {
                        length = sentence.size();
                    }
                    lines.push_back(sentence.substr(0, length));
                    sentence.erase(0, length);
                    while (!sentence.empty() && sentence.front() == ' ')
                    {
                        sentence.erase(0, 1);
                    }
                }
                const auto height = static_cast<int16_t>(std::max<int>(18, static_cast<int>(lines.size()) * 12 + 6));
                layout.items.push_back({ y, height, row, block->id, depth, 0, std::move(lines) });
                y += height;
            }
            uint8_t depth = 0;
            for (const auto& block : program.blocks)
            {
                if (bracket(block) && indexOf(program, block.first) <= row && row <= indexOf(program, block.last))
                {
                    if (depth < 8)
                    {
                        ++depth;
                    }
                }
            }
            layout.orderTops.push_back(y);
            layout.items.push_back({ y, orderHeight, row, 0, depth, static_cast<int16_t>(y + orderHeight), {} });
            y += orderHeight;
            for (auto& visual : layout.items)
            {
                if (!visual.block)
                {
                    continue;
                }
                const auto block = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.id == visual.block; });
                if (bracket(*block) && indexOf(program, block->last) == row)
                {
                    visual.bottom = y - 1;
                }
            }
            const auto endings = std::count_if(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return bracket(b) && indexOf(program, b.last) == row; });
            if (endings)
            {
                y += 4;
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
    void drawProgramBlocks(Gfx::DrawingContext& context, const Program& program, const Runtime* runtime, const ProgramLayout& layout, int16_t width)
    {
        const auto currentBlock = runtime ? executingBlock(program, *runtime) : 0;
        for (const auto& visual : layout.items)
        {
            if (!visual.block)
            {
                continue;
            }
            const auto block = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.id == visual.block; });
            const auto x = static_cast<int16_t>(3 + std::min<int>(visual.depth, 8) * 8);
            const auto feedback = blockFeedback(program, runtime, block->id);
            const auto colour = feedback == Feedback::passed ? Colours::getShade(Colour::green, 7) : feedback == Feedback::failed ? Colours::getShade(Colour::red, 7)
                                                                                                                                  : PaletteIndex::grey1;
            context.fillRect(x, visual.top, width - 2, visual.top + visual.height - 2, colour, Gfx::RectFlags::none);
            for (size_t i = 0; i < visual.lines.size(); ++i)
            {
                auto text = std::string(1, static_cast<char>(ControlCodes::Colour::black)) + visual.lines[i];
                auto renderer = Gfx::TextRenderer(context);
                renderer.setCurrentFont(Gfx::Font::medium_bold);
                renderer.clipString(width - x - 10, text.data());
                renderer.drawString({ static_cast<int16_t>(x + 6), static_cast<int16_t>(visual.top + 3 + i * 12) }, Colour::black, text.c_str());
            }
            if (bracket(*block))
            {
                context.fillRect(x, visual.top, x + 2, visual.bottom, colour, Gfx::RectFlags::none);
                context.fillRect(x, visual.bottom - 2, x + 9, visual.bottom, colour, Gfx::RectFlags::none);
            }
            if (currentBlock == block->id)
            {
                context.fillRect(x + 3, visual.top + 2, x + 5, visual.top + visual.height - 3, PaletteIndex::black0, Gfx::RectFlags::none);
            }
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
    std::optional<Edit> OrderDrag::finish(Owner nextOwner, const Program& program, std::optional<size_t> destination)
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
