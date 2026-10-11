#include <OpenLoco/Graphics/Colour.h>
#include <OpenLoco/Lines/Line.h>
#include <OpenLoco/Map/Track/SubpositionData.h>
#include <OpenLoco/Map/Track/Track.h>
#include <fstream>
#include <gtest/gtest.h>

using namespace OpenLoco;
using namespace OpenLoco::Lines;

namespace
{
    void reset()
    {
        clearLines();
    }

    LineOrder stop(uint16_t station)
    {
        LineOrder order;
        order.type = Vehicles::OrderType::StopAt;
        order.station = station;
        return order;
    }
}

TEST(LineTests, twoVehiclesShareAnAddedStop)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    ASSERT_TRUE(created.ok);
    ASSERT_TRUE(assignVehicle(created.id, 1, TransportMode::rail).ok);
    ASSERT_TRUE(assignVehicle(created.id, 2, TransportMode::rail).ok);
    ASSERT_TRUE(addOrder(created.id, stop(10)).ok);
    ASSERT_TRUE(addOrder(created.id, stop(20)).ok);

    const auto* first = ordersForVehicle(1);
    const auto* second = ordersForVehicle(2);
    const auto* other = ordersForVehicle(3);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(other, nullptr);
    ASSERT_EQ(first->size(), 2U);
    EXPECT_EQ(first->at(1).station, second->at(1).station);
    EXPECT_TRUE(canRun(*findLine(created.id)));
}

TEST(LineTests, emptyLineFollowsTheStopsMode)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    ASSERT_TRUE(created.ok);
    EXPECT_TRUE(setLineMode(created.id, TransportMode::road).ok);
    EXPECT_EQ(findLine(created.id)->mode, TransportMode::road);

    LineOrder waypoint;
    waypoint.type = Vehicles::OrderType::RouteWaypoint;
    ASSERT_TRUE(addOrder(created.id, waypoint).ok);
    EXPECT_FALSE(setLineMode(created.id, TransportMode::rail).ok);
    EXPECT_EQ(findLine(created.id)->mode, TransportMode::road);
}

TEST(LineTests, oneStopKindLocksTheLine)
{
    reset();
    const auto road = createLine("Buses", TransportMode::rail, 1);
    ASSERT_TRUE(road.ok);
    EXPECT_EQ(findLine(road.id)->kinds, kEveryLineKind);
    ASSERT_TRUE(acceptStopKinds(road.id, LineKind::road).ok);
    EXPECT_EQ(findLine(road.id)->kinds, LineKind::road);
    EXPECT_EQ(findLine(road.id)->mode, TransportMode::road);

    const auto tram = createLine("Trams", TransportMode::rail, 1);
    ASSERT_TRUE(tram.ok);
    ASSERT_TRUE(acceptStopKinds(tram.id, LineKind::tram).ok);
    EXPECT_EQ(findLine(tram.id)->kinds, LineKind::tram);
    EXPECT_EQ(findLine(tram.id)->mode, TransportMode::road);
}

TEST(LineTests, mixedStopNarrowsToTheNextStop)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 1);
    ASSERT_TRUE(created.ok);
    ASSERT_TRUE(acceptStopKinds(created.id, LineKind::rail | LineKind::road).ok);
    EXPECT_EQ(findLine(created.id)->kinds, LineKind::rail | LineKind::road);
    EXPECT_EQ(findLine(created.id)->mode, TransportMode::rail);
    ASSERT_TRUE(acceptStopKinds(created.id, LineKind::road).ok);
    EXPECT_EQ(findLine(created.id)->kinds, LineKind::road);
    EXPECT_EQ(findLine(created.id)->mode, TransportMode::road);
}

TEST(LineTests, stopOutsideTheSetIsRejected)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 1);
    ASSERT_TRUE(created.ok);
    ASSERT_TRUE(acceptStopKinds(created.id, LineKind::road).ok);
    EXPECT_FALSE(acceptStopKinds(created.id, LineKind::rail).ok);
    EXPECT_EQ(findLine(created.id)->kinds, LineKind::road);
    EXPECT_EQ(findLine(created.id)->mode, TransportMode::road);
}

