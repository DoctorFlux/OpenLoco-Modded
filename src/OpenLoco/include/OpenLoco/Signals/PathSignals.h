#pragma once

#include "Types.hpp"
#include <OpenLoco/Core/EnumFlags.hpp>
#include <OpenLoco/Engine/World.hpp>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

namespace OpenLoco::World::Track
{
    struct TrackConnections;
}

namespace OpenLoco::Vehicles
{
    struct VehicleHead;
}

namespace OpenLoco::Signals
{
    // Flags on a normal signal. None is a block signal. Path and chain combine, and one-way or two-way is which sides are placed.
    enum class SignalKind : uint8_t
    {
        none = 0,
        path = 1U << 0,
        chain = 1U << 1,
    };
    OPENLOCO_ENABLE_ENUM_OPERATORS(SignalKind);

    constexpr bool isPathSignal(SignalKind kind)
    {
        return (kind & SignalKind::path) != SignalKind::none;
    }

    constexpr bool isChainSignal(SignalKind kind)
    {
        return (kind & SignalKind::chain) != SignalKind::none;
    }

    constexpr uint16_t kNoReservationView = 0xFFFF;

    enum class ChainAspect : uint8_t
    {
        green,
        blue,
        red,
    };

    // Set in the flags of a status 3 result when a train is waiting for a path reservation.
    constexpr uint8_t kPathWaitFlag = 1U << 6;

    // A track piece independent of travel direction: sequence 0 origin, rotation, and track id.
    struct PieceKey
    {
        int16_t x = 0;
        int16_t y = 0;
        int16_t z = 0;
        uint8_t rotation = 0;
        uint8_t trackId = 0;

        bool operator==(const PieceKey&) const = default;
        auto operator<=>(const PieceKey&) const = default;
    };

    // One signal head: side 0 faces trains travelling forward along the piece, side 1 faces reversed travel.
    struct SideKey
    {
        PieceKey piece{};
        uint8_t side = 0;

        bool operator==(const SideKey&) const = default;
        auto operator<=>(const SideKey&) const = default;
    };

    struct ReservedStep
    {
        World::Pos3 pos{};
        uint16_t connection = 0;
    };

    enum class HoldKind : uint8_t
    {
        occupied,
        path,
    };

    struct Hold
    {
        uint16_t owner = 0;
        HoldKind kind = HoldKind::occupied;
        bool isProtected = false;
        uint8_t side = 0;
    };

    using HoldValidator = bool (*)(const PieceKey& piece, const Hold& hold);

    PieceKey canonicalPiece(const World::Pos3& entrance, uint16_t trackAndDirection);
    SideKey facingSide(const World::Pos3& entrance, uint16_t trackAndDirection);
    SideKey behindSide(const World::Pos3& entrance, uint16_t trackAndDirection);
    ReservedStep entranceOf(const SideKey& side);
    PieceKey pieceFromElement(const World::Pos3& elementPos, uint8_t rotation, uint8_t trackId, uint8_t sequenceIndex);
    bool piecesConflict(const PieceKey& a, const PieceKey& b);

    void setSignalKind(const SideKey& side, SignalKind kind);
    SignalKind getSignalKind(const SideKey& side);
    size_t specialSignalCount();
    const std::map<SideKey, SignalKind>& allSignalKinds();
    ChainAspect getChainAspect(const SideKey& side);
    void setChainAspect(const SideKey& side, ChainAspect aspect);
    ChainAspect evaluateChainAspect(bool blockBusy, size_t exitCount, size_t redExitCount);

    // Vanilla signal states: 0 green, 1 red, 2 yellow.
    enum class DisplayState : uint8_t
    {
        green = 0,
        red = 1,
        yellow = 2,
    };

    struct DisplayChoice
    {
        // A train has its route through this signal and its front has not passed it.
        bool claimed = false;
        // This is that train's last claimed signal and the train is waiting at a special signal ahead.
        bool claimEndsAtWait = false;
        bool trainWaitingHere = false;
        bool blockBusy = false;
        size_t exitCount = 0;
        size_t openExitCount = 0;
        // Visual state of the nearest block signals before and after this post.
        // Missing neighbours count as clear, matching an open line.
        bool aheadClear = true;
        bool behindClear = true;
    };

    DisplayState chooseDisplayState(SignalKind kind, const DisplayChoice& choice);
    // A two-state signal has no yellow frame; proceed is green there.
    DisplayState postStateFor(DisplayState state, uint8_t stateCount);
    // Waiting at a special signal displays red without changing packed occupancy.
    uint8_t postFrameForDisplay(SignalKind kind, bool waiting, uint8_t vanillaFrame, uint8_t frameCount);

    struct SignalClaim
    {
        uint16_t owner = 0;
        bool isLast = false;
    };

    // Each route is one train's facing signals in travel order, from just ahead of its front to the end of its reservation.
    std::map<SideKey, SignalClaim> buildSignalClaims(const std::vector<std::pair<uint16_t, std::vector<SideKey>>>& routes);

