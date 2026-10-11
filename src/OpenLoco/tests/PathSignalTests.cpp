#include <OpenLoco/Signals/PathSearch.h>
#include <OpenLoco/Signals/PathSignals.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <string>

using namespace OpenLoco;
using namespace OpenLoco::Signals;

namespace
{
    class TestGraph final : public TrackGraph
    {
    public:
        StepEdge& add(int16_t from, int16_t to)
        {
            StepEdge edge;
            edge.step = TrackStep{ World::Pos3{ to, 0, 0 }, 0 };
            edge.connection = static_cast<uint16_t>(to);
            edge.cost = 32;
            auto& list = _edges[from];
            list.push_back(edge);
            return list.back();
        }

        StepEdge start(int16_t at) const
        {
            StepEdge edge;
            edge.step = TrackStep{ World::Pos3{ at, 0, 0 }, 0 };
            edge.connection = static_cast<uint16_t>(at);
            edge.cost = 32;
            return edge;
        }

        void successors(const TrackStep& from, std::vector<StepEdge>& out) override
        {
            const auto found = _edges.find(from.pos.x);
            if (found == _edges.end())
            {
                return;
            }
            out.insert(out.end(), found->second.begin(), found->second.end());
        }

    private:
        std::map<int16_t, std::vector<StepEdge>> _edges;
    };

    std::vector<int16_t> ids(const SearchResult& result)
    {
        std::vector<int16_t> out;
        for (const auto& edge : result.reservation)
        {
            out.push_back(edge.step.pos.x);
        }
        return out;
    }

    ReservedStep straight(int16_t x, int16_t y, uint8_t rotation)
    {
        return ReservedStep{ World::Pos3{ x, y, 0 }, static_cast<uint16_t>(rotation) };
    }
}

TEST(PathSignalTests, registryStoresOnlySpecialKinds)
{
    clearAll();
    const SideKey side{ PieceKey{ 320, 320, 0, 0, 0 }, 1 };
    setSignalKind(side, SignalKind::chain);
    EXPECT_EQ(getSignalKind(side), SignalKind::chain);
    EXPECT_EQ(specialSignalCount(), 1U);
    setSignalKind(side, SignalKind::none);
    EXPECT_EQ(getSignalKind(side), SignalKind::none);
    EXPECT_EQ(specialSignalCount(), 0U);
}

TEST(PathSignalTests, sidesAndPiecesRoundTrip)
{
    const PieceKey piece{ 640, 640, 32, 1, 4 };
    for (uint8_t side = 0; side < 2; ++side)
    {
        const auto entrance = entranceOf(SideKey{ piece, side });
        EXPECT_EQ(canonicalPiece(entrance.pos, entrance.connection), piece);
        EXPECT_EQ(facingSide(entrance.pos, entrance.connection).side, side);
        EXPECT_EQ(behindSide(entrance.pos, entrance.connection).side, side == 0 ? 1 : 0);
    }
}

TEST(PathSignalTests, crossingConflictsAndParallelDoesNot)
{
    const PieceKey alongX{ 320, 320, 0, 0, 0 };
    const PieceKey crossing{ 320, 320, 0, 1, 0 };
    const PieceKey parallel{ 320, 352, 0, 0, 0 };
    const PieceKey bridge{ 320, 320, 64, 1, 0 };
    EXPECT_TRUE(piecesConflict(alongX, crossing));
    EXPECT_FALSE(piecesConflict(alongX, parallel));
    EXPECT_FALSE(piecesConflict(alongX, bridge));

    const PieceKey diagonalA{ 0, 0, 0, 0, 1 };
    const PieceKey diagonalB{ 32, 32, 0, 0, 1 };
    EXPECT_FALSE(piecesConflict(diagonalA, diagonalB));
}