TEST(LineTests, rejectsMismatchedMode)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    const auto assigned = assignVehicle(created.id, 5, TransportMode::road);
    EXPECT_FALSE(assigned.ok);
    EXPECT_EQ(lineForVehicle(5), nullptr);
}

TEST(LineTests, pointToPointReversesAndLoopDoesNot)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    addOrder(created.id, stop(1));
    addOrder(created.id, stop(2));
    addOrder(created.id, stop(3));
    const auto* line = findLine(created.id);
    const auto forward = nextStop(*line, { 0, false });
    EXPECT_EQ(forward.index, 1U);
    EXPECT_FALSE(forward.backward);
    const auto back = nextStop(*line, { 2, false });
    EXPECT_TRUE(back.backward);
    EXPECT_EQ(back.index, 1U);

    setLoop(created.id, true);
    const auto looped = nextStop(*findLine(created.id), { 2, false });
    EXPECT_FALSE(looped.backward);
    EXPECT_EQ(looped.index, 0U);
}

TEST(LineTests, clickingTheSameStopRoutesThroughIt)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    ASSERT_TRUE(insertOrder(created.id, 0, stop(4)).ok);
    ASSERT_TRUE(insertOrder(created.id, 1, stop(4)).ok);
    const auto* line = findLine(created.id);
    ASSERT_EQ(line->orders.size(), 1U);
    EXPECT_EQ(line->orders[0].type, Vehicles::OrderType::RouteThrough);
}

TEST(LineTests, moveOrderSwapsStops)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    ASSERT_TRUE(addOrder(created.id, stop(1)).ok);
    ASSERT_TRUE(addOrder(created.id, stop(2)).ok);
    ASSERT_TRUE(moveOrder(created.id, 1, -1).ok);
    EXPECT_EQ(findLine(created.id)->orders[0].station, 2);
}

TEST(LineTests, rejectsDuplicateAndSingleStop)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    ASSERT_TRUE(addOrder(created.id, stop(4)).ok);
    EXPECT_FALSE(addOrder(created.id, stop(4)).ok);
    EXPECT_FALSE(canRun(*findLine(created.id)));
}

TEST(LineTests, saveRoundTripAndVanillaSave)
{
    reset();
    const auto created = createLine("Harbour", TransportMode::water, 3);
    addOrder(created.id, stop(8));
    addOrder(created.id, stop(9));
    assignVehicle(created.id, 12, TransportMode::water);
    const auto path = std::filesystem::temp_directory_path() / "openloco-line-save.SV5";
    ASSERT_TRUE(saveCompanion(path));
    clearLines();
    ASSERT_TRUE(loadCompanion(path));
    const auto* line = findLine(created.id);
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->name, "Harbour");
    EXPECT_EQ(line->vehicles.size(), 1U);
    EXPECT_EQ(ordersForVehicle(12)->size(), 2U);

    clearLines();
    const auto missing = std::filesystem::temp_directory_path() / "openloco-line-missing.SV5";
    std::filesystem::remove(companionPath(missing));
    ASSERT_TRUE(loadCompanion(missing));
    EXPECT_TRUE(allLines().empty());
    std::filesystem::remove(companionPath(path));
}

TEST(TimetableTests, earlyWaitsLateDepartsAndAutoSpacing)
{
    LineOrder stop;
    stop.slots.push_back({ 0, 30 });
    const auto early = decideDeparture(stop, 10, 0xFFFF, 0, false, 0, 1);
    EXPECT_TRUE(early.wait);
    EXPECT_EQ(early.waitMinutes, 20);
    EXPECT_FALSE(early.late);

    const auto late = decideDeparture(stop, 50, 0xFFFF, 0, false, 0, 1);
    EXPECT_TRUE(late.late);
    EXPECT_FALSE(late.wait);

    const auto spaced = decideDeparture(stop, 10, 0, 0, true, 30, 2);
    EXPECT_TRUE(spaced.wait);
    EXPECT_GE(spaced.waitMinutes, 15);
}

TEST(LineTests, shortGapInFrontRequestsAHold)
{
    const SpreadPlace others[] = { { 2, 100 } };
    const auto spread = chooseEvenSpread(1000, 1, 0, others, 1, 400);
    EXPECT_TRUE(spread.hold);
    EXPECT_EQ(spread.capTicks, 320U);
}

