#include "Signals/PathSignals.h"

#include "Map/QuarterTile.h"
#include "Map/Tile.h"
#include "Map/Track/Track.h"
#include "Map/Track/TrackData.h"
#include <OpenLoco/Math/Vector.hpp>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <system_error>

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
            bool pathMode = false;
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

        bool isFreeFor(const PieceKey& piece, uint16_t owner, bool protectedOnly)
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
                if (hold != _holds.end() && hold->second.owner == owner && hold->second.kind == HoldKind::path)
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

    DisplayState postStateFor(DisplayState state, uint8_t stateCount)
    {
        if (state == DisplayState::yellow && stateCount < 3)
        {
            return DisplayState::green;
        }
        return state;
    }

    uint8_t postFrameForDisplay(SignalKind kind, bool waiting, uint8_t vanillaFrame, uint8_t frameCount)
    {
        if (kind == SignalKind::none || !waiting || frameCount == 0)
        {
            return vanillaFrame;
        }
        return std::min<uint8_t>(3, frameCount - 1);
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
        refreshViewedReservation();
    }

    void releaseAllHolds(uint16_t owner)
    {
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
        auto& state = _trains[owner];
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
            }
            state.pending.push_back(step);
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

    void forgetTrain(uint16_t owner)
    {
        dropPendingInternal(owner);
        releaseAllHolds(owner);
        _trains.erase(owner);
        clearWaitingSignals(owner);
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

    void setWaitingSignals(uint16_t owner, const std::vector<SideKey>& sides)
    {
        std::map<SideKey, bool> previous;
        for (const auto& [side, owners] : _waitingSignals)
        {
            if (owners.contains(owner))
            {
                previous.emplace(side, true);
            }
        }
        for (const auto& side : sides)
        {
            previous.try_emplace(side, isSignalWaiting(side));
        }
        for (auto it = _waitingSignals.begin(); it != _waitingSignals.end();)
        {
            it->second.erase(owner);
            if (it->second.empty())
            {
                it = _waitingSignals.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (const auto& side : sides)
        {
            _waitingSignals[side].insert(owner);
        }
        if (_viewDirty != nullptr)
        {
            for (const auto& [side, wasWaiting] : previous)
            {
                if (wasWaiting != isSignalWaiting(side))
                {
                    _viewDirty(side.piece);
                }
            }
        }
    }

    void clearWaitingSignals(uint16_t owner)
    {
        setWaitingSignals(owner, {});
    }

    bool isSignalWaiting(const SideKey& side)
    {
        return _waitingSignals.find(side) != _waitingSignals.end();
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
        _viewedOwner = kNoReservationView;
        if (_viewDirty != nullptr)
        {
            for (const auto& [side, owners] : _waitingSignals)
            {
                _viewDirty(side.piece);
            }
        }
        _waitingSignals.clear();
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

    bool saveCompanion(const std::filesystem::path& savePath)
    {
        const auto path = companionPath(savePath);
        size_t trainCount = 0;
        for (const auto& [owner, state] : _trains)
        {
            if (state.pathMode || !state.pending.empty())
            {
                trainCount++;
            }
        }
        if (_kinds.empty() && trainCount == 0)
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            return true;
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }
        stream << "OSIGNALS 3\n";
        stream << "KINDS " << _kinds.size() << "\n";
        for (const auto& [side, kind] : _kinds)
        {
            stream << side.piece.x << ' ' << side.piece.y << ' ' << side.piece.z << ' ' << static_cast<int>(side.piece.rotation) << ' '
                   << static_cast<int>(side.piece.trackId) << ' ' << static_cast<int>(side.side) << ' ' << static_cast<int>(kind) << "\n";
        }
        stream << "TRAINS " << trainCount << "\n";
        for (const auto& [owner, state] : _trains)
        {
            if (!state.pathMode && state.pending.empty())
            {
                continue;
            }
            stream << owner << ' ' << (state.pathMode ? 1 : 0) << ' ' << state.pending.size() << "\n";
            for (const auto& step : state.pending)
            {
                stream << step.pos.x << ' ' << step.pos.y << ' ' << step.pos.z << ' ' << step.connection << "\n";
            }
        }
        return static_cast<bool>(stream);
    }

    bool loadCompanion(const std::filesystem::path& savePath)
    {
        std::vector<uint16_t> owners;
        for (const auto& [owner, state] : _trains)
        {
            owners.push_back(owner);
        }
        for (const auto owner : owners)
        {
            dropPendingInternal(owner);
        }
        _trains.clear();
        _kinds.clear();
        _chainAspects.clear();
        _lightRepairPending = false;

        const auto path = companionPath(savePath);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec))
        {
            return true;
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            return false;
        }

        std::string magic;
        int version = 0;
        stream >> magic >> version;
        if (magic != "OSIGNALS" || version < 1 || version > 3)
        {
            return false;
        }
        _lightRepairPending = version == 2;

        std::string tag;
        size_t count = 0;
        stream >> tag >> count;
        if (!stream || tag != "KINDS" || count > kMaxCompanionEntries)
        {
            _kinds.clear();
            return false;
        }
        for (size_t i = 0; i < count; ++i)
        {
            int x = 0;
            int y = 0;
            int z = 0;
            int rotation = 0;
            int trackId = 0;
            int side = 0;
            int kind = 0;
            stream >> x >> y >> z >> rotation >> trackId >> side >> kind;
            if (!stream)
            {
                _kinds.clear();
                return false;
            }
            if (side < 0 || side > 1 || rotation < 0 || rotation > 3 || trackId < 0 || trackId >= kTrackIdCount)
            {
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
            else if ((kind & ~0x3) == 0)
            {
                flags = static_cast<SignalKind>(kind) & (SignalKind::path | SignalKind::chain);
            }
            if (flags == SignalKind::none)
            {
                continue;
            }
            const PieceKey piece{ static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z), static_cast<uint8_t>(rotation), static_cast<uint8_t>(trackId) };
            _kinds[SideKey{ piece, static_cast<uint8_t>(side) }] = flags;
        }

        stream >> tag >> count;
        if (!stream || tag != "TRAINS" || count > kMaxCompanionEntries)
        {
            return false;
        }
        for (size_t i = 0; i < count; ++i)
        {
            unsigned int owner = 0;
            int pathMode = 0;
            size_t stepCount = 0;
            stream >> owner >> pathMode >> stepCount;
            if (!stream || stepCount > kMaxCompanionSteps)
            {
                return false;
            }
            std::vector<ReservedStep> steps;
            for (size_t j = 0; j < stepCount; ++j)
            {
                int x = 0;
                int y = 0;
                int z = 0;
                unsigned int connection = 0;
                stream >> x >> y >> z >> connection;
                if (!stream)
                {
                    return false;
                }
                steps.push_back(ReservedStep{ World::Pos3{ static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z) }, static_cast<uint16_t>(connection) });
            }
            const auto id = static_cast<uint16_t>(owner);
            if (!steps.empty())
            {
                reservePath(id, steps);
            }
            setPathMode(id, pathMode != 0);
        }

        for (auto& [piece, hold] : _holds)
        {
            if (hold.kind == HoldKind::occupied && isInPathMode(hold.owner))
            {
                hold.isProtected = true;
            }
        }
        return true;
    }
}