TEST(PathSignalTests, reservationIsAllOrNothing)
{
    clearAll();
    ASSERT_TRUE(reservePath(1, { straight(320, 320, 0) }));
    EXPECT_FALSE(reservePath(2, { straight(320, 352, 0), straight(320, 320, 1) }));
    EXPECT_FALSE(holdAt(canonicalPiece(World::Pos3{ 320, 352, 0 }, 0)).has_value());
    EXPECT_EQ(pathHoldCount(), 1U);

    ASSERT_TRUE(reservePath(2, { straight(320, 352, 0) }));
    EXPECT_EQ(pathHoldCount(), 2U);
    EXPECT_TRUE(isHeldByProtectedOther(canonicalPiece(World::Pos3{ 320, 320, 0 }, 1), 2));
    EXPECT_FALSE(isHeldByProtectedOther(canonicalPiece(World::Pos3{ 320, 320, 0 }, 0), 1));

    ASSERT_TRUE(consumePendingFront(1, World::Pos3{ 320, 320, 0 }, 0));
    EXPECT_EQ(holdAt(canonicalPiece(World::Pos3{ 320, 320, 0 }, 0))->kind, HoldKind::occupied);
    releaseHold(1, canonicalPiece(World::Pos3{ 320, 320, 0 }, 0));
    EXPECT_TRUE(canHold(canonicalPiece(World::Pos3{ 320, 320, 0 }, 1), 2));

    forgetTrain(2);
    EXPECT_EQ(holdCount(), 0U);
}

TEST(PathSignalTests, unprotectedOccupancyDoesNotStopOtherTrains)
{
    clearAll();
    const auto piece = canonicalPiece(World::Pos3{ 320, 320, 0 }, 0);
    holdOccupied(7, piece, false);
    EXPECT_FALSE(isHeldByProtectedOther(piece, 8));
    EXPECT_FALSE(canHold(piece, 8));
    holdOccupied(7, piece, true);
    EXPECT_TRUE(isHeldByProtectedOther(piece, 8));
}

TEST(PathSignalTests, chainAspectFollowsThePicture)
{
    EXPECT_EQ(evaluateChainAspect(false, 3, 0), ChainAspect::green);
    EXPECT_EQ(evaluateChainAspect(false, 3, 1), ChainAspect::blue);
    EXPECT_EQ(evaluateChainAspect(false, 3, 3), ChainAspect::red);
    EXPECT_EQ(evaluateChainAspect(true, 3, 0), ChainAspect::red);
    EXPECT_EQ(evaluateChainAspect(false, 0, 0), ChainAspect::green);
}

TEST(PathSignalTests, claimedSignalsShowTheirRoute)
{
    DisplayChoice claimed;
    claimed.claimed = true;
    EXPECT_EQ(chooseDisplayState(SignalKind::path, claimed), DisplayState::green);
    EXPECT_EQ(chooseDisplayState(SignalKind::path | SignalKind::chain, claimed), DisplayState::green);
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, claimed), DisplayState::green);

    DisplayChoice waitsAhead = claimed;
    waitsAhead.claimEndsAtWait = true;
    EXPECT_EQ(chooseDisplayState(SignalKind::path, waitsAhead), DisplayState::yellow);

    DisplayChoice clearBlock;
    EXPECT_EQ(chooseDisplayState(SignalKind::path, clearBlock), DisplayState::green);
    EXPECT_EQ(chooseDisplayState(SignalKind::path | SignalKind::chain, clearBlock), DisplayState::green);
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, clearBlock), DisplayState::green);

    DisplayChoice occupied = clearBlock;
    occupied.blockBusy = true;
    EXPECT_EQ(chooseDisplayState(SignalKind::path, occupied), DisplayState::red);
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, occupied), DisplayState::red);

    DisplayChoice aheadRed;
    aheadRed.aheadClear = false;
    EXPECT_EQ(chooseDisplayState(SignalKind::path, aheadRed), DisplayState::red);
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, aheadRed), DisplayState::red);

    DisplayChoice behindRed;
    behindRed.behindClear = false;
    EXPECT_EQ(chooseDisplayState(SignalKind::path | SignalKind::chain, behindRed), DisplayState::red);
}

TEST(PathSignalTests, chainPostFollowsItsExits)
{
    DisplayChoice noExits;
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, noExits), DisplayState::green);

    DisplayChoice busy;
    busy.blockBusy = true;
    busy.exitCount = 2;
    busy.openExitCount = 2;
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, busy), DisplayState::red);

    DisplayChoice allClosed;
    allClosed.exitCount = 2;
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, allClosed), DisplayState::red);

    DisplayChoice oneOpen;
    oneOpen.exitCount = 2;
    oneOpen.openExitCount = 1;
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, oneOpen), DisplayState::green);

    DisplayChoice waiting = oneOpen;
    waiting.trainWaitingHere = true;
    EXPECT_EQ(chooseDisplayState(SignalKind::chain, waiting), DisplayState::red);
    EXPECT_EQ(chooseDisplayState(SignalKind::path, waiting), DisplayState::red);
}