TEST(LineTests, evenGapInFrontRequestsNoHold)
{
    const SpreadPlace others[] = { { 2, 500 } };
    const auto spread = chooseEvenSpread(1000, 1, 0, others, 1, 400);
    EXPECT_FALSE(spread.hold);
    EXPECT_EQ(spread.capTicks, 0U);
}

TEST(LineTests, vehicleOneStopBehindDoesNotCancelTheHold)
{
    const SpreadPlace bunched[] = { { 2, 100 }, { 3, 750 } };
    const auto held = chooseEvenSpread(1000, 1, 0, bunched, 2, 600);
    EXPECT_TRUE(held.hold);
    EXPECT_GT(held.capTicks, 0U);
    EXPECT_LE(held.capTicks, 600U);

    const SpreadPlace behind[] = { { 3, 750 } };
    const auto clear = chooseEvenSpread(1000, 1, 0, behind, 1, 600);
    EXPECT_FALSE(clear.hold);
}

TEST(LineTests, sameSpotLeadDepartsAndTheOtherHolds)
{
    const SpreadPlace follower[] = { { 2, 0 } };
    const auto lead = chooseEvenSpread(1000, 1, 0, follower, 1, 400);
    EXPECT_FALSE(lead.hold);

    const SpreadPlace leader[] = { { 1, 0 } };
    const auto other = chooseEvenSpread(1000, 2, 0, leader, 1, 400);
    EXPECT_TRUE(other.hold);
    EXPECT_EQ(other.capTicks, 400U);
}

TEST(LineTests, timetableMinuteUsesItsOwnCycle)
{
    EXPECT_EQ(timetableMinute(0), 0);
    EXPECT_EQ(timetableMinute(40), 1);
    EXPECT_EQ(timetableMinute(39), 0);
    EXPECT_EQ(timetableMinute(40 * 24 * 60), 0);
    EXPECT_EQ(timetableMinute(40 * 24 * 60 + 80), 2);
}

TEST(LineTests, renameAndExpressRoundTripAndOlderCompanion)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    ASSERT_TRUE(renameLine(created.id, "Harbour loop").ok);
    ASSERT_TRUE(setExpress(created.id, true).ok);
    EXPECT_TRUE(findLine(created.id)->express);
    EXPECT_EQ(findLine(created.id)->name, "Harbour loop");

    const auto path = std::filesystem::temp_directory_path() / "openloco-line-express.SV5";
    ASSERT_TRUE(saveCompanion(path));
    clearLines();
    ASSERT_TRUE(loadCompanion(path));
    const auto* saved = findLine(created.id);
    ASSERT_NE(saved, nullptr);
    EXPECT_TRUE(saved->express);
    EXPECT_EQ(saved->name, "Harbour loop");

    const auto older = std::filesystem::temp_directory_path() / "openloco-line-v2.SV5";
    {
        std::ofstream stream(companionPath(older), std::ios::binary | std::ios::trunc);
        stream << "OLINES 2\n1\nLINE 7 2 1 1 0 0 0 Old road\nORDERS 0\nVEHICLES 0\n";
    }
    clearLines();
    ASSERT_TRUE(loadCompanion(older));
    const auto* legacy = findLine(7);
    ASSERT_NE(legacy, nullptr);
    EXPECT_EQ(legacy->name, "Old road");
    EXPECT_FALSE(legacy->express);
    EXPECT_EQ(legacy->mode, TransportMode::road);
    std::filesystem::remove(companionPath(path));
    std::filesystem::remove(companionPath(older));
}

TEST(LineTests, newLineUsesTheGivenCompanyColours)
{
    reset();
    const auto created = createCompanyLine("Main", TransportMode::rail, static_cast<uint8_t>(Colour::yellow), static_cast<uint8_t>(Colour::darkYellow));
    ASSERT_TRUE(created.ok);
    const auto* line = findLine(created.id);
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->colour, static_cast<uint8_t>(Colour::yellow));
    EXPECT_EQ(line->secondaryColour, static_cast<uint8_t>(Colour::darkYellow));
    EXPECT_FALSE(line->paintVehicles);
}

