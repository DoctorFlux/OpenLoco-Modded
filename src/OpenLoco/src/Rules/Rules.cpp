#include "Rules/Rules.h"
#include <algorithm>
#include <limits>
#include <set>

namespace OpenLoco::Rules
{
    size_t indexOf(const Program& p, Key key)
    {
        const auto it = std::find_if(p.rows.begin(), p.rows.end(), [key](const Row& r) { return r.key == key; });
        return static_cast<size_t>(it - p.rows.begin());
    }
    bool isRoutable(uint64_t raw)
    {
        const auto type = raw & 7;
        return type == 1 || type == 2 || type == 3;
    }
    Program makeProgram(const std::vector<uint64_t>& orders, bool rail)
    {
        Program p;
        p.rail = rail;
        for (auto raw : orders)
        {
            if ((raw & 7) != 0)
            {
                p.rows.push_back({ p.nextKey++, raw });
            }
        }
        return p;
    }
    static bool action(Kind kind) { return kind == Kind::skip || kind == Kind::goTo || kind == Kind::breakLoop; }
    static bool control(Kind kind) { return kind == Kind::ifBlock || kind == Kind::otherwise || kind == Kind::elseIf || kind == Kind::repeat; }
    static const Block* blockById(const Program& p, Key id)
    {
        const auto it = std::find_if(p.blocks.begin(), p.blocks.end(), [id](const Block& b) { return b.id == id; });
        return it == p.blocks.end() ? nullptr : &*it;
    }
    Result validate(const Program& p)
    {
        if (p.nextKey == 0 || p.blocks.size() > 256 || p.rows.size() > 63)
        {
            return { false, "This list is too large" };
        }
        std::set<Key> keys;
        for (const auto& r : p.rows)
        {
            if (r.key == 0 || !keys.insert(r.key).second || (r.raw & 7) == 0 || (r.raw & 7) > 5)
            {
                return { false, "Invalid order key or order type" };
            }
        }
        for (const auto& b : p.blocks)
        {
            if (b.id == 0 || !keys.insert(b.id).second || b.id >= p.nextKey || static_cast<unsigned>(b.kind) > static_cast<unsigned>(Kind::breakLoop))
            {
                return { false, "Invalid block key or kind" };
            }
        }
        for (const auto& r : p.rows)
        {
            if (r.key >= p.nextKey)
            {
                return { false, "Invalid next key" };
            }
        }
        for (const auto& b : p.blocks)
        {
            const auto first = indexOf(p, b.first), last = indexOf(p, b.last);
            if (first >= p.rows.size() || last >= p.rows.size() || first > last || b.name.size() > 80)
            {
                return { false, "A block needs a consecutive section of this list" };
            }
            if (b.kind == Kind::repeat && (b.number > 20))
            {
                return { false, "Repeat needs 1 to 20 times, or 0 for forever" };
            }
            if (b.kind == Kind::waitUntil && (b.number == 0 || b.number > 1440))
            {
                return { false, "Wait needs a stop or waypoint and a time limit from 1 to 1440 minutes" };
            }
            if (static_cast<unsigned>(b.join) > static_cast<unsigned>(Join::orCheck))
            {
                return { false, "Invalid joined check" };
            }
            if (b.enabled && (b.kind == Kind::ifBlock || b.kind == Kind::elseIf || b.kind == Kind::waitUntil))
            {
                for (const auto* c : { &b.condition, &b.extra })
                {
                    if (c == &b.extra && b.join == Join::none)
                    {
                        continue;
                    }
                    if (static_cast<unsigned>(c->check) > static_cast<unsigned>(Check::year) || static_cast<unsigned>(c->compare) > 2 || c->cargo >= 32 || c->value < 0)
                    {
                        return { false, "Invalid check" };
                    }
                    if (c->check == Check::platformFree && !p.rail)
                    {
                        return { false, "Platforms are only available on rail lists" };
                    }
                    if (c->check == Check::cargoWaiting || c->check == Check::platformFree || c->check == Check::late || c->check == Check::onTime)
                    {
                        auto stop = indexOf(p, c->stop);
                        if (stop >= p.rows.size() || ((p.rows[stop].raw & 7) != 1 && (p.rows[stop].raw & 7) != 2))
                        {
                            return { false, "Choose a stop on this list" };
                        }
                    }
                }
            }
            const bool clause = b.kind == Kind::otherwise || b.kind == Kind::elseIf;
            if (clause || (action(b.kind) && (b.kind != Kind::breakLoop || b.parent)))
            {
                const auto* parent = blockById(p, b.parent);
                if (!parent || (parent->kind != Kind::ifBlock && parent->kind != Kind::elseIf && !(action(b.kind) && parent->kind == Kind::otherwise)))
                {
                    return { false, "Choose an If / Else if / Else parent for this action" };
                }
                if (action(b.kind) && (b.first != parent->first || b.last != parent->last))
                {
                    return { false, "An action uses its branch's section" };
                }
                if (clause)
                {
                    if (b.parent >= b.id)
                    {
                        return { false, "Else branches must follow their parent" };
                    }
                    const bool paired = b.first == parent->first && b.last == parent->last && std::any_of(p.blocks.begin(), p.blocks.end(), [&](const Block& child) { return child.parent == parent->id && action(child.kind); });
                    if (!paired && indexOf(p, parent->last) >= first)
                    {
                        return { false, "Else / Else if must follow its parent section" };
                    }
                    for (const auto& other : p.blocks)
                    {
                        if (other.id != b.id && other.parent == b.parent && (other.kind == Kind::otherwise || other.kind == Kind::elseIf))
                        {
                            return { false, "Each branch can have one Else or Else if" };
                        }
                    }
                }
            }
            if (b.kind == Kind::breakLoop)
            {
                const auto* loop = blockById(p, b.target);
                if (!loop || loop->kind != Kind::repeat || indexOf(p, loop->first) > first || indexOf(p, loop->last) < last)
                {
                    return { false, "Break needs an enclosing Repeat loop" };
                }
            }
            if (b.kind == Kind::goTo && b.enabled)
            {
                const auto target = indexOf(p, b.target);
                if (target >= p.rows.size() || !isRoutable(p.rows[target].raw))
                {
                    return { false, "Go to needs a stop or waypoint on this list" };
                }
            }
            for (const auto& other : p.blocks)
            {
                if (action(b.kind) || action(other.kind))
                {
                    continue;
                }
                const auto a = indexOf(p, other.first), z = indexOf(p, other.last);
                if ((first < a && a <= last && last < z) || (a < first && first <= z && z < last))
                {
                    return { false, "Sections cannot cross each other" };
                }
            }
        }
        for (size_t i = 0; i < p.rows.size(); ++i)
        {
            size_t depth = 0;
            for (const auto& b : p.blocks)
            {
                if (control(b.kind) && indexOf(p, b.first) <= i && i <= indexOf(p, b.last))
                {
                    ++depth;
                }
            }
            if (depth > 4)
            {
                return { false, "Only four blocks can wrap an order" };
            }
        }
        return { true, {} };
    }
    static void resizeAttached(Program& p, const Block& old, Key first, Key last)
    {
        std::vector<Block> pending{ old };
        std::set<Key> seen;
        while (!pending.empty())
        {
            const auto parent = pending.back();
            pending.pop_back();
            if (!seen.insert(parent.id).second)
            {
                continue;
            }
            for (auto& child : p.blocks)
            {
                if (child.id == parent.id || (child.parent == parent.id && (action(child.kind) || ((child.kind == Kind::otherwise || child.kind == Kind::elseIf) && child.first == parent.first && child.last == parent.last))))
                {
                    if (child.id != parent.id)
                    {
                        pending.push_back(child);
                    }
                    child.first = first;
                    child.last = last;
                }
            }
        }
    }
    static void growInto(Program& p, const Program& before, Key into, Key first, Key last)
    {
        const auto* target = blockById(before, into);
        for (const auto& old : before.blocks)
        {
            if (action(old.kind) || indexOf(before, old.first) > indexOf(before, target->first) || indexOf(before, old.last) < indexOf(before, target->last))
            {
                continue;
            }
            const auto* current = blockById(p, old.id);
            const auto a = std::min(indexOf(p, current->first), indexOf(p, first));
            const auto z = std::max(indexOf(p, current->last), indexOf(p, last));
            resizeAttached(p, *current, p.rows[a].key, p.rows[z].key);
        }
    }
    Result edit(Program& original, const Edit& command)
    {
        Program p = original;
        if (command.into)
        {
            const auto* target = blockById(p, command.into);
            const auto boundary = command.kind == EditKind::insertRow ? command.first : command.destination;
            if ((command.kind != EditKind::moveRows && command.kind != EditKind::insertRow) || !target || action(target->kind)
                || boundary < indexOf(p, target->first) || boundary > indexOf(p, target->last) + 1)
            {
                return { false, "Choose a drop position inside this block" };
            }
            if (command.kind == EditKind::moveRows && command.first <= indexOf(p, target->first) && indexOf(p, target->last) <= command.last)
            {
                return { false, "A section cannot be dropped into itself" };
            }
        }
        switch (command.kind)
        {
            case EditKind::addBlock:
            {
                auto b = command.block;
                b.id = p.nextKey++;
                p.blocks.push_back(std::move(b));
                break;
            }
            case EditKind::updateBlock:
            {
                auto it = std::find_if(p.blocks.begin(), p.blocks.end(), [&](const Block& b) { return b.id == command.block.id; });
                if (it == p.blocks.end())
                {
                    return { false, "Block no longer exists" };
                }
                const auto old = *it;
                *it = command.block;
                resizeAttached(p, old, command.block.first, command.block.last);
                break;
            }
            case EditKind::removeBlock:
            {
                if (blockById(p, command.block.id) == nullptr)
                {
                    return { false, "Block no longer exists" };
                }
                std::set<Key> removed{ command.block.id };
                bool changed = true;
                while (changed)
                {
                    changed = false;
                    for (const auto& b : p.blocks)
                    {
                        if ((removed.contains(b.parent) || (b.kind == Kind::breakLoop && removed.contains(b.target))) && removed.insert(b.id).second)
                        {
                            changed = true;
                        }
                    }
                }
                std::erase_if(p.blocks, [&](const Block& b) { return removed.contains(b.id); });
                break;
            }
            case EditKind::insertRow:
                if (command.first > p.rows.size())
                {
                    return { false, "Order no longer exists" };
                }
                p.rows.insert(p.rows.begin() + command.first, { p.nextKey++, command.raw });
                if (command.into)
                {
                    growInto(p, original, command.into, p.rows[command.first].key, p.rows[command.first].key);
                }
                break;
            case EditKind::deleteRow:
            {
                if (command.first >= p.rows.size())
                {
                    return { false, "Order no longer exists" };
                }
                const auto removed = p.rows[command.first].key;
                for (auto& b : p.blocks)
                {
                    if (b.first == removed && b.last != removed)
                    {
                        b.first = p.rows[command.first + 1].key;
                    }
                    if (b.last == removed && b.first != removed)
                    {
                        b.last = p.rows[command.first - 1].key;
                    }
                    if (b.target == removed)
                    {
                        b.enabled = false;
                    }
                    if (b.condition.stop == removed || b.extra.stop == removed)
                    {
                        b.enabled = false;
                    }
                }
                std::set<Key> erased;
                for (const auto& b : p.blocks)
                {
                    if (b.first == removed && b.last == removed)
                    {
                        erased.insert(b.id);
                    }
                }
                bool changed = true;
                while (changed)
                {
                    changed = false;
                    for (const auto& b : p.blocks)
                    {
                        if ((erased.contains(b.parent) || (b.kind == Kind::breakLoop && erased.contains(b.target))) && erased.insert(b.id).second)
                        {
                            changed = true;
                        }
                    }
                }
                std::erase_if(p.blocks, [&](const Block& b) { return erased.contains(b.id); });
                p.rows.erase(p.rows.begin() + command.first);
                break;
            }
            case EditKind::moveRows:
            {
                if (command.first > command.last || command.last >= p.rows.size() || command.destination > p.rows.size())
                {
                    return { false, "Invalid section move" };
                }
                if (command.destination >= command.first && command.destination <= command.last + 1)
                {
                    if (!command.into)
                    {
                        return { true, {} };
                    }
                    growInto(p, original, command.into, p.rows[command.first].key, p.rows[command.last].key);
                    break;
                }
                std::vector<Row> section(p.rows.begin() + command.first, p.rows.begin() + command.last + 1);
                // Moving a row out shrinks its original brackets; moving a complete bracket keeps it intact.
                for (auto& b : p.blocks)
                {
                    auto a = indexOf(p, b.first), z = indexOf(p, b.last);
                    if (command.first <= a && z <= command.last)
                    {
                        continue;
                    }
                    if (command.first <= a && a <= command.last)
                    {
                        b.first = p.rows[command.last + 1].key;
                    }
                    if (command.first <= z && z <= command.last)
                    {
                        b.last = p.rows[command.first - 1].key;
                    }
                }
                p.rows.erase(p.rows.begin() + command.first, p.rows.begin() + command.last + 1);
                auto dest = command.destination;
                if (dest > command.last)
                {
                    dest -= section.size();
                }
                p.rows.insert(p.rows.begin() + dest, section.begin(), section.end());
                if (command.into)
                {
                    growInto(p, original, command.into, section.front().key, section.back().key);
                }
                if (command.first == command.last && command.block.id == 0)
                {
                    std::erase_if(p.blocks, [&](const Block& b) { return b.kind == Kind::group && b.first == section.front().key && b.last == section.front().key; });
                }
                break;
            }
            case EditKind::reverseRows:
                std::reverse(p.rows.begin(), p.rows.end());
                for (auto& b : p.blocks)
                {
                    std::swap(b.first, b.last);
                }
                break;
            case EditKind::deleteRows:
            {
                if (command.first > command.last || command.last >= p.rows.size())
                {
                    return { false, "Invalid section deletion" };
                }
                for (size_t i = command.last + 1; i > command.first; --i)
                {
                    Edit remove;
                    remove.kind = EditKind::deleteRow;
                    remove.first = i - 1;
                    const auto result = edit(p, remove);
                    if (!result)
                    {
                        return result;
                    }
                }
                break;
            }
        }
        // Deleted check targets are deliberately unavailable, rather than silently rebound.
        auto checkable = p;
        for (auto& b : checkable.blocks)
        {
            if (!b.enabled)
            {
                b.condition = {};
                b.extra = {};
                b.join = Join::none;
            }
        }
        const auto result = validate(checkable);
        if (!result)
        {
            return result;
        }
        ++p.revision;
        original = std::move(p);
        return { true, {} };
    }

