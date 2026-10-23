#include "Signals/PathSignals.h"

#include "Graphics/Colour.h"
#include "Map/QuarterTile.h"
#include "Map/Tile.h"
#include "Map/Track/Track.h"
#include "Map/Track/TrackData.h"
#include <OpenLoco/Math/Vector.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace OpenLoco::Signals
{
    namespace
    {
        struct PieceTile
        {
            int16_t x;
            int16_t y;
            int16_t zLow;
            int16_t zHigh;
            uint8_t quarters;
        };

        struct TileRef
        {
            PieceKey piece;
            int16_t zLow;
            int16_t zHigh;
            uint8_t quarters;
        };

        struct TrainState
        {
            std::deque<ReservedStep> pending;
            // The whole section becomes non-yieldable when planning commits
            // its entrance. Keep protection even when execution is replanned.
            std::set<PieceKey> committed;
            bool pathMode = false;
            bool departureWaiting = false; // Saved with priority; revalidated before movement.
        };

        constexpr uint8_t kTrackIdCount = 44;
        constexpr uint16_t kBasicTaDMask = World::Track::AdditionalTaDFlags::basicTaDMask;
        constexpr size_t kMaxCompanionEntries = 100000;
        constexpr size_t kMaxCompanionSteps = 4096;

        std::map<SideKey, SignalKind> _kinds;
        std::map<SideKey, ChainAspect> _chainAspects;
        std::map<PieceKey, Hold> _holds;
        std::map<uint32_t, std::vector<TileRef>> _tiles;
        std::map<uint16_t, TrainState> _trains;
        size_t _pathHolds = 0;
        HoldValidator _validator = nullptr;
        uint16_t _viewedOwner = kNoReservationView;
        std::map<PieceKey, uint8_t> _viewedSides;
        std::map<PieceKey, ReservationArrow> _allReserved;
        std::map<SideKey, std::set<uint16_t>> _waitingSignals;
        std::map<SideKey, std::set<uint16_t>> _approachingSignals;
        std::map<uint16_t, uint32_t> _approachProbeTicks;
        bool _lightRepairPending = false;
        ReservationViewDirty _viewDirty = nullptr;

        void refreshViewedReservation();

        World::Pos3 otherEnd(const World::Pos3& entrance, uint16_t tad)
        {
            const auto& trackSize = World::TrackData::getUnkTrack(tad & kBasicTaDMask);
            auto pos = entrance + trackSize.pos;
            if (trackSize.rotationEnd < 12)
            {
                pos -= World::Pos3{ World::kRotationOffset[trackSize.rotationEnd], 0 };
            }
            return pos;
        }

        std::vector<PieceTile> tilesOf(const PieceKey& piece)
        {
            std::vector<PieceTile> tiles;
            if (piece.trackId >= kTrackIdCount)
            {
                return tiles;
            }
            for (const auto& part : World::TrackData::getTrackPiece(piece.trackId))
            {
                const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, piece.rotation);
                const auto zLow = static_cast<int16_t>(piece.z + part.z);
                const auto zHigh = static_cast<int16_t>(zLow + part.clearZ + 32);
                const auto quarters = part.subTileClearance.rotate(piece.rotation).getBaseQuarterOccupied();
                tiles.push_back(PieceTile{ static_cast<int16_t>(piece.x + offset.x), static_cast<int16_t>(piece.y + offset.y), zLow, zHigh, quarters });
            }
            return tiles;
        }

        uint32_t tileKey(int16_t x, int16_t y)
        {
            return (static_cast<uint32_t>(static_cast<uint16_t>(x)) << 16) | static_cast<uint16_t>(y);
        }

        bool overlaps(const PieceTile& tile, const TileRef& ref)
        {
            return (tile.quarters & ref.quarters) != 0 && tile.zLow < ref.zHigh && ref.zLow < tile.zHigh;
        }

        void insertTileRefs(const PieceKey& piece)
        {
            for (const auto& tile : tilesOf(piece))
            {
                _tiles[tileKey(tile.x, tile.y)].push_back(TileRef{ piece, tile.zLow, tile.zHigh, tile.quarters });
            }
        }

        void eraseTileRefs(const PieceKey& piece)
        {
            for (const auto& tile : tilesOf(piece))
            {
                const auto found = _tiles.find(tileKey(tile.x, tile.y));
                if (found == _tiles.end())
                {
                    continue;
                }
                auto& refs = found->second;
                refs.erase(std::remove_if(refs.begin(), refs.end(), [&piece](const TileRef& ref) { return ref.piece == piece; }), refs.end());
                if (refs.empty())
                {
                    _tiles.erase(found);
                }
            }
        }

        void eraseHold(const PieceKey& piece)
        {
            const auto found = _holds.find(piece);
            if (found == _holds.end())
            {
                return;
            }
            if (found->second.kind == HoldKind::path)
            {
                _pathHolds--;
            }
            _holds.erase(found);
            eraseTileRefs(piece);
        }

        void setHold(const PieceKey& piece, const Hold& hold)
        {
            const auto found = _holds.find(piece);
            if (found == _holds.end())
            {
                _holds.emplace(piece, hold);
                insertTileRefs(piece);
                if (hold.kind == HoldKind::path)
                {
                    _pathHolds++;
                }
                return;
            }
            if (found->second.kind == HoldKind::path)
            {
                _pathHolds--;
            }
            found->second = hold;
            if (hold.kind == HoldKind::path)
            {
                _pathHolds++;
            }
        }

        bool isFreeFor(const PieceKey& piece, uint16_t owner, bool protectedOnly, bool ignorePending = false)
        {
            std::vector<PieceKey> candidates;
            for (const auto& tile : tilesOf(piece))
            {
                const auto found = _tiles.find(tileKey(tile.x, tile.y));
                if (found == _tiles.end())
                {
                    continue;
                }
                for (const auto& ref : found->second)
                {
                    if (ref.piece == piece || overlaps(tile, ref))
                    {
                        candidates.push_back(ref.piece);
                    }
                }
            }
            std::sort(candidates.begin(), candidates.end());
            candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

            for (const auto& candidate : candidates)
            {
                const auto found = _holds.find(candidate);
                if (found == _holds.end())
                {
                    continue;
                }
                const Hold hold = found->second;
                if (hold.owner == owner)
                {
                    continue;
                }
                if (protectedOnly && !hold.isProtected && hold.kind != HoldKind::path)
                {
                    continue;
                }
                if (_validator != nullptr && !_validator(candidate, hold))
                {
                    eraseHold(candidate);
                    continue;
                }
                if (ignorePending && hold.kind == HoldKind::path && !isCommittedRoutePiece(hold.owner, candidate))
                {
                    continue;
                }
                return false;
            }
            return true;
        }

        std::vector<ReservedStep> dropPendingInternal(uint16_t owner)
        {
            std::vector<ReservedStep> dropped;
            const auto found = _trains.find(owner);
            if (found == _trains.end())
            {
                return dropped;
            }
            for (const auto& step : found->second.pending)
            {
                const auto piece = canonicalPiece(step.pos, step.connection);
                const auto hold = _holds.find(piece);
                if (hold != _holds.end() && hold->second.owner == owner && hold->second.kind == HoldKind::path && !found->second.committed.contains(piece))
                {
                    eraseHold(piece);
                }
                dropped.push_back(step);
            }
            found->second.pending.clear();
            refreshViewedReservation();
            return dropped;
        }
    }

    PieceKey canonicalPiece(const World::Pos3& entrance, uint16_t trackAndDirection)
    {
        const uint16_t tad = trackAndDirection & kBasicTaDMask;
        const auto origin = (tad & (1U << 2)) ? otherEnd(entrance, tad) : entrance;
        return PieceKey{ origin.x, origin.y, origin.z, static_cast<uint8_t>(tad & 0x3), static_cast<uint8_t>((tad >> 3) & 0x3F) };
    }

    SideKey facingSide(const World::Pos3& entrance, uint16_t trackAndDirection)
    {
        return SideKey{ canonicalPiece(entrance, trackAndDirection), static_cast<uint8_t>((trackAndDirection & (1U << 2)) ? 1 : 0) };
    }

    SideKey behindSide(const World::Pos3& entrance, uint16_t trackAndDirection)
    {
        return SideKey{ canonicalPiece(entrance, trackAndDirection), static_cast<uint8_t>((trackAndDirection & (1U << 2)) ? 0 : 1) };
    }

    ReservedStep entranceOf(const SideKey& side)
    {
        const uint16_t forward = static_cast<uint16_t>((side.piece.trackId << 3) | side.piece.rotation);
        const World::Pos3 origin{ side.piece.x, side.piece.y, side.piece.z };
        if (side.side == 0)
        {
            return ReservedStep{ origin, forward };
        }
        return ReservedStep{ otherEnd(origin, forward), static_cast<uint16_t>(forward | (1U << 2)) };
    }

    PieceKey pieceFromElement(const World::Pos3& elementPos, uint8_t rotation, uint8_t trackId, uint8_t sequenceIndex)
    {
        if (trackId >= kTrackIdCount)
        {
            return PieceKey{ elementPos.x, elementPos.y, elementPos.z, rotation, trackId };
        }
        const auto pieces = World::TrackData::getTrackPiece(trackId);
        if (sequenceIndex >= pieces.size())
        {
            return PieceKey{ elementPos.x, elementPos.y, elementPos.z, rotation, trackId };
        }
        const auto& part = pieces[sequenceIndex];
        const auto offset = Math::Vector::rotate(World::Pos2{ part.x, part.y }, rotation);
        return PieceKey{ static_cast<int16_t>(elementPos.x - offset.x), static_cast<int16_t>(elementPos.y - offset.y), static_cast<int16_t>(elementPos.z - part.z), rotation, trackId };
    }

    bool piecesConflict(const PieceKey& a, const PieceKey& b)
    {
        if (a == b)
        {
            return true;
        }
        const auto tilesA = tilesOf(a);
        for (const auto& tileB : tilesOf(b))
        {
            const TileRef refB{ b, tileB.zLow, tileB.zHigh, tileB.quarters };
            for (const auto& tileA : tilesA)
            {
                if (tileA.x == tileB.x && tileA.y == tileB.y && overlaps(tileA, refB))
                {
                    return true;
                }
            }
        }
        return false;
    }

    void setSignalKind(const SideKey& side, SignalKind kind)
    {
        const auto flags = kind & (SignalKind::path | SignalKind::chain);
        if (flags == SignalKind::none)
        {
            _kinds.erase(side);
            _chainAspects.erase(side);
            return;
        }
        _kinds[side] = flags;
        if (!isChainSignal(flags))
        {
            _chainAspects.erase(side);
        }
    }

    SignalKind getSignalKind(const SideKey& side)
    {
        if (_kinds.empty())
        {
            return SignalKind::none;
        }
        const auto found = _kinds.find(side);
        return found == _kinds.end() ? SignalKind::none : found->second;
    }

    size_t specialSignalCount()
    {
        return _kinds.size();
    }

    const std::map<SideKey, SignalKind>& allSignalKinds()
    {
        return _kinds;
    }

    ChainAspect getChainAspect(const SideKey& side)
    {
        const auto found = _chainAspects.find(side);
        return found == _chainAspects.end() ? ChainAspect::green : found->second;
    }

    void setChainAspect(const SideKey& side, ChainAspect aspect)
    {
        _chainAspects[side] = aspect;
    }

    ChainAspect evaluateChainAspect(bool blockBusy, size_t exitCount, size_t redExitCount)
    {
        if (blockBusy)
        {
            return ChainAspect::red;
        }
        if (exitCount == 0 || redExitCount == 0)
        {
            return ChainAspect::green;
        }
        if (redExitCount >= exitCount)
        {
            return ChainAspect::red;
        }
        return ChainAspect::blue;
    }

    DisplayState chooseDisplayState(SignalKind kind, const DisplayChoice& choice)
    {
        if (choice.claimed)
        {
            return choice.claimEndsAtWait ? DisplayState::yellow : DisplayState::green;
        }
        if (choice.trainWaitingHere)
        {
            return DisplayState::red;
        }
        // Path, chain, and combined posts rest like the block signals beside them.
        // Green when those lamps are green. Red when this piece is occupied, a
        // neighbour is red, or every exit ahead is closed.
        if (isPathSignal(kind) || isChainSignal(kind))
        {
            if (choice.blockBusy || !choice.aheadClear || !choice.behindClear)
            {
                return DisplayState::red;
            }
            const bool exitOpen = choice.exitCount == 0 || choice.openExitCount > 0;
            return exitOpen ? DisplayState::green : DisplayState::red;
        }
        return DisplayState::green;
    }

    uint8_t postStateFor(DisplayState state, uint8_t stateCount)
    {
        if (stateCount < 2 || state == DisplayState::red)
        {
            return 0;
        }
        if (state == DisplayState::yellow)
        {
            return 1; // Two-state posts use their proceed state for caution.
        }
        return std::min<uint8_t>(3, stateCount - 1);
    }

    uint8_t postFrameForDisplay(SignalKind kind, bool waiting, uint8_t vanillaFrame, uint8_t frameCount, bool blockOccupied)
    {
        const bool blocked = (kind != SignalKind::none && waiting) || (!isPathSignal(kind) && blockOccupied);
        if (!blocked || frameCount == 0)
        {
            return vanillaFrame;
        }
        // Native posts start at danger. In particular SIGC3 frame 3 is yellow;
        // its red frame is 0, as for the other native signal families.
        return 0;
    }

    Colour signalArrowColour(SignalKind kind, bool /*bothDirections*/)
    {
        if (isPathSignal(kind) && isChainSignal(kind))
        {
            return Colour::orange;
        }
        if (isPathSignal(kind))
        {
            return Colour::yellow;
        }
        if (isChainSignal(kind))
        {
            return Colour::darkBlue;
        }
        return Colour::grey;
    }

    std::map<SideKey, SignalClaim> buildSignalClaims(const std::vector<std::pair<uint16_t, std::vector<SideKey>>>& routes)
    {
        std::map<SideKey, SignalClaim> claims;
        for (const auto& [owner, sides] : routes)
        {
            for (size_t i = 0; i < sides.size(); ++i)
            {
                claims.emplace(sides[i], SignalClaim{ owner, i + 1 == sides.size() });
            }
        }
        return claims;
    }

    void setHoldValidator(HoldValidator validator)
    {
        _validator = validator;
    }

    std::optional<Hold> holdAt(const PieceKey& piece)
    {
        const auto found = _holds.find(piece);
        if (found == _holds.end())
        {
            return std::nullopt;
        }
        return found->second;
    }

    bool canHold(const PieceKey& piece, uint16_t owner)
    {
        return isFreeFor(piece, owner, false);
    }

    bool hasCommittedRoutes()
    {
        return std::any_of(_trains.begin(), _trains.end(), [](const auto& entry) { return !entry.second.committed.empty(); });
    }

    bool isCommittedRoutePiece(uint16_t owner, const PieceKey& piece)
    {
        const auto found = _trains.find(owner);
        return found != _trains.end() && found->second.committed.contains(piece);
    }

    bool canHoldIgnoringPending(const PieceKey& piece, uint16_t owner)
    {
        return isFreeFor(piece, owner, false, true);
    }

    std::vector<uint16_t> conflictingPendingOwners(uint16_t owner, const std::vector<ReservedStep>& steps)
    {
        std::set<uint16_t> owners;
        for (const auto& step : steps)
        {
            const auto piece = canonicalPiece(step.pos, step.connection);
            if (piece.trackId >= kTrackIdCount)
            {
                continue;
            }
            for (const auto& tile : tilesOf(piece))
            {
                const auto found = _tiles.find(tileKey(tile.x, tile.y));
                if (found == _tiles.end())
                {
                    continue;
                }
                for (const auto& ref : found->second)
                {
                    if (ref.piece != piece && !overlaps(tile, ref))
                    {
                        continue;
                    }
                    const auto held = _holds.find(ref.piece);
                    if (held != _holds.end() && held->second.owner != owner && held->second.kind == HoldKind::path
                        && (!_validator || _validator(ref.piece, held->second)))
                    {
                        owners.insert(held->second.owner);
                    }
                }
            }
        }
        return { owners.begin(), owners.end() };
    }

    bool isHeldByProtectedOther(const PieceKey& piece, uint16_t owner)
    {
        if (_holds.empty())
        {
            return false;
        }
        return !isFreeFor(piece, owner, true);
    }

    void holdOccupied(uint16_t owner, const PieceKey& piece, bool isProtected)
    {
        const auto found = _holds.find(piece);
        if (found != _holds.end() && found->second.owner != owner)
        {
            return;
        }
        const bool wasProtected = found != _holds.end() && found->second.isProtected;
        const uint8_t side = found == _holds.end() ? 0 : found->second.side;
        setHold(piece, Hold{ owner, HoldKind::occupied, isProtected || wasProtected, side });
    }

    void releaseHold(uint16_t owner, const PieceKey& piece)
    {
        const auto found = _holds.find(piece);
        if (found == _holds.end() || found->second.owner != owner)
        {
            return;
        }
        eraseHold(piece);
        if (auto state = _trains.find(owner); state != _trains.end())
        {
            state->second.committed.erase(piece);
        }
        refreshViewedReservation();
    }

    void releaseAllHolds(uint16_t owner)
    {
        if (auto state = _trains.find(owner); state != _trains.end())
        {
            state->second.committed.clear();
        }
        std::vector<PieceKey> owned;
        for (const auto& [piece, hold] : _holds)
        {
            if (hold.owner == owner)
            {
                owned.push_back(piece);
            }
        }
        for (const auto& piece : owned)
        {
            eraseHold(piece);
        }
        if (!owned.empty())
        {
            refreshViewedReservation();
        }
    }

    void releaseHoldsAt(const PieceKey& piece)
    {
        for (auto& [owner, state] : _trains)
        {
            state.committed.erase(piece);
        }
        eraseHold(piece);
        refreshViewedReservation();
    }

    void pruneHolds(bool (*keep)(const PieceKey& piece, const Hold& hold))
    {
        std::vector<PieceKey> stale;
        for (const auto& [piece, hold] : _holds)
        {
            if (!keep(piece, hold))
            {
                stale.push_back(piece);
            }
        }
        for (const auto& piece : stale)
        {
            eraseHold(piece);
        }
    }

    size_t pathHoldCount()
    {
        return _pathHolds;
    }

    size_t holdCount()
    {
        return _holds.size();
    }

    bool reservePath(uint16_t owner, const std::vector<ReservedStep>& steps)
    {
        // The old reservation remains intact throughout validation. There is
        // no simulation callback or failure path after the complete preflight.
        for (const auto& step : steps)
        {
            if (((step.connection >> 3) & 0x3F) >= kTrackIdCount)
            {
                return false;
            }
            if (!canHold(canonicalPiece(step.pos, step.connection), owner))
            {
                return false;
            }
        }
        // All replacement pieces passed preflight. Occupied holds are retained
        // by dropPendingInternal; only unused pending claims are replaced.
        dropPendingInternal(owner);
        auto& state = _trains[owner];
        const bool committed = !state.committed.empty();
        // A complete safe replacement is ready. Only now release the old
        // unused suffix; occupied/ring protection survives the replacement.
        std::vector<PieceKey> replaced;
        for (const auto& piece : state.committed)
        {
            const auto held = _holds.find(piece);
            if (held != _holds.end() && held->second.owner == owner && held->second.kind == HoldKind::path)
            {
                replaced.push_back(piece);
            }
        }
        for (const auto& piece : replaced)
        {
            eraseHold(piece);
            state.committed.erase(piece);
        }
        for (const auto& step : steps)
        {
            const auto piece = canonicalPiece(step.pos, step.connection);
            const auto facing = facingSide(step.pos, step.connection);
            const auto found = _holds.find(piece);
            if (found == _holds.end() || found->second.owner != owner || found->second.kind != HoldKind::occupied)
            {
                setHold(piece, Hold{ owner, HoldKind::path, true, facing.side });
            }
            else
            {
                found->second.side = facing.side;
                found->second.isProtected = true;
            }
            state.pending.push_back(step);
            if (committed)
            {
                state.committed.insert(piece);
            }
        }
        state.pathMode = true;
        refreshViewedReservation();
        return true;
    }

    std::vector<ReservedStep> dropPendingPath(uint16_t owner)
    {
        return dropPendingInternal(owner);
    }

    const std::deque<ReservedStep>* pendingPath(uint16_t owner)
    {
        const auto found = _trains.find(owner);
        if (found == _trains.end())
        {
            return nullptr;
        }
        return &found->second.pending;
    }

    void commitPendingSection(uint16_t owner)
    {
        const auto found = _trains.find(owner);
        if (found == _trains.end())
        {
            return;
        }
        for (const auto& step : found->second.pending)
        {
            found->second.committed.insert(canonicalPiece(step.pos, step.connection));
        }
    }

    bool consumePendingFront(uint16_t owner, const World::Pos3& entrance, uint16_t connection)
    {
        const auto found = _trains.find(owner);
        if (found == _trains.end() || found->second.pending.empty())
        {
            return false;
        }
        const auto& front = found->second.pending.front();
        if (front.pos != entrance || (front.connection & kBasicTaDMask) != (connection & kBasicTaDMask))
        {
            return false;
        }
        commitPendingSection(owner);
        found->second.pending.pop_front();
        const auto piece = canonicalPiece(entrance, connection);
        const auto facing = facingSide(entrance, connection);
        const auto hold = _holds.find(piece);
        if (hold != _holds.end() && hold->second.owner == owner)
        {
            setHold(piece, Hold{ owner, HoldKind::occupied, true, facing.side });
        }
        refreshViewedReservation();
        return true;
    }

    bool isInPathMode(uint16_t owner)
    {
        const auto found = _trains.find(owner);
        return found != _trains.end() && found->second.pathMode;
    }

    void setPathMode(uint16_t owner, bool enabled)
    {
        if (!enabled)
        {
            const auto found = _trains.find(owner);
            if (found != _trains.end())
            {
                found->second.pathMode = false;
            }
            return;
        }
        _trains[owner].pathMode = true;
    }

    bool isStationDepartureWaiting(uint16_t owner)
    {
        const auto found = _trains.find(owner);
        return found != _trains.end() && found->second.departureWaiting;
    }

    void setStationDepartureWaiting(uint16_t owner, bool waiting)
    {
        if (waiting)
        {
            _trains[owner].departureWaiting = true;
        }
        else if (const auto found = _trains.find(owner); found != _trains.end())
        {
            found->second.departureWaiting = false;
        }
    }

    void forgetTrain(uint16_t owner)
    {
        clearTrainPriority(owner);
        dropPendingInternal(owner);
        releaseAllHolds(owner);
        _trains.erase(owner);
        clearWaitingSignals(owner);
        clearApproachingSignals(owner);
        refreshViewedReservation();
    }

    std::vector<uint16_t> trainsWithPendingPiece(const PieceKey& piece)
    {
        std::vector<uint16_t> owners;
        for (const auto& [owner, state] : _trains)
        {
            for (const auto& step : state.pending)
            {
                if (canonicalPiece(step.pos, step.connection) == piece)
                {
                    owners.push_back(owner);
                    break;
                }
            }
        }
        return owners;
    }

    bool sideHasReservation(const SideKey& side)
    {
        for (const auto& [owner, state] : _trains)
        {
            (void)owner;
            for (const auto& step : state.pending)
            {
                if (facingSide(step.pos, step.connection) == side)
                {
                    return true;
                }
            }
        }
        return false;
    }

    static void setSignalOwners(std::map<SideKey, std::set<uint16_t>>& signals, uint16_t owner, const std::vector<SideKey>& sides)
    {
        std::map<SideKey, bool> previous;
        for (const auto& [side, owners] : signals)
        {
            if (owners.contains(owner))
            {
                previous.emplace(side, isSignalBlockedForDisplay(side));
            }
        }
        for (const auto& side : sides)
        {
            previous.try_emplace(side, isSignalBlockedForDisplay(side));
        }
        for (auto it = signals.begin(); it != signals.end();)
        {
            it->second.erase(owner);
            if (it->second.empty())
            {
                it = signals.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (const auto& side : sides)
        {
            signals[side].insert(owner);
        }
        if (_viewDirty != nullptr)
        {
            for (const auto& [side, wasWaiting] : previous)
            {
                if (wasWaiting != isSignalBlockedForDisplay(side))
                {
                    _viewDirty(side.piece);
                }
            }
        }
    }

    void setWaitingSignals(uint16_t owner, const std::vector<SideKey>& sides)
    {
        setSignalOwners(_waitingSignals, owner, sides);
    }

    void setApproachingSignals(uint16_t owner, const std::vector<SideKey>& sides)
    {
        setSignalOwners(_approachingSignals, owner, sides);
    }

    void clearApproachingSignals(uint16_t owner)
    {
        setApproachingSignals(owner, {});
        _approachProbeTicks.erase(owner);
    }

    bool beginApproachProbe(uint16_t owner, uint32_t tick)
    {
        const auto found = _approachProbeTicks.find(owner);
        if (found != _approachProbeTicks.end() && static_cast<int32_t>(tick - found->second) < 0)
        {
            return false;
        }
        _approachProbeTicks[owner] = tick + 16;
        return true;
    }

    void clearWaitingSignals(uint16_t owner)
    {
        setWaitingSignals(owner, {});
    }

    bool isSignalWaiting(const SideKey& side)
    {
        return _waitingSignals.find(side) != _waitingSignals.end();
    }

    bool isSignalBlockedForDisplay(const SideKey& side)
    {
        return isSignalWaiting(side) || _approachingSignals.contains(side);
    }

    bool isTrainWaiting(uint16_t owner)
    {
        return std::any_of(_waitingSignals.begin(), _waitingSignals.end(), [owner](const auto& entry) { return entry.second.contains(owner); });
    }

    std::optional<ReservationArrow> reservationArrowAt(const PieceKey& piece)
    {
        const auto found = _allReserved.find(piece);
        if (found == _allReserved.end())
        {
            return std::nullopt;
        }
        return found->second;
    }

    namespace
    {
        void collectReserved(uint16_t onlyOwner, std::map<PieceKey, ReservationArrow>& out)
        {
            for (const auto& [owner, state] : _trains)
            {
                if (onlyOwner != kNoReservationView && owner != onlyOwner)
                {
                    continue;
                }
                for (const auto& step : state.pending)
                {
                    const auto side = facingSide(step.pos, step.connection);
                    out[side.piece] = ReservationArrow{ side.side, owner };
                }
            }
            for (const auto& [piece, hold] : _holds)
            {
                const bool stillReserved = hold.kind == HoldKind::path || (hold.kind == HoldKind::occupied && hold.isProtected);
                if (!stillReserved)
                {
                    continue;
                }
                if (onlyOwner != kNoReservationView && hold.owner != onlyOwner)
                {
                    continue;
                }
                if (out.find(piece) == out.end())
                {
                    out.emplace(piece, ReservationArrow{ hold.side, hold.owner });
                }
            }
        }

        void publishReservation(std::map<PieceKey, uint8_t>& viewed, std::map<PieceKey, ReservationArrow>& allReserved)
        {
            if (viewed == _viewedSides && allReserved == _allReserved)
            {
                return;
            }

            std::vector<PieceKey> dirty;
            for (const auto& [piece, side] : _viewedSides)
            {
                const auto found = viewed.find(piece);
                if (found == viewed.end() || found->second != side)
                {
                    dirty.push_back(piece);
                }
            }
            for (const auto& [piece, side] : viewed)
            {
                const auto found = _viewedSides.find(piece);
                if (found == _viewedSides.end() || found->second != side)
                {
                    dirty.push_back(piece);
                }
            }
            for (const auto& [piece, arrow] : _allReserved)
            {
                const auto found = allReserved.find(piece);
                if (found == allReserved.end() || found->second.side != arrow.side || found->second.owner != arrow.owner)
                {
                    dirty.push_back(piece);
                }
            }
            for (const auto& [piece, arrow] : allReserved)
            {
                const auto found = _allReserved.find(piece);
                if (found == _allReserved.end() || found->second.side != arrow.side || found->second.owner != arrow.owner)
                {
                    dirty.push_back(piece);
                }
            }
            _viewedSides = std::move(viewed);
            _allReserved = std::move(allReserved);
            if (_viewDirty != nullptr)
            {
                for (const auto& piece : dirty)
                {
                    _viewDirty(piece);
                }
            }
        }

        void refreshViewedReservation()
        {
            std::map<PieceKey, ReservationArrow> allReserved;
            collectReserved(kNoReservationView, allReserved);

            std::map<PieceKey, uint8_t> viewed;
            if (_viewedOwner != kNoReservationView)
            {
                std::map<PieceKey, ReservationArrow> owned;
                collectReserved(_viewedOwner, owned);
                for (const auto& [piece, arrow] : owned)
                {
                    viewed.emplace(piece, arrow.side);
                }
            }
            publishReservation(viewed, allReserved);
        }
    }

    void setReservationView(uint16_t owner)
    {
        _viewedOwner = owner;
        refreshViewedReservation();
    }

    uint16_t reservationView()
    {
        return _viewedOwner;
    }

    std::optional<uint8_t> viewedTravelSide(const PieceKey& piece)
    {
        const auto found = _viewedSides.find(piece);
        if (found == _viewedSides.end())
        {
            return std::nullopt;
        }
        return found->second;
    }

    void refreshReservationView()
    {
        refreshViewedReservation();
    }

    void setReservationViewDirty(ReservationViewDirty callback)
    {
        _viewDirty = callback;
    }

    void clearAll()
    {
        clearReservationPriorities();
        _viewedOwner = kNoReservationView;
        if (_viewDirty != nullptr)
        {
            for (const auto& [side, owners] : _waitingSignals)
            {
                _viewDirty(side.piece);
            }
            for (const auto& [side, owners] : _approachingSignals)
            {
                if (!_waitingSignals.contains(side))
                {
                    _viewDirty(side.piece);
                }
            }
        }
        _waitingSignals.clear();
        _approachingSignals.clear();
        _approachProbeTicks.clear();
        _lightRepairPending = false;
        _kinds.clear();
        _chainAspects.clear();
        _holds.clear();
        _tiles.clear();
        _trains.clear();
        _pathHolds = 0;
        refreshViewedReservation();
    }

    std::filesystem::path companionPath(const std::filesystem::path& savePath)
    {
        return std::filesystem::path(savePath.string() + ".osignals");
    }

    bool companionNeedsLightRepair()
    {
        return _lightRepairPending;
    }

    void clearCompanionLightRepair()
    {
        _lightRepairPending = false;
    }

    namespace
    {
        std::optional<uint64_t> saveFingerprint(const std::filesystem::path& path)
        {
            std::error_code error;
            if (!std::filesystem::exists(path, error) && !error)
            {
                return uint64_t{ 0 }; // Model-only callers have no SV5.
            }
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                return std::nullopt;
            }
            uint64_t hash = 14695981039346656037ULL;
            std::array<char, 65536> buffer;
            while (input)
            {
                input.read(buffer.data(), buffer.size());
                for (std::streamsize i = 0; i < input.gcount(); ++i)
                {
                    hash ^= static_cast<uint8_t>(buffer[i]);
                    hash *= 1099511628211ULL;
                }
            }
            return input.bad() ? std::nullopt : std::optional{ hash };
        }

        struct SavedTrain
        {
            uint16_t owner;
            uint64_t identity;
            bool pathMode;
            std::vector<ReservedStep> pending;
            std::vector<std::pair<PieceKey, uint8_t>> committed;
        };
    }

    bool saveCompanion(const std::filesystem::path& savePath)
    {
        const auto path = companionPath(savePath);
        const auto priorities = capturePriorities();
        std::error_code fileError;
        const bool nativeSave = std::filesystem::exists(savePath, fileError);
        if (fileError)
        {
            return false;
        }
        std::map<uint16_t, uint64_t> identities;
        for (const auto& [owner, state] : _trains)
        {
            if (!state.pathMode && state.pending.empty() && state.committed.empty())
            {
                continue;
            }
            const auto identity = trainPriorityIdentity(owner);
            // Picked-up/deleted owners have no placed route to restore. Model
            // tests without a native save may use synthetic reservation owners.
            if (nativeSave && !identity)
            {
                continue;
            }
            identities.emplace(owner, identity.value_or(0));
        }
        const auto trainCount = identities.size();
        if (_kinds.empty() && trainCount == 0 && priorities.empty())
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            return !error;
        }
        const auto fingerprint = saveFingerprint(savePath);
        if (!fingerprint)
        {
            return false;
        }
        auto temporary = path;
        temporary += ".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }
        stream << "OSIGNALS 4\nSAVE " << priorityTick() << ' ' << *fingerprint << "\nKINDS " << _kinds.size() << '\n';
        for (const auto& [side, kind] : _kinds)
        {
            stream << side.piece.x << ' ' << side.piece.y << ' ' << side.piece.z << ' ' << int(side.piece.rotation) << ' ' << int(side.piece.trackId) << ' ' << int(side.side) << ' ' << int(kind) << '\n';
        }
        stream << "TRAINS " << trainCount << '\n';
        for (const auto& [owner, state] : _trains)
        {
            const auto identity = identities.find(owner);
            if (identity == identities.end())
            {
                continue;
            }
            stream << owner << ' ' << identity->second << ' ' << int(state.pathMode) << ' ' << state.pending.size() << ' ' << state.committed.size() << '\n';
            for (const auto& step : state.pending)
            {
                stream << step.pos.x << ' ' << step.pos.y << ' ' << step.pos.z << ' ' << step.connection << '\n';
            }
            for (const auto& piece : state.committed)
            {
                const auto held = _holds.find(piece);
                const auto side = held == _holds.end() ? 0 : held->second.side;
                stream << piece.x << ' ' << piece.y << ' ' << piece.z << ' ' << int(piece.rotation) << ' ' << int(piece.trackId) << ' ' << int(side) << '\n';
            }
        }
        stream << "PRIORITIES " << priorities.size() << '\n';
        for (const auto& entry : priorities)
        {
            stream << entry.owner << ' ' << entry.identity << ' ' << entry.waiting << ' ' << entry.waitedTicks << ' ' << entry.hasLottery << ' ' << entry.lottery << ' ' << entry.departureWaiting << '\n';
        }
        stream.flush();
        const bool success = bool(stream);
        stream.close();
        if (!success || stream.fail())
        {
            std::error_code error;
            std::filesystem::remove(temporary, error);
            return false;
        }
#ifdef _WIN32
        return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        return !error;
#endif
    }

    bool loadCompanion(const std::filesystem::path& savePath)
    {
        clearReservationPriorities();
        std::vector<PieceKey> stale;
        for (const auto& [piece, hold] : _holds)
        {
            if (hold.kind == HoldKind::path)
            {
                stale.push_back(piece);
            }
        }
        for (const auto& piece : stale)
        {
            eraseHold(piece);
        }
        _trains.clear();
        _kinds.clear();
        _chainAspects.clear();
        _lightRepairPending = false;
        _waitingSignals.clear();
        _approachingSignals.clear();
        _approachProbeTicks.clear();
        const auto path = companionPath(savePath);
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
        {
            return !error || !std::filesystem::exists(path);
        }
        if (std::filesystem::file_size(path, error) > 32 * 1024 * 1024 || error)
        {
            return false;
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            return false;
        }
        std::string tag;
        int version = 0;
        size_t count = 0, total = 0;
        if (!(stream >> tag >> version) || tag != "OSIGNALS" || version < 1 || version > 4)
        {
            return false;
        }
        if (version == 4)
        {
            uint64_t tick = 0, hash = 0;
            if (!(stream >> tag >> tick >> hash) || tag != "SAVE" || tick != priorityTick())
            {
                return false;
            }
            const auto fingerprint = saveFingerprint(savePath);
            if (!fingerprint || hash != *fingerprint)
            {
                return false;
            }
        }
        const bool nativeSave = std::filesystem::exists(savePath, error);
        if (error)
        {
            return false;
        }
        std::map<SideKey, SignalKind> kinds;
        std::vector<SavedTrain> trains;
        std::vector<SavedPriority> priorities;
        std::set<uint16_t> owners;
        const auto coord = [](int value) { return value >= INT16_MIN && value <= INT16_MAX; };
        if (!(stream >> tag >> count) || tag != "KINDS" || count > kMaxCompanionEntries)
        {
            return false;
        }
        for (size_t i = 0; i < count; ++i)
        {
            int x, y, z, rotation, trackId, side, kind;
            if (!(stream >> x >> y >> z >> rotation >> trackId >> side >> kind) || !coord(x) || !coord(y) || !coord(z))
            {
                return false;
            }
            if (side < 0 || side > 1 || rotation < 0 || rotation > 3 || trackId < 0 || trackId >= kTrackIdCount)
            {
                if (version == 4)
                {
                    return false;
                }
                continue;
            }
            SignalKind flags = SignalKind::none;
            if (version == 1)
            {
                if (kind == 1 || kind == 2)
                {
                    flags = SignalKind::path;
                }
                else if (kind == 3)
                {
                    flags = SignalKind::chain;
                }
            }
            else if ((kind & ~3) == 0)
            {
                flags = static_cast<SignalKind>(kind);
            }
            else if (version == 4)
            {
                return false;
            }
            if (flags != SignalKind::none)
            {
                kinds[{ PieceKey{ int16_t(x), int16_t(y), int16_t(z), uint8_t(rotation), uint8_t(trackId) }, uint8_t(side) }] = flags;
            }
        }
        if (!(stream >> tag >> count) || tag != "TRAINS" || count > kMaxCompanionEntries)
        {
            return false;
        }
        for (size_t i = 0; i < count; ++i)
        {
            uint64_t owner = 0, identity = 0;
            int mode = 0;
            size_t steps = 0, committed = 0;
            if (!(stream >> owner) || owner > UINT16_MAX)
            {
                return false;
            }
            if (version == 4 && !(stream >> identity))
            {
                return false;
            }
            if (!(stream >> mode >> steps) || mode < 0 || mode > 1 || steps > kMaxCompanionSteps)
            {
                return false;
            }
            if (version == 4 && (!(stream >> committed) || committed > kMaxCompanionSteps))
            {
                return false;
            }
            if (!owners.insert(uint16_t(owner)).second || (total += steps + committed) > kMaxCompanionEntries)
            {
                return false;
            }
            if (version == 4 && nativeSave && identity == 0)
            {
                return false;
            }
            if (identity && trainPriorityIdentity(uint16_t(owner)) != std::optional<uint64_t>{ identity })
            {
                return false;
            }
            SavedTrain train{ uint16_t(owner), identity, mode != 0, {}, {} };
            for (size_t j = 0; j < steps; ++j)
            {
                int x, y, z;
                uint64_t connection;
                if (!(stream >> x >> y >> z >> connection) || !coord(x) || !coord(y) || !coord(z) || connection > UINT16_MAX || ((connection >> 3) & 63) >= kTrackIdCount)
                {
                    return false;
                }
                train.pending.push_back({ { int16_t(x), int16_t(y), int16_t(z) }, uint16_t(connection) });
            }
            for (size_t j = 0; j < committed; ++j)
            {
                int x, y, z, rotation, id, side;
                if (!(stream >> x >> y >> z >> rotation >> id >> side) || !coord(x) || !coord(y) || !coord(z) || rotation < 0 || rotation > 3 || id < 0 || id >= kTrackIdCount || side < 0 || side > 1)
                {
                    return false;
                }
                train.committed.push_back({ { int16_t(x), int16_t(y), int16_t(z), uint8_t(rotation), uint8_t(id) }, uint8_t(side) });
            }
            trains.push_back(std::move(train));
        }
        if (version == 4)
        {
            if (!(stream >> tag >> count) || tag != "PRIORITIES" || count > kMaxCompanionEntries)
            {
                return false;
            }
            owners.clear();
            for (size_t i = 0; i < count; ++i)
            {
                uint64_t owner, identity, age, lottery;
                int waiting, hasLottery, departing;
                if (!(stream >> owner >> identity >> waiting >> age >> hasLottery >> lottery >> departing) || owner > UINT16_MAX || age > UINT32_MAX || lottery > UINT32_MAX || waiting < 0 || waiting > 1 || hasLottery < 0 || hasLottery > 1 || departing < 0 || departing > 1 || !owners.insert(uint16_t(owner)).second)
                {
                    return false;
                }
                if (trainPriorityIdentity(uint16_t(owner)) != std::optional<uint64_t>{ identity })
                {
                    return false;
                }
                priorities.push_back({ uint16_t(owner), identity, waiting != 0, uint32_t(age), hasLottery != 0, uint32_t(lottery), departing != 0 });
            }
            stream >> std::ws;
            if (!stream.eof())
            {
                return false;
            }
        }
        // Parse and identity checks are complete. Roll back every attached path
        // on conflict, retaining native reconstructed occupied protection.
        const auto oldHolds = _holds;
        const auto oldTiles = _tiles;
        const auto oldPathCount = _pathHolds;
        const auto refuse = [&] { _holds=oldHolds;_tiles=oldTiles;_pathHolds=oldPathCount;_trains.clear();_kinds.clear();clearReservationPriorities();return false; };
        for (const auto& train : trains)
        {
            if (!reservePath(train.owner, train.pending))
            {
                return refuse();
            }
            auto& state = _trains[train.owner];
            state.pathMode = train.pathMode;
            // Older companions cannot distinguish an unused plan from a route
            // already entered. Keep their pending section non-yieldable until
            // normal progress or a fully validated replacement resolves it.
            if (version < 4 && train.pathMode)
            {
                for (const auto& step : train.pending)
                {
                    state.committed.insert(canonicalPiece(step.pos, step.connection));
                }
            }
            for (const auto& [piece, side] : train.committed)
            {
                if (!canHold(piece, train.owner))
                {
                    return refuse();
                }
                const auto held = _holds.find(piece);
                if (held == _holds.end() || held->second.kind != HoldKind::occupied)
                {
                    setHold(piece, { train.owner, HoldKind::path, true, side });
                }
                else
                {
                    held->second.isProtected = true;
                }
                state.committed.insert(piece);
            }
        }
        _kinds = std::move(kinds);
        if (!restorePriorities(priorities))
        {
            return refuse();
        }
        _lightRepairPending = version == 2;
        for (auto& [piece, hold] : _holds)
        {
            if (hold.kind == HoldKind::occupied && isInPathMode(hold.owner))
            {
                hold.isProtected = true;
            }
        }
        refreshViewedReservation();
        return true;
    }
}