TEST(LineTests, sameLineBackAndForthCountsAsOneLine)
{
    const uint16_t ids[] = { 4, 4, 4 };
    EXPECT_EQ(countDistinctLines(ids, 3), 1);
    EXPECT_EQ(laneOffsetPixels(0, countDistinctLines(ids, 3)), 0);
}

TEST(LineTests, twoLinesOnOneTileSitOnOppositeSides)
{
    EXPECT_EQ(laneOffsetPixels(0, 2), -laneOffsetPixels(1, 2));
    EXPECT_LT(laneOffsetPixels(0, 2), 0);
    EXPECT_GT(laneOffsetPixels(1, 2), 0);
    const auto left = shiftStroke(0, 0, 100, 0, laneOffsetPixels(0, 2));
    const auto right = shiftStroke(0, 0, 100, 0, laneOffsetPixels(1, 2));
    EXPECT_EQ(left.y0, -right.y0);
    EXPECT_NE(left.y0, right.y0);
    EXPECT_EQ(left.x0, 0);
    EXPECT_EQ(right.x1, 100);
}

TEST(LineTests, aGapAcrossTheMapIsNotDrawn)
{
    EXPECT_TRUE(strokeContinues(0, 0, 64, 32));
    EXPECT_FALSE(strokeContinues(0, 0, 0, 0));
    EXPECT_FALSE(strokeContinues(0, 0, 4000, 0));
}

TEST(LineTests, aStraightPieceStaysStraight)
{
    CurveSample samples[8]{};
    const auto count = trackPieceCurve(0, 0, 16, 0, false, samples, std::size(samples));
    ASSERT_GE(count, 2U);
    for (size_t i = 0; i < count; ++i)
    {
        EXPECT_EQ(samples[i].y, samples[0].y);
    }
}