    namespace
    {
        enum class Op
        {
            row,
            check,
            otherwise,
            repeatStart,
            repeatEnd,
            wait,
            skip,
            goTo,
            breakLoop
        };
        struct Instruction
        {
            Op op;
            Key key;
            size_t jump = 0;
        };
        struct Code
        {
            std::vector<Instruction> instructions;
            std::map<Key, size_t> entries;
        };
        Code compile(const Program& p)
        {
            Code code;
            std::map<Key, size_t> starts;
            std::map<Key, size_t> rowEnds;
            for (size_t i = 0; i < p.rows.size(); ++i)
            {
                code.entries[p.rows[i].key] = code.instructions.size();
                std::vector<const Block*> opening, closing;
                for (const auto& b : p.blocks)
                {
                    if (action(b.kind) || b.kind == Kind::group)
                    {
                        continue;
                    }
                    if (indexOf(p, b.first) == i && b.kind != Kind::waitUntil)
                    {
                        opening.push_back(&b);
                    }
                    if (indexOf(p, b.last) == i)
                    {
                        closing.push_back(&b);
                    }
                }
                std::sort(opening.begin(), opening.end(), [&](const Block* a, const Block* b) {
                    const auto al = indexOf(p, a->last), bl = indexOf(p, b->last);
                    return al != bl ? al > bl : a->id < b->id;
                });
                for (auto* b : opening)
                {
                    starts[b->id] = code.instructions.size();
                    auto op = b->kind == Kind::repeat ? Op::repeatStart : b->kind == Kind::otherwise ? Op::otherwise
                                                                                                     : Op::check;
                    code.instructions.push_back({ op, b->id });
                    bool hasActions = false;
                    for (const auto& child : p.blocks)
                    {
                        if (child.parent == b->id && action(child.kind))
                        {
                            code.instructions.push_back({ child.kind == Kind::skip ? Op::skip : child.kind == Kind::breakLoop ? Op::breakLoop
                                                                                                                              : Op::goTo,
                                                          child.id });
                            hasActions = true;
                        }
                    }
                    if (hasActions)
                    {
                        code.instructions[starts[b->id]].jump = code.instructions.size();
                    }
                }
                for (const auto& block : p.blocks)
                {
                    if (block.kind == Kind::breakLoop && !block.parent && indexOf(p, block.first) == i)
                    {
                        code.instructions.push_back({ Op::breakLoop, block.id });
                    }
                }
                code.instructions.push_back({ Op::row, p.rows[i].key });
                rowEnds[p.rows[i].key] = code.instructions.size();
                std::sort(closing.begin(), closing.end(), [&](const Block* a, const Block* b) {
                    const auto af = indexOf(p, a->first), bf = indexOf(p, b->first);
                    return af != bf ? af > bf : a->id > b->id;
                });
                for (auto* b : closing)
                {
                    if (b->kind == Kind::waitUntil)
                    {
                        code.instructions.push_back({ Op::wait, b->id });
                    }
                    else if (b->kind == Kind::repeat)
                    {
                        code.instructions.push_back({ Op::repeatEnd, b->id, starts[b->id] + 1 });
                    }
                    else if (code.instructions[starts[b->id]].jump == 0)
                    {
                        code.instructions[starts[b->id]].jump = code.instructions.size();
                    }
                }
            }
            for (auto& instruction : code.instructions)
            {
                if (instruction.op == Op::breakLoop)
                {
                    const auto* block = blockById(p, instruction.key);
                    for (size_t i = 0; i < code.instructions.size(); ++i)
                    {
                        if (code.instructions[i].op == Op::repeatEnd && code.instructions[i].key == block->target)
                        {
                            instruction.jump = i + 1;
                        }
                    }
                    continue;
                }
                if (instruction.op != Op::skip)
                {
                    continue;
                }
                const auto* b = blockById(p, instruction.key);
                const auto after = indexOf(p, b->last) + 1;
                instruction.jump = after == p.rows.size() ? code.instructions.size() : code.entries[p.rows[after].key];
                // Skip means this pass. Preserve the enclosing Repeat's end instruction.
                for (size_t i = rowEnds[b->last]; i < instruction.jump; ++i)
                {
                    const auto& close = code.instructions[i];
                    if (close.op != Op::repeatEnd)
                    {
                        continue;
                    }
                    const auto* repeat = blockById(p, close.key);
                    if (indexOf(p, repeat->first) <= indexOf(p, b->first) && indexOf(p, repeat->last) >= indexOf(p, b->last))
                    {
                        instruction.jump = i;
                        break;
                    }
                }
            }
            return code;
        }
        CheckResult evaluateBlock(const Block& b, const Evaluate& evaluate)
        {
            if (!b.enabled)
            {
                return { false, false, "This block has a missing stop or destination" };
            }
            auto first = evaluate(b.condition);
            if (b.join == Join::none)
            {
                return first;
            }
            auto second = evaluate(b.extra);
            if (!first.available || !second.available)
            {
                return { false, false, first.available ? second.reason : first.reason };
            }
            first.passed = b.join == Join::andCheck ? first.passed && second.passed : first.passed || second.passed;
            return first;
        }
    }
    const char* kindName(Kind kind)
    {
        constexpr const char* names[] = { "Group", "If", "Else", "Repeat", "Wait until", "Skip", "Go to", "Else if", "Break" };
        return static_cast<size_t>(kind) < std::size(names) ? names[static_cast<size_t>(kind)] : "Unavailable";
    }
    Key executingBlock(const Program& program, const Runtime& runtime)
    {
        if (runtime.revision != program.revision)
        {
            return 0;
        }
        if (runtime.waiting)
        {
            return runtime.waiting;
        }
        Key cursor = runtime.current;
        if (runtime.steps == 256)
        {
            const auto code = compile(program);
            if (runtime.pc < code.instructions.size())
            {
                if (code.instructions[runtime.pc].op != Op::row)
                {
                    return code.instructions[runtime.pc].key;
                }
                cursor = code.instructions[runtime.pc].key;
            }
        }
        Key selected = 0;
        size_t smallest = program.rows.size() + 1;
        const auto current = indexOf(program, cursor);
        for (const auto& block : program.blocks)
        {
            if (action(block.kind))
            {
                continue;
            }
            const auto first = indexOf(program, block.first), last = indexOf(program, block.last);
            const auto branch = runtime.branches.find(block.id);
            if (current < first || current > last || (branch != runtime.branches.end() && !branch->second))
            {
                continue;
            }
            if (last - first <= smallest)
            {
                selected = block.id;
                smallest = last - first;
            }
        }
        return selected;
    }
    CheckResult evaluateBranch(const Program& p, const Runtime& r, const Block& b, const Evaluate& evaluate)
    {
        bool taken = false;
        if (b.kind == Kind::otherwise || b.kind == Kind::elseIf)
        {
            auto parent = b.parent;
            for (size_t i = 0; parent && i < p.blocks.size(); ++i)
            {
                const auto choice = r.branches.find(parent);
                taken |= choice != r.branches.end() && choice->second;
                const auto* previous = blockById(p, parent);
                parent = previous && previous->kind == Kind::elseIf ? previous->parent : 0;
            }
            if (taken)
            {
                return { true, false, {} };
            }
            if (b.kind == Kind::otherwise)
            {
                return { b.enabled, b.enabled, b.enabled ? "" : "Else is unavailable" };
            }
        }
        auto result = evaluateBlock(b, evaluate);
        for (const auto& child : p.blocks)
        {
            if (child.parent == b.id && child.kind == Kind::goTo && !child.enabled)
            {
                return { false, false, "Go to destination is missing" };
            }
        }
        return result;
    }
    void startAt(const Program& p, Runtime& r, Key order)
    {
        r = {};
        r.revision = p.revision;
        const auto code = compile(p);
        const auto it = code.entries.find(order);
        if (it != code.entries.end())
        {
            r.pc = it->second;
        }
    }
    void resumeAfter(const Program& p, Runtime& r, Key order)
    {
        startAt(p, r, order);
        const auto code = compile(p);
        for (size_t i = 0; i < code.instructions.size(); ++i)
        {
            if (code.instructions[i].op == Op::row && code.instructions[i].key == order)
            {
                r.pc = i + 1;
                r.current = order;
                return;
            }
        }
    }
    Decision next(const Program& p, Runtime& r, uint32_t tick, const Evaluate& evaluate)
    {
        if (p.rows.empty())
        {
            return {};
        }
        if (r.revision != p.revision)
        {
            startAt(p, r, r.current);
        }
        const auto code = compile(p);
        if (r.waiting != 0 && r.budgetTick == tick)
        {
            return { RunKind::waiting, r.current, r.waiting };
        }
        if (r.budgetTick != tick)
        {
            r.budgetTick = tick;
            r.steps = 0;
        }
        while (r.steps < 256)
        {
            ++r.steps;
            if (r.pc >= code.instructions.size())
            {
                r.pc = 0;
                r.repeats.clear();
                r.branches.clear();
            }
            const auto instruction = code.instructions[r.pc];
            const auto* b = instruction.op == Op::row ? nullptr : blockById(p, instruction.key);
            switch (instruction.op)
            {
                case Op::row:
                    if (r.status == "Choosing the next stop..." || r.status == "Waiting until the check passes")
                    {
                        r.status.clear();
                    }
                    ++r.pc;
                    r.current = instruction.key;
                    return { RunKind::order, instruction.key, 0 };
                case Op::check:
                case Op::otherwise:
                {
                    auto result = r.prechecked.contains(b->id) ? CheckResult{ true, r.prechecked[b->id], {} } : evaluateBranch(p, r, *b, evaluate);
                    r.prechecked.erase(b->id);
                    r.branches[b->id] = result.available && result.passed;
                    if (!result.available)
                    {
                        r.status = result.reason.empty() ? "Check unavailable" : result.reason;
                    }
                    if (!r.branches[b->id])
                    {
                        for (size_t i = r.pc + 1; i < instruction.jump; ++i)
                        {
                            if (code.instructions[i].op == Op::row)
                            {
                                r.skipped.insert(code.instructions[i].key);
                            }
                        }
                    }
                    r.pc = r.branches[b->id] ? r.pc + 1 : instruction.jump;
                    break;
                }
                case Op::repeatStart:
                    std::erase_if(r.branches, [&](const auto& branch) {
                        const auto* inner = blockById(p, branch.first);
                        return inner && indexOf(p, b->first) <= indexOf(p, inner->first) && indexOf(p, inner->last) <= indexOf(p, b->last);
                    });
                    r.repeats[b->id] = b->number;
                    ++r.pc;
                    break;
                case Op::repeatEnd:
                    if (r.repeats.contains(b->id) && (r.repeats[b->id] == 0 || r.repeats[b->id] > 1))
                    {
                        if (r.repeats[b->id])
                        {
                            --r.repeats[b->id];
                        }
                        std::erase_if(r.branches, [&](const auto& branch) {
                            const auto* inner = blockById(p, branch.first);
                            return inner && indexOf(p, b->first) <= indexOf(p, inner->first) && indexOf(p, inner->last) <= indexOf(p, b->last);
                        });
                        r.pc = instruction.jump;
                    }
                    else
                    {
                        r.repeats.erase(b->id);
                        ++r.pc;
                    }
                    break;
                case Op::wait:
                {
                    const auto result = evaluateBlock(*b, evaluate);
                    r.branches[b->id] = result.available && result.passed;
                    if (result.available && result.passed)
                    {
                        r.status.clear();
                        r.waiting = 0;
                        ++r.pc;
                        break;
                    }
                    if (r.waiting != b->id)
                    {
                        r.waiting = b->id;
                        r.waitStart = tick;
                    }
                    if (static_cast<uint32_t>(tick - r.waitStart) >= static_cast<uint32_t>(b->number) * 40)
                    {
                        r.waiting = 0;
                        r.status = "Time limit reached";
                        ++r.pc;
                        break;
                    }
                    r.status = result.available ? "Waiting until the check passes" : result.reason;
                    return { RunKind::waiting, r.current, b->id };
                }
                case Op::skip:
                    for (size_t i = r.pc + 1; i < instruction.jump; ++i)
                    {
                        if (code.instructions[i].op == Op::row)
                        {
                            r.skipped.insert(code.instructions[i].key);
                        }
                    }
                    r.pc = instruction.jump;
                    break;
                case Op::breakLoop:
                {
                    const auto* loop = blockById(p, b->target);
                    for (size_t i = indexOf(p, b->first); i <= indexOf(p, loop->last); ++i)
                    {
                        r.skipped.insert(p.rows[i].key);
                    }
                    std::erase_if(r.repeats, [&](const auto& frame) {
                        const auto* inner = blockById(p, frame.first);
                        return inner && indexOf(p, loop->first) <= indexOf(p, inner->first) && indexOf(p, inner->last) <= indexOf(p, loop->last);
                    });
                    r.pc = instruction.jump;
                    break;
                }
                case Op::goTo:
                {
                    if (!b->enabled)
                    {
                        r.status = "Go to destination is missing";
                        ++r.pc;
                        break;
                    }
                    auto target = code.entries.find(b->target);
                    if (target == code.entries.end())
                    {
                        r.status = "Go to destination is missing";
                        return { RunKind::fault, r.current, b->id };
                    }
                    for (const auto& row : p.rows)
                    {
                        if (row.key != b->target && isRoutable(row.raw))
                        {
                            r.skipped.insert(row.key);
                        }
                    }
                    // Retain a repeat only when the destination is still inside it.
                    std::erase_if(r.repeats, [&](const auto& frame) {
                        const auto* repeat = blockById(p, frame.first);
                        return indexOf(p, b->target) < indexOf(p, repeat->first) || indexOf(p, b->target) > indexOf(p, repeat->last);
                    });
                    r.pc = target->second;
                    break;
                }
            }
        }
        r.status = "Choosing the next stop...";
        return { RunKind::yielded, r.current, 0 };
    }
    bool validRuntime(const Program& p, const Runtime& r)
    {
        const auto code = compile(p);
        if (r.revision != p.revision || r.pc > code.instructions.size() || r.steps > 256 || r.repeats.size() > 4 || r.branches.size() > 256 || r.status.size() > 256)
        {
            return false;
        }
        if (r.current && indexOf(p, r.current) >= p.rows.size())
        {
            return false;
        }
        if (r.waiting)
        {
            const auto* b = blockById(p, r.waiting);
            if (!b || b->kind != Kind::waitUntil || r.pc >= code.instructions.size() || code.instructions[r.pc].op != Op::wait || code.instructions[r.pc].key != r.waiting)
            {
                return false;
            }
        }
        for (auto [key, count] : r.repeats)
        {
            const auto* b = blockById(p, key);
            if (!b || b->kind != Kind::repeat || (b->number == 0 ? count != 0 : count < 1 || count > b->number))
            {
                return false;
            }
        }
        for (auto [key, passed] : r.branches)
        {
            const auto* b = blockById(p, key);
            if (!b || (b->kind != Kind::ifBlock && b->kind != Kind::otherwise && b->kind != Kind::elseIf && b->kind != Kind::waitUntil))
            {
                return false;
            }
        }
        for (auto [key, passed] : r.prechecked)
        {
            const auto* b = blockById(p, key);
            if (!b || (b->kind != Kind::ifBlock && b->kind != Kind::otherwise && b->kind != Kind::elseIf))
            {
                return false;
            }
        }
        for (auto key : r.skipped)
        {
            if (indexOf(p, key) >= p.rows.size())
            {
                return false;
            }
        }
        return true;
    }
}