TEST(PathSignalTests, twoStateSignalsShowProceedAsGreen)
{
    EXPECT_EQ(postStateFor(DisplayState::yellow, 2), DisplayState::green);
    EXPECT_EQ(postStateFor(DisplayState::yellow, 3), DisplayState::yellow);
    EXPECT_EQ(postStateFor(DisplayState::red, 2), DisplayState::red);
    EXPECT_EQ(postStateFor(DisplayState::green, 4), DisplayState::green);
}

TEST(PathSignalTests, claimsMarkEachTrainsLastSignal)
{
    const SideKey first{ PieceKey{ 320, 320, 0, 0, 0 }, 0 };
    const SideKey second{ PieceKey{ 352, 320, 0, 0, 0 }, 0 };
    const SideKey other{ PieceKey{ 384, 320, 0, 0, 0 }, 1 };
    const auto claims = buildSignalClaims({ { 3, { first, second } }, { 4, { other } } });
    ASSERT_EQ(claims.size(), 3U);
    EXPECT_EQ(claims.at(first).owner, 3U);
    EXPECT_FALSE(claims.at(first).isLast);
    EXPECT_TRUE(claims.at(second).isLast);
    EXPECT_EQ(claims.at(other).owner, 4U);
    EXPECT_TRUE(claims.at(other).isLast);
}

TEST(PathSignalTests, pathAndChainFlagsCombine)
{
    clearAll();
    const SideKey side{ PieceKey{ 320, 320, 0, 0, 0 }, 0 };
    setSignalKind(side, SignalKind::path | SignalKind::chain);
    EXPECT_TRUE(isPathSignal(getSignalKind(side)));
    EXPECT_TRUE(isChainSignal(getSignalKind(side)));
    EXPECT_EQ(specialSignalCount(), 1U);

    setSignalKind(side, SignalKind::path);
    EXPECT_TRUE(isPathSignal(getSignalKind(side)));
    EXPECT_FALSE(isChainSignal(getSignalKind(side)));
    EXPECT_EQ(getChainAspect(side), ChainAspect::green);
}

TEST(PathSignalTests, companionMigratesVersion1Kinds)
{
    clearAll();
    const auto save = std::filesystem::temp_directory_path() / "openloco-path-signals-v1.SV5";
    {
        std::ofstream file(companionPath(save), std::ios::binary | std::ios::trunc);
        file << "OSIGNALS 1\n";
        file << "KINDS 3\n";
        file << "320 320 0 0 0 0 1\n";
        file << "352 320 0 0 0 1 2\n";
        file << "384 320 0 0 0 0 3\n";
        file << "TRAINS 0\n";
    }
    ASSERT_TRUE(loadCompanion(save));
    EXPECT_EQ(getSignalKind(SideKey{ PieceKey{ 320, 320, 0, 0, 0 }, 0 }), SignalKind::path);
    EXPECT_EQ(getSignalKind(SideKey{ PieceKey{ 352, 320, 0, 0, 0 }, 1 }), SignalKind::path);
    EXPECT_EQ(getSignalKind(SideKey{ PieceKey{ 384, 320, 0, 0, 0 }, 0 }), SignalKind::chain);
    EXPECT_FALSE(companionNeedsLightRepair());
    std::filesystem::remove(companionPath(save));
}

TEST(PathSignalTests, companionVersion2RequestsRouteLightRepair)
{
    clearAll();
    const auto save = std::filesystem::temp_directory_path() / "openloco-path-signals-v2.SV5";
    {
        std::ofstream file(companionPath(save), std::ios::binary | std::ios::trunc);
        file << "OSIGNALS 2\n";
        file << "KINDS 1\n";
        file << "320 320 0 0 0 0 3\n";
        file << "TRAINS 0\n";
    }
    ASSERT_TRUE(loadCompanion(save));
    EXPECT_EQ(getSignalKind(SideKey{ PieceKey{ 320, 320, 0, 0, 0 }, 0 }), SignalKind::path | SignalKind::chain);
    EXPECT_TRUE(companionNeedsLightRepair());
    clearCompanionLightRepair();
    EXPECT_FALSE(companionNeedsLightRepair());
    std::filesystem::remove(companionPath(save));
}

