#include "Rules/Editor.h"
#include "Graphics/Colour.h"
#include "Graphics/Gfx.h"
#include "Graphics/TextRenderer.h"
#include "Localisation/Formatting.h"
#include "Localisation/StringManager.h"
#include "Objects/CargoObject.h"
#include "Objects/ObjectManager.h"
#include <algorithm>
#include <charconv>

namespace OpenLoco::Rules
{

    Feedback blockFeedback(const Program& program, const Runtime* runtime, Key key)
    {
        if (!runtime || runtime->revision != program.revision)
        {
            return Feedback::idle;
        }
        const auto block = std::find_if(program.blocks.begin(), program.blocks.end(), [key](const Block& block) { return block.id == key; });
        if (block != program.blocks.end() && block->parent && (block->kind == Kind::skip || block->kind == Kind::goTo || block->kind == Kind::breakLoop))
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
        const char* compares[] = { "<", "=", ">", "!=", "<=", ">=", "is true", "is false" };
        if (!numericCheck(condition.check) && condition.compare == Compare::isFalse)
        {
            auto positive = condition;
            positive.compare = Compare::isTrue;
            return "not (" + checkSentence(program, positive) + ")";
        }
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
            case Check::cargoWaitingFor: return "cargo waiting for stop " + std::to_string(condition.stop) + " " + std::string(compares[static_cast<unsigned>(condition.compare)]) + " " + std::to_string(condition.value);
            case Check::cargoOnboardFor: return "cargo onboard for stop " + std::to_string(condition.stop) + " " + std::string(compares[static_cast<unsigned>(condition.compare)]) + " " + std::to_string(condition.value);
            case Check::year: return "year " + std::string(compares[static_cast<unsigned>(condition.compare)]) + " " + std::to_string(condition.value);
            case Check::loadPercentage: return "load (%) " + std::string(compares[static_cast<unsigned>(condition.compare)]) + " " + std::to_string(condition.value);
            case Check::vehicleAge: return "age (years) " + std::string(compares[static_cast<unsigned>(condition.compare)]) + " " + std::to_string(condition.value);
            case Check::reliability: return "reliability (%) " + std::string(compares[static_cast<unsigned>(condition.compare)]) + " " + std::to_string(condition.value);
            case Check::maxSpeed: return "max speed (mph) " + std::string(compares[static_cast<unsigned>(condition.compare)]) + " " + std::to_string(condition.value);
            case Check::always: return "always";
        }
        return {};
    }
    std::string blockSentence(const Program& program, const Block& block)
    {
        std::string sentence = kindName(block.kind);
        if (hasCondition(block))
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
        if (block.kind == Kind::waitUntil)
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
        return static_cast<int16_t>(std::min<int>(visual.depth * 8, std::max<int>(0, width - 146)));
    }
    static std::vector<FieldVisual> blockFields(const Program& program, const Block& b)
    {
        std::vector<FieldVisual> fields;
        auto add = [&](BlockField field, std::string label, bool extra = false) { fields.push_back({ field, extra, 0, 0, 0, 16, std::move(label) }); };
        auto ordinal = [&](Key key) {
            size_t number = 0;
            for (const auto& row : program.rows)
            {
                number += isRoutable(row.raw);
                if (row.key == key)
                {
                    return number;
                }
            }
            return size_t{ 0 };
        };
        if (b.kind == Kind::waitUntil)
        {
            add(BlockField::when, timingOf(program, b) == Timing::onArrival ? "arrive" : "before");
        }
        auto condition = [&](const Condition& c, bool extra) {
            constexpr const char* checks[] = { "cargo", "full", "empty", "space", "platform", "late", "on time", "year", "waiting for", "onboard for", "load %", "age (years)", "reliability %", "max mph", "always" };
            add(BlockField::check, checks[static_cast<unsigned>(c.check)], extra);
            if (c.check == Check::cargoWaiting || c.check == Check::cargoWaitingFor || c.check == Check::cargoOnboardFor || c.check == Check::platformFree || c.check == Check::late || c.check == Check::onTime)
            {
                add(BlockField::stop, ordinal(c.stop) ? "stop " + std::to_string(ordinal(c.stop)) : "missing", extra);
            }
            if (c.check == Check::cargoWaiting || c.check == Check::cargoWaitingFor || c.check == Check::cargoOnboardFor)
            {
                std::string label = "cargo " + std::to_string(c.cargo + 1);
                if (const auto* cargo = ObjectManager::get<CargoObject>(c.cargo))
                {
                    char name[128]{};
                    StringManager::formatString(name, sizeof(name), cargo->name);
                    label = name;
                }
                add(BlockField::cargo, label, extra);
            }
            if (c.check == Check::platformFree)
            {
                add(BlockField::cargo, "plat " + std::to_string(c.platform), extra);
            }
            if (numericCheck(c.check))
            {
                constexpr const char* compares[] = { "<", "=", ">", "!=", "<=", ">=", "true", "false" };
                add(BlockField::compare, compares[static_cast<unsigned>(c.compare)], extra);
                add(BlockField::value, std::to_string(c.value), extra);
            }
            else
            {
                add(BlockField::compare, c.compare == Compare::isFalse ? "is false" : "is true", extra);
            }
        };
        if (hasCondition(b))
        {
            condition(b.condition, false);
            add(BlockField::join, b.join == Join::none ? "+" : b.join == Join::andCheck ? "and"
                                                                                        : "or");
            if (b.join != Join::none)
            {
                condition(b.extra, true);
            }
        }
        if (b.kind == Kind::repeat)
        {
            add(BlockField::repeat, b.number ? std::to_string(b.number) + " times" : "Forever");
        }
        if (b.kind == Kind::waitUntil)
        {
            add(BlockField::timeout, std::to_string(b.number) + " min");
        }
        if (b.kind == Kind::group)
        {
            add(BlockField::name, b.name);
        }
        if (b.kind == Kind::goTo)
        {
            add(BlockField::target, ordinal(b.target) ? "to " + std::to_string(ordinal(b.target)) : "missing");
        }
        if (b.kind == Kind::breakLoop)
        {
            add(BlockField::target, "loop");
        }
        return fields;
    }
    std::string fieldTextValue(const Program& program, Key block, BlockField field, bool extra)
    {
        const auto b = std::find_if(program.blocks.begin(), program.blocks.end(), [block](const Block& b) { return b.id == block; });
        if (b == program.blocks.end())
        {
            return {};
        }
        if (field == BlockField::name && b->kind == Kind::group)
        {
            return b->name;
        }
        if (field == BlockField::repeat && b->kind == Kind::repeat)
        {
            return b->number ? std::to_string(b->number) : "Forever";
        }
        if (field == BlockField::timeout && b->kind == Kind::waitUntil)
        {
            return std::to_string(b->number);
        }
        if (field == BlockField::value)
        {
            const auto& c = extra ? b->extra : b->condition;
            if ((!extra || b->join != Join::none) && (c.check == Check::year || c.check == Check::cargoWaiting))
            {
                return std::to_string(c.value);
            }
        }
        return {};
    }
    Result fieldTextEdit(const Program& program, Key block, BlockField field, bool extra, std::string_view value, Edit& command)
    {
        const auto b = std::find_if(program.blocks.begin(), program.blocks.end(), [block](const Block& b) { return b.id == block; });
        if (b == program.blocks.end())
        {
            return { false, "This block no longer exists" };
        }
        const auto fields = blockFields(program, *b);
        if (!std::any_of(fields.begin(), fields.end(), [&](const FieldVisual& f) { return f.field == field && f.extra == extra; }))
        {
            return { false, "This field is not available on this block" };
        }
        Edit proposed;
        proposed.kind = EditKind::updateBlock;
        proposed.block = *b;
        if (field == BlockField::name)
        {
            proposed.block.name = value;
        }
        else
        {
            int32_t number = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
            if (field == BlockField::repeat && (value == "Forever" || value == "forever" || value == "infinite"))
            {
                number = 0;
            }
            else if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || number < 0 || number > 1000000)
            {
                return { false, "Enter a whole number from 0 to 1000000" };
            }
            if (field == BlockField::repeat || field == BlockField::timeout)
            {
                if (number > 1440)
                {
                    return { false, "That number is too large" };
                }
                proposed.block.number = static_cast<uint16_t>(number);
            }
            else if (field == BlockField::value)
            {
                (extra ? proposed.block.extra : proposed.block.condition).value = number;
            }
            else
            {
                return { false, "Choose this field from its dropdown" };
            }
        }
        auto preview = program;
        const auto result = edit(preview, proposed);
        if (result)
        {
            command = std::move(proposed);
        }
        return result;
    }
    bool fieldEditStillValid(Owner expectedOwner, Owner currentOwner, const Program& program, Key block, uint32_t revision)
    {
        return expectedOwner == currentOwner && revision == program.revision && std::any_of(program.blocks.begin(), program.blocks.end(), [block](const Block& b) { return b.id == block; });
    }
    std::vector<int> menuPageItems(size_t count, size_t page)
    {
        std::vector<int> items;
        const auto first = page * 30;
        if (first >= count)
        {
            return items;
        }
        for (size_t i = first; i < std::min(count, first + 30); ++i)
        {
            items.push_back(static_cast<int>(i));
        }
        if (page)
        {
            items.push_back(-1);
        }
        if (first + 30 < count)
        {
            items.push_back(-2);
        }
        return items;
    }
    bool orderGripHit(const ProgramVisual& visual, int16_t x, int16_t origin, int16_t width)
    {
        const auto left = origin + visualIndent(visual, width) + 4;
        return !visual.block && left <= x && x < left + 18;
    }
    ProgramLayout programLayout(const Program& program, int16_t width, int16_t orderHeight, FieldTextWidth measure)
    {
        ProgramLayout layout;
        int16_t y = 2;
        orderHeight = std::max<int16_t>(14, orderHeight);
        const auto textWidth = [measure](std::string_view text, bool heading) {
            return measure ? measure(text, heading) : static_cast<int16_t>(Gfx::TextRenderer::getStringWidth(heading ? Gfx::Font::medium_bold : Gfx::Font::medium_normal, std::string(text).c_str()));
        };
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
                const auto right = std::max<int>(34, width - visualIndent(v, width) - 11);
                int16_t left = static_cast<int16_t>(std::min<int>(right, textWidth(block->kind == Kind::goTo && !block->parent ? "Jump if" : kindName(block->kind), true) + 14));
                int16_t top = 2;
                v.fields = blockFields(program, *block);
                for (auto& field : v.fields)
                {
                    const auto cap = field.field == BlockField::cargo ? 140 : field.field == BlockField::value ? 64
                                                                                                               : 180;
                    const auto desired = std::clamp<int>(textWidth(field.label, false) + 12, 20, cap);
                    field.width = static_cast<int16_t>(std::min<int>(desired, right - 8));
                    if (left + field.width > right)
                    {
                        left = 8;
                        top += 18;
                    }
                    field.left = left;
                    field.top = v.top + top;
                    left += field.width + 2;
                }
                v.height = static_cast<int16_t>(v.fields.empty() ? 16 : top + 18);
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
                layout.items[i].bottom = y + 7;
                layout.items.push_back({ y, 8, row, v.block, v.depth, static_cast<int16_t>(y + 7), {}, true });
                y += 8;
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
    const FieldVisual* blockFieldHit(const ProgramLayout& layout, int16_t x, int16_t y, int16_t width, int16_t origin)
    {
        const auto* item = programHit(layout, y);
        if (!item || !item->block || item->closing)
        {
            return nullptr;
        }
        const auto left = origin + 3 + visualIndent(*item, width);
        for (const auto& field : item->fields)
        {
            if (left + field.left <= x && x < left + field.left + field.width && field.top <= y && y < field.top + field.height)
            {
                return &field;
            }
        }
        return nullptr;
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
        return { hit->row + (y >= hit->top + hit->height / 2 ? 1 : 0), into };
    }
    std::optional<Edit> blockDrop(const Program& program, Kind kind, size_t first, size_t last, Key hovered, bool conditionalJump)
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
        if (kind == Kind::skip || (kind == Kind::goTo && !conditionalJump) || kind == Kind::breakLoop)
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
            if (!block.parent)
            {
                block.last = block.first;
                block.condition.check = conditionalJump ? Check::full : Check::always;
            }
            const auto row = std::find_if(program.rows.begin(), program.rows.end(), [&](const Row& row) { return isRoutable(row.raw) && (block.parent || row.key != block.first); });
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
    std::optional<Edit> paletteDrop(const Program& program, const ProgramLayout& layout, Kind kind, int16_t y, std::optional<std::pair<size_t, size_t>> selection, bool conditionalJump)
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
        return blockDrop(program, kind, first, last, hit->block, conditionalJump);
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
        stackShape(context, x, y, width, height, Colours::getShade(colour, 3));
        context.fillRect(x + 4, y + 1, x + width - 5, y + 1, Colours::getShade(colour, 6), Gfx::RectFlags::none);
        context.fillRect(x + 4, y + height - 2, x + width - 5, y + height - 2, Colours::getShade(colour, 1), Gfx::RectFlags::none);
        if (selected)
        {
            context.fillRect(x + 4, y + 4, x + 6, y + height - 3, Colours::getShade(Colour::yellow, 9), Gfx::RectFlags::none);
        }
    }
    void drawOrderBlock(Gfx::DrawingContext& context, int16_t x, int16_t y, int16_t width, int16_t height, bool selected)
    {
        stackShape(context, x, y, width, height - 3, Colours::getShade(selected ? Colour::yellow : Colour::grey, 9));
    }
    static void socketShape(Gfx::DrawingContext& context, int16_t x, int16_t y, int16_t width, int16_t height, uint8_t colour, bool boolean)
    {
        const auto radius = std::min<int>(boolean ? 4 : 2, height / 2);
        for (int row = 0; row < height; ++row)
        {
            const auto inset = std::max<int>(0, radius - std::min(row, height - row - 1));
            context.fillRect(x + inset, y + row, x + width - inset - 1, y + row, colour, Gfx::RectFlags::none);
        }
    }
    static void blockText(Gfx::DrawingContext& context, int16_t x, int16_t y, int16_t width, std::string text, bool white, Gfx::Font font = Gfx::Font::medium_bold)
    {
        text.insert(text.begin(), static_cast<char>(white ? ControlCodes::Colour::white : ControlCodes::Colour::black));
        auto renderer = Gfx::TextRenderer(context);
        renderer.setCurrentFont(font);
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
                // Native order rows keep the list background; only logic gets a block silhouette.
                continue;
            }
            const auto b = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& b) { return b.id == visual.block; });
            const auto feedback = blockFeedback(program, runtime, b->id);
            const auto colour = blockColour(b->kind, feedback);
            const auto nextClause = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& next) { return next.parent == b->id && (next.kind == Kind::otherwise || next.kind == Kind::elseIf) && indexOf(program, next.first) == indexOf(program, b->last) + 1; });
            if (visual.closing)
            {
                if (nextClause == program.blocks.end())
                {
                    stackShape(context, x, visual.top, static_cast<int16_t>(right - x), 8, Colours::getShade(colour, 3));
                }
                else
                {
                    context.fillRect(x, visual.top, x + 3, visual.top + visual.height - 1, Colours::getShade(colour, 3), Gfx::RectFlags::none);
                }
                continue;
            }
            const auto previous = std::find_if(program.blocks.begin(), program.blocks.end(), [&](const Block& previous) { return previous.id == b->parent; });
            const bool joined = (b->kind == Kind::otherwise || b->kind == Kind::elseIf) && previous != program.blocks.end() && indexOf(program, previous->last) + 1 == indexOf(program, b->first);
            stackShape(context, x, visual.top, static_cast<int16_t>(right - x), visual.height - 1, Colours::getShade(colour, 3), !joined);
            context.fillRect(x + 4, visual.top + 1, right - 5, visual.top + 1, Colours::getShade(colour, 6), Gfx::RectFlags::none);
            context.fillRect(x + 4, visual.top + visual.height - 2, right - 5, visual.top + visual.height - 2, Colours::getShade(colour, 1), Gfx::RectFlags::none);
            if (joined)
            {
                context.fillRect(x, visual.top, x + 3, visual.top + 3, Colours::getShade(colour, 3), Gfx::RectFlags::none);
            }
            if (selected == b->id)
            {
                context.fillRect(x + 4, visual.top + 4, x + 6, visual.top + visual.height - 6, Colours::getShade(Colour::yellow, 9), Gfx::RectFlags::none);
            }
            if (labels)
            {
                blockText(context, x + 9, visual.top + 3, static_cast<int16_t>(right - x - 15), b->kind == Kind::goTo && !b->parent ? "Jump if" : kindName(b->kind), true);
            }
            if (bracket(*b))
            {
                context.fillRect(x, visual.top + visual.height - 3, x + 3, visual.bottom, Colours::getShade(colour, 3), Gfx::RectFlags::none);
            }
            for (const auto& field : visual.fields)
            {
                const auto boolean = field.field == BlockField::check;
                socketShape(context, x + field.left, field.top, field.width, field.height, Colours::getShade(boolean ? colour : Colour::grey, boolean ? 2 : 9), boolean);
                context.fillRect(x + field.left + 4, field.top + 1, x + field.left + field.width - 5, field.top + 1, Colours::getShade(boolean ? colour : Colour::grey, 6), Gfx::RectFlags::none);
                context.fillRect(x + field.left + 4, field.top + field.height - 2, x + field.left + field.width - 5, field.top + field.height - 2, Colours::getShade(boolean ? colour : Colour::grey, 1), Gfx::RectFlags::none);
                if (labels)
                {
                    blockText(context, x + field.left + 5, field.top + 3, field.width - 10, field.label, boolean, Gfx::Font::medium_normal);
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
            else if (hasCondition(block))
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
    int16_t orderRowHeight(const Program* program) { return program && !program->blocks.empty() ? 14 : 10; }
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
    EditorLayout editorLayout(int16_t width, int16_t height, bool details)
    {
        return { static_cast<int16_t>(details && (width < 500 || height < 308) ? 138 : 96), 42, static_cast<int16_t>(height - (details ? 117 : 59)), static_cast<int16_t>(height - 112), static_cast<int16_t>(height - 54), static_cast<int16_t>(height - 17) };
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
        for (auto size : { Ui::Size{ 560, 320 }, Ui::Size{ 360, 240 } })
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