TEST(LineTests, aSmallCurveBendsOffTheStraightLine)
{
    auto bends = [](uint16_t tad, bool road) {
        CurveSample samples[16]{};
        const auto count = trackPieceCurve(0, 0, 16, tad, road, samples, std::size(samples));
        if (count < 3)
        {
            return false;
        }
        const auto dx = static_cast<int64_t>(samples[count - 1].x) - samples[0].x;
        const auto dy = static_cast<int64_t>(samples[count - 1].y) - samples[0].y;
        for (size_t i = 1; i + 1 < count; ++i)
        {
            const auto sx = static_cast<int64_t>(samples[i].x) - samples[0].x;
            const auto sy = static_cast<int64_t>(samples[i].y) - samples[0].y;
            if (dx * sy - dy * sx != 0)
            {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(bends(static_cast<uint16_t>(2U << 3), false));
    EXPECT_TRUE(bends(static_cast<uint16_t>(1U << 3), true));
}

TEST(LineTests, pieceSamplesMatchTheVehicleMoveTable)
{
    const auto tad = static_cast<uint16_t>(2U << 3);
    const auto moves = World::TrackData::getTrackSubPositon(tad);
    ASSERT_FALSE(moves.empty());
    CurveSample samples[64]{};
    const auto count = trackPieceCurve(100, 200, 16, tad, false, samples, std::size(samples));
    ASSERT_GE(count, 2U);
    EXPECT_EQ(samples[0].x, 100 + moves.front().loc.x);
    EXPECT_EQ(samples[0].y, 200 + moves.front().loc.y);
    EXPECT_EQ(samples[0].z, 16 + moves.front().loc.z);
    EXPECT_EQ(samples[count - 1].x, 100 + moves.back().loc.x);
    EXPECT_EQ(samples[count - 1].y, 200 + moves.back().loc.y);
    EXPECT_EQ(samples[count - 1].z, 16 + moves.back().loc.z);
}

TEST(LineTests, onePieceStaysWithinTheDrawnSpan)
{
    const World::Pos3 origin{ 0, 0, 16 };
    for (uint8_t piece = 0; piece < 44; ++piece)
    {
        for (uint8_t direction = 0; direction < 8; ++direction)
        {
            const auto tad = static_cast<uint16_t>((piece << 3) | direction);
            const auto exit = World::Track::getTrackConnectionEnd(origin, tad);
            if (exit.nextPos.x != origin.x || exit.nextPos.y != origin.y)
            {
                EXPECT_TRUE(strokeContinues(origin.x, origin.y, exit.nextPos.x, exit.nextPos.y)) << static_cast<int>(piece) << " dir " << static_cast<int>(direction);
            }
        }
    }
    for (uint8_t piece = 0; piece < 10; ++piece)
    {
        for (uint8_t direction = 0; direction < 8; ++direction)
        {
            const auto tad = static_cast<uint16_t>((piece << 3) | direction);
            const auto exit = World::Track::getRoadConnectionEnd(origin, tad);
            if (exit.nextPos.x != origin.x || exit.nextPos.y != origin.y)
            {
                EXPECT_TRUE(strokeContinues(origin.x, origin.y, exit.nextPos.x, exit.nextPos.y)) << static_cast<int>(piece) << " dir " << static_cast<int>(direction);
            }
        }
    }
}

TEST(LineTests, theSamePieceInTheSameDirectionIsOnePass)
{
    EXPECT_TRUE(samePieceSameWay(3, 10, 20, 16, 0, 3, 10, 20, 16, 0));
    EXPECT_TRUE(samePieceSameWay(3, 10, 20, 16, 0, 3, 10, 20, 16, static_cast<uint16_t>(1U << 2)));
    EXPECT_FALSE(samePieceSameWay(3, 10, 20, 16, 0, 8, 10, 20, 16, 0));
    EXPECT_FALSE(samePieceSameWay(3, 10, 20, 16, 0, 3, 40, 20, 16, 0));
}

TEST(LineTests, fourStopsReturnToTheFirstAfterTheLastVisit)
{
    RouteLeg legs[8]{};
    ASSERT_EQ(loopLegs(4, legs, 8), 4U);
    EXPECT_EQ(legs[0].fromStop, 0U);
    EXPECT_EQ(legs[0].toStop, 1U);
    EXPECT_EQ(legs[1].fromStop, 1U);
    EXPECT_EQ(legs[1].toStop, 2U);
    EXPECT_EQ(legs[2].fromStop, 2U);
    EXPECT_EQ(legs[2].toStop, 3U);
    EXPECT_EQ(legs[3].fromStop, 3U);
    EXPECT_EQ(legs[3].toStop, 0U);
}

TEST(LineTests, routeLegsReturnToTheFirstStop)
{
    RouteLeg legs[4]{};
    const auto count = loopLegs(3, legs, 4);
    ASSERT_EQ(count, 3U);
    EXPECT_EQ(legs[0].fromStop, 0U);
    EXPECT_EQ(legs[0].toStop, 1U);
    EXPECT_EQ(legs[1].fromStop, 1U);
    EXPECT_EQ(legs[1].toStop, 2U);
    EXPECT_EQ(legs[2].fromStop, 2U);
    EXPECT_EQ(legs[2].toStop, 0U);
    EXPECT_EQ(loopLegs(1, legs, 4), 0U);
}

TEST(LineTests, sharedPieceKeepsTheSelectedLineColour)
{
    const OverlayClaim claims[] = { { 1, 4, false }, { 2, 9, true } };
    EXPECT_EQ(chosenOverlayColour(claims, 2, 2), 9);
    EXPECT_EQ(chosenOverlayColour(claims, 2, 1), 4);
}

TEST(LineTests, version3LineKeepsColourAndDoesNotPaintVehicles)
{
    reset();
    const auto path = std::filesystem::temp_directory_path() / "openloco-line-v3.SV5";
    {
        std::ofstream stream(companionPath(path), std::ios::binary | std::ios::trunc);
        stream << "OLINES 3\n1\nLINE 4 6 0 0 0 0 0 0 Harbour\nORDERS 0\nVEHICLES 0\n";
    }
    ASSERT_TRUE(loadCompanion(path));
    const auto* line = findLine(4);
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->name, "Harbour");
    EXPECT_EQ(line->colour, 6);
    EXPECT_EQ(line->secondaryColour, 6);
    EXPECT_FALSE(line->paintVehicles);
    EXPECT_EQ(line->schemeEnabled, 0);
    EXPECT_EQ(line->schemePrimary[0], 6);
    std::filesystem::remove(companionPath(path));
}

TEST(LineTests, specialSchemesAndOwnColoursRoundTrip)
{
    reset();
    const auto created = createCompanyLine("Main", TransportMode::rail, static_cast<uint8_t>(Colour::yellow), static_cast<uint8_t>(Colour::darkYellow));
    ASSERT_TRUE(created.ok);
    ASSERT_TRUE(setLineSchemeEnabled(created.id, 0, true).ok);
    ASSERT_TRUE(setLineSchemeColours(created.id, 0, static_cast<uint8_t>(Colour::red), static_cast<uint8_t>(Colour::darkRed)).ok);
    ASSERT_TRUE(setVehicleOwnColours(created.id, 12, true).ok);
    EXPECT_TRUE(lineSchemeEnabled(*findLine(created.id), 0));
    EXPECT_FALSE(lineSchemeEnabled(*findLine(created.id), 1));
    EXPECT_TRUE(vehicleUsesOwnColours(created.id, 12));
    EXPECT_FALSE(vehicleUsesOwnColours(created.id, 13));

    const auto path = std::filesystem::temp_directory_path() / "openloco-line-schemes.SV5";
    ASSERT_TRUE(saveCompanion(path));
    clearLines();
    ASSERT_TRUE(loadCompanion(path));
    const auto* line = findLine(created.id);
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE(lineSchemeEnabled(*line, 0));
    EXPECT_EQ(line->schemePrimary[0], static_cast<uint8_t>(Colour::red));
    EXPECT_EQ(line->schemeSecondary[0], static_cast<uint8_t>(Colour::darkRed));
    EXPECT_EQ(line->schemePrimary[3], static_cast<uint8_t>(Colour::yellow));
    EXPECT_TRUE(vehicleUsesOwnColours(created.id, 12));
    std::filesystem::remove(companionPath(path));
}

TEST(LineTests, deletingLineGivesEachVehicleAPrivateCopy)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    addOrder(created.id, stop(1));
    addOrder(created.id, stop(2));
    assignVehicle(created.id, 1, TransportMode::rail);
    assignVehicle(created.id, 2, TransportMode::rail);
    ASSERT_TRUE(deleteLine(created.id).ok);
    const auto* first = ordersForVehicle(1);
    const auto* second = ordersForVehicle(2);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_EQ(first->size(), second->size());
}

TEST(LineTests, progressSitsOnTheStopBeingServed)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    addOrder(created.id, stop(10));
    addOrder(created.id, stop(20));
    addOrder(created.id, stop(30));
    const auto* line = findLine(created.id);
    ASSERT_NE(line, nullptr);
    const std::vector<StopPosition> positions{ { 0, 0 }, { 1000, 0 }, { 2000, 0 } };
    const auto progress = progressAlongLine(*line, 2, 20, true, 1000, 0, positions);
    EXPECT_TRUE(progress.atStop);
    EXPECT_EQ(progress.fromOrdinal, 1U);
    EXPECT_EQ(progress.toOrdinal, 1U);
}