TEST(PathSignalTests, reservationArrowTracksEveryPendingPath)
{
    clearAll();
    const auto step = straight(320, 320, 0);
    ASSERT_TRUE(reservePath(4, { step }));
    const auto side = facingSide(step.pos, step.connection);
    EXPECT_TRUE(sideHasReservation(side));
    const auto arrow = reservationArrowAt(side.piece);
    ASSERT_TRUE(arrow.has_value());
    EXPECT_EQ(arrow->owner, 4U);
    EXPECT_EQ(arrow->side, side.side);

    ASSERT_TRUE(consumePendingFront(4, step.pos, step.connection));
    const auto held = reservationArrowAt(side.piece);
    ASSERT_TRUE(held.has_value());
    EXPECT_EQ(held->owner, 4U);
    EXPECT_EQ(held->side, side.side);

    releaseHold(4, side.piece);
    EXPECT_FALSE(reservationArrowAt(side.piece).has_value());

    ASSERT_TRUE(reservePath(4, { step }));
    const SideKey waiting{ side.piece, side.side };
    setWaitingSignals(4, { waiting });
    EXPECT_TRUE(isSignalWaiting(waiting));
    EXPECT_TRUE(isTrainWaiting(4));
    EXPECT_FALSE(isTrainWaiting(5));
    clearWaitingSignals(4);
    EXPECT_FALSE(isSignalWaiting(waiting));
    EXPECT_FALSE(isTrainWaiting(4));
}

TEST(PathSignalTests, viewerFollowsTheReservedPath)
{
    clearAll();
    const auto first = straight(320, 320, 0);
    const auto second = straight(352, 320, 0);
    ASSERT_TRUE(reservePath(9, { first, second }));
    setReservationView(9);

    const auto firstPiece = canonicalPiece(first.pos, first.connection);
    const auto secondPiece = canonicalPiece(second.pos, second.connection);
    ASSERT_TRUE(viewedTravelSide(firstPiece).has_value());
    EXPECT_EQ(*viewedTravelSide(firstPiece), facingSide(first.pos, first.connection).side);
    ASSERT_TRUE(viewedTravelSide(secondPiece).has_value());

    setReservationView(kNoReservationView);
    EXPECT_FALSE(viewedTravelSide(firstPiece).has_value());
    EXPECT_FALSE(viewedTravelSide(secondPiece).has_value());
    EXPECT_EQ(reservationView(), kNoReservationView);
}

TEST(PathSignalTests, pathAndChainLooksThroughUntilAPathSignal)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).signal = EntrySignal::chain;
    graph.add(3, 4).signal = EntrySignal::path;
    graph.add(4, 5);

    SearchRequest request;
    request.mode = SearchMode::chainSignal;
    request.starts.push_back(graph.start(1));
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 3, 4, 5 }));
}

TEST(PathSignalTests, companionRoundTripAndBadFiles)
{
    clearAll();
    const SideKey path{ PieceKey{ 320, 320, 0, 0, 0 }, 0 };
    const SideKey chain{ PieceKey{ 384, 320, 0, 0, 0 }, 1 };
    setSignalKind(path, SignalKind::path);
    setSignalKind(chain, SignalKind::chain);
    ASSERT_TRUE(reservePath(5, { straight(320, 320, 0), straight(352, 320, 0) }));

    const auto save = std::filesystem::temp_directory_path() / "openloco-path-signals.SV5";
    ASSERT_TRUE(saveCompanion(save));
    {
        std::ifstream written(companionPath(save), std::ios::binary);
        std::string magic;
        int version = 0;
        written >> magic >> version;
        EXPECT_EQ(magic, "OSIGNALS");
        EXPECT_EQ(version, 3);
    }
    clearAll();
    ASSERT_TRUE(loadCompanion(save));
    EXPECT_FALSE(companionNeedsLightRepair());
    EXPECT_EQ(getSignalKind(path), SignalKind::path);
    EXPECT_EQ(getSignalKind(chain), SignalKind::chain);
    ASSERT_NE(pendingPath(5), nullptr);
    EXPECT_EQ(pendingPath(5)->size(), 2U);
    EXPECT_TRUE(isInPathMode(5));
    EXPECT_EQ(holdAt(canonicalPiece(World::Pos3{ 352, 320, 0 }, 0))->owner, 5);

    clearAll();
    const auto missing = std::filesystem::temp_directory_path() / "openloco-path-signals-missing.SV5";
    std::filesystem::remove(companionPath(missing));
    EXPECT_TRUE(loadCompanion(missing));
    EXPECT_EQ(specialSignalCount(), 0U);

    {
        std::ofstream bad(companionPath(missing), std::ios::binary | std::ios::trunc);
        bad << "NOTSIGNALS 9\n";
    }
    EXPECT_FALSE(loadCompanion(missing));
    EXPECT_EQ(specialSignalCount(), 0U);

    std::filesystem::remove(companionPath(save));
    std::filesystem::remove(companionPath(missing));
}

