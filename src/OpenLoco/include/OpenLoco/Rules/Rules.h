#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace OpenLoco::Rules
{
    using Key = uint32_t;
    enum class OwnerKind : uint8_t
    {
        vehicle,
        line
    };
    struct Owner
    {
        OwnerKind kind = OwnerKind::vehicle;
        uint16_t id = 0;
        auto operator<=>(const Owner&) const = default;
    };
    enum class Kind : uint8_t
    {
        group,
        ifBlock,
        otherwise,
        repeat,
        waitUntil,
        skip,
        goTo,
        elseIf,
        breakLoop
    };
    enum class Check : uint8_t
    {
        cargoWaiting,
        full,
        empty,
        hasSpace,
        platformFree,
        late,
        onTime,
        year
    };
    enum class Compare : uint8_t
    {
        less,
        equal,
        greater
    };
    enum class Join : uint8_t
    {
        none,
        andCheck,
        orCheck
    };
    enum class Timing : uint8_t
    {
        automatic,
        beforeTravel,
        onArrival
    };
    struct Condition
    {
        Check check = Check::full;
        Compare compare = Compare::equal;
        Key stop = 0;
        uint8_t cargo = 0;
        uint16_t platform = 0;
        uint64_t platformAnchor = 0;
        int32_t value = 0;
        bool operator==(const Condition&) const = default;
    };
    struct Row
    {
        Key key = 0;
        uint64_t raw = 0;
        bool operator==(const Row&) const = default;
    };
    struct Block
    {
        Key id = 0;
        Kind kind = Kind::group;
        Key first = 0;
        Key last = 0;
        Key parent = 0; // Conditional action parent, or preceding branch in an Else chain.
        Key target = 0;
        Condition condition{};
        Join join = Join::none;
        Condition extra{};
        uint16_t number = 1; // Repeat count (0 means forever) or wait time limit in timetable minutes.
        std::string name;
        bool enabled = true;
        Timing when = Timing::automatic;
        bool operator==(const Block&) const = default;
    };
    struct Program
    {
        std::vector<Row> rows;
        std::vector<Block> blocks;
        Key nextKey = 1;
        uint32_t revision = 0;
        bool rail = false;
        bool operator==(const Program&) const = default;
    };
    struct Result
    {
        bool ok = false;
        std::string message;
        explicit operator bool() const { return ok; }
    };
    enum class EditKind : uint8_t
    {
        addBlock,
        updateBlock,
        removeBlock,
        insertRow,
        deleteRow,
        moveRows,
        reverseRows,
        deleteRows
    };
    struct Edit
    {
        EditKind kind = EditKind::addBlock;
        Block block{};
        size_t first = 0;
        size_t last = 0;
        size_t destination = 0; // insertion boundary in the original list
        uint64_t raw = 0;
        Key into = 0; // Explicit destination bracket for a row insertion/move; not persisted.
    };
    Result validate(const Program& program);
    Result edit(Program& program, const Edit& command); // validate a copy, then atomically commit
    size_t indexOf(const Program& program, Key key);
    bool isRoutable(uint64_t raw);
    Timing timingOf(const Program& program, const Block& block);
    Key arrivalAnchor(const Program& program, const Block& block);
    Program makeProgram(const std::vector<uint64_t>& orders, bool rail);

    struct CheckResult
    {
        bool available = true;
        bool passed = false;
        std::string reason;
    };
    using Evaluate = std::function<CheckResult(const Condition&)>;
    struct Runtime
    {
        uint32_t revision = 0;
        size_t pc = 0;
        Key current = 0;
        Key waiting = 0;
        uint32_t waitStart = 0;
        uint32_t budgetTick = UINT32_MAX;
        uint16_t steps = 0;
        std::map<Key, uint16_t> repeats;
        std::map<Key, bool> branches;
        std::map<Key, bool> prechecked;
        std::set<Key> skipped;
        std::string status;
        Key awaitingArrival = 0; // Block whose entry needs physical arrival.
        Key arrived = 0;         // Physically reached routable row, consumed by the entry gate.
        Key arrivalRow = 0;      // Entry row already visited by an on-arrival gate.
    };
    enum class RunKind : uint8_t
    {
        order,
        waiting,
        yielded,
        fault,
        empty,
        departure // Arrival-only evaluation stops before decisions for a new trip.
    };
    struct Decision
    {
        RunKind kind = RunKind::empty;
        Key order = 0;
        Key block = 0;
    };
    // Arrival gates keep selecting their travel anchor until markArrived.
    // The host holds planner progression at requiresArrivalStop boundaries.
    Decision next(const Program& program, Runtime& runtime, uint32_t tick, const Evaluate& evaluate, Key arrivalOnly = 0);
    void markArrived(const Program& program, Runtime& runtime, Key order);
    bool requiresArrivalStop(const Program& program, const Runtime& runtime);
    Key executingBlock(const Program& program, const Runtime& runtime);
    const char* kindName(Kind kind);
    CheckResult evaluateBranch(const Program& program, const Runtime& runtime, const Block& block, const Evaluate& evaluate);
    void startAt(const Program& program, Runtime& runtime, Key order);
    void resumeAfter(const Program& program, Runtime& runtime, Key order);
    bool validRuntime(const Program& program, const Runtime& runtime);

    const Program* find(Owner owner);
    Program snapshot(Owner owner);
    Result submit(Owner owner, const Edit& command);
    void forget(Owner owner);
    void clear();
    Owner ownerForVehicle(uint16_t vehicle);
    void assigned(uint16_t vehicle);
    void detached(uint16_t vehicle, uint16_t line);
    void cloned(uint16_t source, uint16_t destination);
    Result noteOrderEdit(Owner owner, const Edit& command);
    void noteRawOrderChange(Owner owner, size_t index, uint64_t raw);
    Result movedPrivateOrder(uint16_t vehicle, uint16_t offset, bool up);
    Result reversePrivateOrders(uint16_t vehicle);
    bool prepareVehicle(uint16_t vehicle);
    void reached(uint16_t vehicle);
    void passed(uint16_t vehicle); // Route-planner progression, not physical arrival.
    bool planArrival(uint16_t vehicle);
    bool isApproaching(uint16_t vehicle);
    bool finishApproach(uint16_t vehicle);
    bool needsArrivalStop(uint16_t vehicle);
    bool holdingDecision(uint16_t vehicle);
    void processStationArrival(uint16_t vehicle);
    void manuallySkipped(uint16_t vehicle);
    bool holdAtStation(uint16_t vehicle);
    bool cargoOrderEnabled(uint16_t vehicle, uint16_t orderOffset);
    uint16_t timetableOrder(uint16_t vehicle, uint16_t vanillaOffset);
    bool canStopAt(uint16_t vehicle, uint16_t station);
    std::string vehicleStatus(uint16_t vehicle);
    std::string vehicleNextStop(uint16_t vehicle);
    const Runtime* runtimeForVehicle(uint16_t vehicle);
    bool saveCompanion(const std::filesystem::path& savePath);
    bool loadCompanion(const std::filesystem::path& savePath);
    std::filesystem::path companionPath(const std::filesystem::path& savePath);
}