TEST(LineTests, progressIsHalfwayBetweenStops)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    addOrder(created.id, stop(10));
    addOrder(created.id, stop(20));
    addOrder(created.id, stop(30));
    const auto* line = findLine(created.id);
    ASSERT_NE(line, nullptr);
    const std::vector<StopPosition> positions{ { 0, 0 }, { 1000, 0 }, { 3000, 0 } };
    const auto progress = progressAlongLine(*line, 2, 0xFFFF, false, 500, 0, positions);
    EXPECT_FALSE(progress.atStop);
    EXPECT_EQ(progress.fromOrdinal, 0U);
    EXPECT_EQ(progress.toOrdinal, 1U);
    EXPECT_EQ(progress.perThousand, 500U);
}

TEST(LineTests, progressIsCloserToTheDestination)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    addOrder(created.id, stop(10));
    addOrder(created.id, stop(20));
    addOrder(created.id, stop(30));
    const auto* line = findLine(created.id);
    ASSERT_NE(line, nullptr);
    const std::vector<StopPosition> positions{ { 0, 0 }, { 1000, 0 }, { 3000, 0 } };
    const auto progress = progressAlongLine(*line, 2, 0xFFFF, false, 800, 0, positions);
    EXPECT_EQ(progress.fromOrdinal, 0U);
    EXPECT_EQ(progress.toOrdinal, 1U);
    EXPECT_EQ(progress.perThousand, 800U);
}