TEST(PathSignalTests, searchTakesTheFreeBranchAtAJunction)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).blocked = true;
    graph.add(2, 4);
    graph.add(3, 5).isTarget = true;
    graph.add(4, 6).isTarget = true;

    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = true;
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_TRUE(result.reachedTarget);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 4, 6 }));
}

TEST(PathSignalTests, pathReservationStopsBeforeTheNextSignal)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).signal = EntrySignal::path;
    graph.add(3, 4).isTarget = true;

    SearchRequest request;
    request.starts.push_back(graph.start(1));
    request.hasTarget = true;
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2 }));
}

TEST(PathSignalTests, chainReservesThroughToTheExitPath)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).signal = EntrySignal::chain;
    graph.add(3, 4);
    graph.add(4, 5).signal = EntrySignal::path;
    graph.add(5, 6);
    graph.add(6, 7).signal = EntrySignal::path;

    SearchRequest request;
    request.mode = SearchMode::chainSignal;
    request.starts.push_back(graph.start(1));
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 3, 4, 5, 6 }));
}

TEST(PathSignalTests, chainWaitsWhenItsOnlyExitBlockIsOccupied)
{
    TestGraph graph;
    graph.add(1, 2);
    auto& exit = graph.add(2, 3);
    exit.signal = EntrySignal::block;
    exit.exitOccupied = true;

    SearchRequest request;
    request.mode = SearchMode::chainSignal;
    request.starts.push_back(graph.start(1));
    EXPECT_FALSE(findReservation(graph, request).found);
}

TEST(PathSignalTests, chainUsesTheOpenExitWhenSomeAreRed)
{
    TestGraph graph;
    graph.add(1, 2);
    auto& red = graph.add(2, 3);
    red.signal = EntrySignal::block;
    red.exitOccupied = true;
    graph.add(2, 4).signal = EntrySignal::block;
    graph.add(4, 5);
    graph.add(5, 6).signal = EntrySignal::path;

    SearchRequest request;
    request.mode = SearchMode::chainSignal;
    request.starts.push_back(graph.start(1));
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 4, 5 }));
}

TEST(PathSignalTests, oneWaySignalFromBehindIsADeadEnd)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3).signal = EntrySignal::blockedFromBehind;

    SearchRequest request;
    request.starts.push_back(graph.start(1));
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2 }));
}

TEST(PathSignalTests, noOrdersPicksTheNearestSafePoint)
{
    TestGraph graph;
    graph.add(1, 2);
    graph.add(2, 3);
    graph.add(3, 4).signal = EntrySignal::path;
    graph.add(2, 10);
    graph.add(10, 11);
    graph.add(11, 12);
    graph.add(12, 13).signal = EntrySignal::path;

    SearchRequest request;
    request.starts.push_back(graph.start(1));
    const auto result = findReservation(graph, request);
    ASSERT_TRUE(result.found);
    EXPECT_FALSE(result.reachedTarget);
    EXPECT_EQ(ids(result), (std::vector<int16_t>{ 1, 2, 3 }));
}

TEST(PathSignalTests, blockedStartCannotBeReserved)
{
    TestGraph graph;
    auto start = graph.start(1);
    start.blocked = true;
    SearchRequest request;
    request.starts.push_back(start);
    EXPECT_FALSE(findReservation(graph, request).found);
}