    void setHoldValidator(HoldValidator validator);
    std::optional<Hold> holdAt(const PieceKey& piece);
    bool canHold(const PieceKey& piece, uint16_t owner);
    bool isHeldByProtectedOther(const PieceKey& piece, uint16_t owner);
    void holdOccupied(uint16_t owner, const PieceKey& piece, bool isProtected);
    void releaseHold(uint16_t owner, const PieceKey& piece);
    void releaseAllHolds(uint16_t owner);
    void releaseHoldsAt(const PieceKey& piece);
    void pruneHolds(bool (*keep)(const PieceKey& piece, const Hold& hold));
    size_t pathHoldCount();
    size_t holdCount();

    bool reservePath(uint16_t owner, const std::vector<ReservedStep>& steps);
    std::vector<ReservedStep> dropPendingPath(uint16_t owner);
    const std::deque<ReservedStep>* pendingPath(uint16_t owner);
    bool consumePendingFront(uint16_t owner, const World::Pos3& entrance, uint16_t connection);
    bool isInPathMode(uint16_t owner);
    void setPathMode(uint16_t owner, bool enabled);
    void forgetTrain(uint16_t owner);
    std::vector<uint16_t> trainsWithPendingPiece(const PieceKey& piece);
    bool sideHasReservation(const SideKey& side);
    void setWaitingSignals(uint16_t owner, const std::vector<SideKey>& sides);
    void clearWaitingSignals(uint16_t owner);
    bool isSignalWaiting(const SideKey& side);
    bool isTrainWaiting(uint16_t owner);

    struct ReservationArrow
    {
        uint8_t side = 0;
        uint16_t owner = 0;

        bool operator==(const ReservationArrow&) const = default;
    };
    std::optional<ReservationArrow> reservationArrowAt(const PieceKey& piece);

    void setReservationView(uint16_t owner);
    uint16_t reservationView();
    std::optional<uint8_t> viewedTravelSide(const PieceKey& piece);
    void refreshReservationView();
    using ReservationViewDirty = void (*)(const PieceKey& piece);
    void setReservationViewDirty(ReservationViewDirty callback);

    void clearAll();
    bool saveCompanion(const std::filesystem::path& savePath);
    bool loadCompanion(const std::filesystem::path& savePath);
    std::filesystem::path companionPath(const std::filesystem::path& savePath);
    // Set after loading a version 2 file: builds that wrote it could leave route-indicator lights on special signals.
    bool companionNeedsLightRepair();
    void clearCompanionLightRepair();

    // Everything below reads or writes the loaded map and vehicles.
    struct EntryInfo
    {
        SignalKind kind = SignalKind::none;
        bool hasFacingSignal = false;
        bool blockedFromBehind = false;
        bool facingOccupied = false;
    };

    enum class ReserveStart : uint8_t
    {
        pathSignal,
        chainSignal,
        extend,
    };

    bool hasSignalSide(const PieceKey& piece, uint8_t side);
    EntryInfo classifyEntry(const World::Pos3& entrance, uint16_t connection);
    bool isRailLogicActive();
    bool isSpecialFacingSide(const World::Pos3& entrance, uint16_t trackAndDirection);
    std::optional<uint16_t> reservedConnectionAt(Vehicles::VehicleHead& head, const World::Pos3& nextPos, const World::Track::TrackConnections& tc);
    std::optional<ReserveStart> reservationStartAtJunction(const Vehicles::VehicleHead& head, const World::Pos3& nextPos, const World::Track::TrackConnections& tc);
    void onPathWait(const Vehicles::VehicleHead& head, const World::Pos3& nextPos, const World::Track::TrackConnections& tc);
    std::optional<uint16_t> reserveFrom(Vehicles::VehicleHead& head, const World::Pos3& nextPos, const World::Track::TrackConnections& tc, ReserveStart start);
    bool pieceHeldByProtectedOther(const Vehicles::VehicleHead& head, const World::Pos3& entrance, uint16_t connection);
    bool blockHasForeignPath(const Vehicles::VehicleHead& head, const World::Pos3& entrance, uint16_t connection);
    void onHeadEntersPiece(Vehicles::VehicleHead& head, const World::Pos3& entrance, uint16_t connection);
    void onTailLeavesPiece(uint16_t owner, const World::Pos3& entrance, uint16_t routing);
    void clearRouteLights(const World::Pos3& entrance, uint16_t trackAndDirection);
    void onCompanionLoaded();
    void onPassedBlockSignal(Vehicles::VehicleHead& head);
    void rebuildTrainHolds(Vehicles::VehicleHead& head);
    void onSignalPlaced(const PieceKey& piece, uint16_t sides, SignalKind kind, bool ghost);
    void onSignalSidesRemoved(const PieceKey& piece);
    void onTrackRemoved(const PieceKey& piece);
    void reset();
    void onGameLoaded();
    void prepareForSave();
    void tick();
}