TEST(LineTests, progressOnALoopUsesTheLegBackToTheFirstStop)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    addOrder(created.id, stop(10));
    addOrder(created.id, stop(20));
    addOrder(created.id, stop(30));
    ASSERT_TRUE(setLoop(created.id, true).ok);
    const auto* line = findLine(created.id);
    ASSERT_NE(line, nullptr);
    const std::vector<StopPosition> positions{ { 0, 0 }, { 1000, 0 }, { 0, 1000 } };
    const auto progress = progressAlongLine(*line, 0, 0xFFFF, false, 0, 500, positions);
    EXPECT_TRUE(progress.wraps);
    EXPECT_EQ(progress.fromOrdinal, 2U);
    EXPECT_EQ(progress.toOrdinal, 0U);
    EXPECT_EQ(progress.perThousand, 500U);

    ASSERT_TRUE(setLoop(created.id, false).ok);
    const auto* returning = findLine(created.id);
    ASSERT_NE(returning, nullptr);
    const auto sameGap = progressAlongLine(*returning, 0, 0xFFFF, false, 0, 500, positions);
    EXPECT_TRUE(sameGap.wraps);
    EXPECT_EQ(sameGap.fromOrdinal, 2U);
    EXPECT_EQ(sameGap.toOrdinal, 0U);
}

TEST(LineTests, progressMatchesDistanceCoveredBetweenStops)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    addOrder(created.id, stop(10));
    addOrder(created.id, stop(20));
    addOrder(created.id, stop(30));
    const auto* line = findLine(created.id);
    ASSERT_NE(line, nullptr);
    const std::vector<StopPosition> positions{ { 0, 0 }, { 1000, 0 }, { 3000, 0 } };
    const auto progress = progressAlongLine(*line, 2, 0xFFFF, false, 200, 0, positions);
    EXPECT_EQ(progress.fromOrdinal, 0U);
    EXPECT_EQ(progress.toOrdinal, 1U);
    EXPECT_EQ(progress.perThousand, 200U);
}

TEST(LineTests, progressWithoutAMeasurableChordUsesTheMiddle)
{
    reset();
    const auto created = createLine("Main", TransportMode::rail, 2);
    addOrder(created.id, stop(10));
    addOrder(created.id, stop(20));
    const auto* line = findLine(created.id);
    ASSERT_NE(line, nullptr);
    const std::vector<StopPosition> samePlace{ { 0, 0 }, { 0, 0 } };
    const auto collapsed = progressAlongLine(*line, 2, 0xFFFF, false, 50, 50, samePlace);
    EXPECT_FALSE(collapsed.atStop);
    EXPECT_EQ(collapsed.perThousand, 500U);

    const auto unmeasured = progressAlongLine(*line, 2, 0xFFFF, false, 50, 50, {});
    EXPECT_EQ(unmeasured.fromOrdinal, 0U);
    EXPECT_EQ(unmeasured.toOrdinal, 1U);
    EXPECT_EQ(unmeasured.perThousand, 500U);
}

TEST(LineTests, loopDiagramKeepsAnEqualGapAfterTheLastStop)
{
    constexpr int32_t kWidth = 100;
    constexpr size_t kStops = 5;
    for (size_t ordinal = 0; ordinal < kStops; ++ordinal)
    {
        EXPECT_EQ(diagramStopX(0, kWidth, kStops, true, ordinal), static_cast<int32_t>(ordinal * 20));
    }
    EXPECT_EQ(diagramGapCount(kStops, true), kStops);

    LineProgress halfway;
    halfway.fromOrdinal = 4;
    halfway.toOrdinal = 0;
    halfway.perThousand = 500;
    halfway.wraps = true;
    EXPECT_EQ(diagramMarkerX(0, kWidth, kStops, true, halfway), 90);

    LineProgress arrived = halfway;
    arrived.perThousand = 1000;
    EXPECT_EQ(diagramMarkerX(0, kWidth, kStops, true, arrived), 0);

    EXPECT_EQ(diagramStopX(0, kWidth, kStops, false, 4), kWidth);
}
