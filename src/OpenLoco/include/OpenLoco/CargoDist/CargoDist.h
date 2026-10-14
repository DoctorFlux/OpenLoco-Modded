#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace OpenLoco::CargoDist
{
    constexpr uint16_t kUnknownStation = UINT16_MAX;
    constexpr size_t kMaxPackets = 262144;
    constexpr size_t kMaxRoutes = 262144;
    enum class Mode : uint8_t
    {
        manual,
        symmetric,
        asymmetric
    };
    struct Settings
    {
        std::array<Mode, 32> modes;
        uint16_t distanceEffect = 100;
        uint8_t returning = 100;
        uint8_t accuracy = 16;
        Settings() { modes.fill(Mode::asymmetric); }
        bool operator==(const Settings&) const = default;
    };
    struct Holder
    {
        bool vehicle = false;
        uint16_t id = 0; // Station or actual cargo-component entity, never a pointer.
        uint8_t cargo = 0;
        uint8_t slot = 0; // Primary body (0) or secondary bogie (1).
        auto operator<=>(const Holder&) const = default;
    };
    struct Packet
    {
        uint32_t quantity = 0;
        uint16_t origin = kUnknownStation;
        uint16_t destination = kUnknownStation;
        uint8_t age = 0;
        int64_t credit = 0;
        int16_t originX = 0;
        int16_t originY = 0;
        bool originRemoved = false;
        bool operator==(const Packet&) const = default;
    };
    struct Holding
    {
        uint64_t identity = 0;
        std::vector<Packet> packets;
        uint32_t allocationCursor = 0;
        bool operator==(const Holding&) const = default;
    };
    struct Station
    {
        uint16_t id = 0;
        uint8_t company = 0;
        int16_t x = 0, y = 0;
        uint32_t accepts = 0;
        std::array<uint32_t, 32> supply{};
        bool operator==(const Station&) const = default;
    };
    struct Link
    {
        uint8_t company = 0, cargo = 0;
        uint16_t from = 0, to = 0;
        uint32_t capacity = 0, travel = 1, usage = 0;
        auto operator<=>(const Link&) const = default;
    };
    struct RouteKey
    {
        uint8_t company = 0, cargo = 0;
        uint16_t from = 0, to = 0;
        auto operator<=>(const RouteKey&) const = default;
    };
    struct Route
    {
        uint16_t next = 0;
        uint32_t distance = 0, capacity = 0, assigned = 0;
        bool operator==(const Route&) const = default;
    };
    struct PlannedFlow
    {
        RouteKey key;
        uint16_t next = 0;
        uint32_t quantity = 0;
    };
    struct Job
    {
        uint8_t phase = 0; // Collect services, flatten the graph, then solve routes.
        uint16_t gatherVehicle = 0;
        size_t gatherEdge = 0;
        std::map<RouteKey, Link> aggregate;
        std::vector<Link> links;
        size_t task = 0;
        uint16_t active = kUnknownStation;
        size_t edge = 0;
        std::map<uint16_t, Route> distances;
        std::set<std::pair<uint32_t, uint16_t>> queue;
        std::map<RouteKey, std::vector<Route>> result;
        bool initialised = false;
        uint16_t publishAfter = 0;
        bool operator==(const Job&) const = default;
    };
    struct Unloaded
    {
        std::vector<Packet> delivered;
        uint32_t transferred = 0;
        int64_t virtualCredit = 0;
    };
    class State
    {
    public:
        Settings settings;
        std::map<Holder, Holding> holdings;
        std::map<uint16_t, Station> stations;
        std::map<uint16_t, std::vector<Link>> services;
        std::map<RouteKey, std::vector<Route>> routes;
        std::optional<Job> job;
        std::map<uint16_t, int64_t> incomeAdjustments;
        std::map<uint16_t, std::pair<uint16_t, uint32_t>> lastVisits;
        uint32_t revision = 0;
        bool dirty = true, limited = false;
        size_t totalPackets = 0;
        bool configure(const Settings& next);
        void setStation(const Station& station);
        void removeStation(uint16_t station);
        void setService(uint16_t vehicle, const std::vector<Link>& links);
        void removeService(uint16_t vehicle);
        void forgetComponent(uint16_t component);
        void reconcile(Holder holder, uint64_t identity, uint32_t total, uint16_t origin, uint8_t age);
        void age(Holder holder);
        uint32_t quantity(Holder holder) const;
        uint32_t destined(Holder holder, uint16_t destination) const;
        size_t packetCount() const;
        void calculate(size_t budget = 4096);
        bool assign(Holder source);
        uint16_t nextHop(uint8_t company, uint8_t cargo, uint16_t from, uint16_t destination) const;
        uint32_t remainingCost(uint8_t company, uint8_t cargo, uint16_t from, uint16_t destination) const;
        bool usefulHop(uint8_t company, uint8_t cargo, uint16_t from, uint16_t next, uint16_t destination) const;
        std::vector<PlannedFlow> pendingFlows(uint8_t cargo = 32, uint8_t company = 255, uint16_t source = kUnknownStation, size_t budget = 8192) const;
        uint32_t available(Holder source, const std::set<uint16_t>& nextStops) const;
        uint32_t load(Holder source, Holder target, uint32_t capacity, const std::set<uint16_t>& nextStops);
        Unloaded unload(Holder source, uint16_t station, const std::set<uint16_t>& future, uint32_t room, const std::function<int64_t(const Packet&)>& income);
    };
    Packet split(Packet& source, uint32_t count);
    uint8_t congestionColour(uint32_t usage, uint32_t capacity);
    std::string modeName(Mode mode);
    uint64_t generation();
    const State& state();
    void reset();
    void tick();
    bool submitSettings(const Settings& settings);
    std::filesystem::path companionPath(const std::filesystem::path& save);
    bool saveCompanion(const std::filesystem::path& save);
    bool loadCompanion(const std::filesystem::path& save);
}
