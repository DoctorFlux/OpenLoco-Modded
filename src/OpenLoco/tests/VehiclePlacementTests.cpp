#include <OpenLoco/Entities/EntityManager.h>
#include <OpenLoco/GameState.h>
#include <OpenLoco/Map/Track/SubpositionData.h>
#include <OpenLoco/Vehicles/VehicleHead.h>
#include <OpenLoco/Vehicles/VehicleManager.h>
#include <gtest/gtest.h>

using namespace OpenLoco;

TEST(VehiclePlacementTests, asymmetricCurveEndpointHasValidReversePosition)
{
    constexpr uint16_t direction = (8 << 3) | 4;
    const auto forward = World::TrackData::getTrackSubPositon(direction);
    const auto reverse = World::TrackData::getTrackSubPositon(direction ^ 4);
    ASSERT_EQ(forward.size(), 80U);
    ASSERT_EQ(reverse.size(), 79U);
    const auto position = World::TrackData::getReversedTrackSubPosition(direction, 0);
    ASSERT_TRUE(position);
    EXPECT_LT(*position, reverse.size());
    EXPECT_EQ(*position, 78U);
}

TEST(VehiclePlacementTests, everyRailDirectionAndPositionReversesWithinItsDestination)
{
    for (uint16_t direction = 0; direction < 44 * 8; ++direction)
    {
        const auto forward = World::TrackData::getTrackSubPositon(direction);
        const auto reverse = World::TrackData::getTrackSubPositon(direction ^ 4);
        for (uint16_t position = 0; position < forward.size(); ++position)
        {
            const auto result = World::TrackData::getReversedTrackSubPosition(direction, position);
            ASSERT_TRUE(result) << direction << ":" << position;
            EXPECT_LT(*result, reverse.size()) << direction << ":" << position;
            if (forward.size() == reverse.size())
            {
                EXPECT_EQ(*result, forward.size() - 1 - position);
            }
        }
    }
}

TEST(VehiclePlacementTests, invalidRailPlacementPositionsAreRejected)
{
    for (uint16_t direction = 0; direction < 44 * 8; ++direction)
    {
        const auto movement = World::TrackData::getTrackSubPositon(direction);
        EXPECT_FALSE(World::TrackData::getReversedTrackSubPosition(direction, static_cast<uint16_t>(movement.size())));
        EXPECT_FALSE(World::TrackData::getReversedTrackSubPosition(direction, 0xFFFF));
    }
    EXPECT_FALSE(World::TrackData::getReversedTrackSubPosition(44 * 8, 0));
    EXPECT_FALSE(World::TrackData::getReversedTrackSubPosition(0xFFFF, 0));
}

TEST(VehiclePlacementTests, nativePlacementRejectsInvalidProgressBeforeChangingTheVehicle)
{
    EntityManager::reset();
    auto& head = *reinterpret_cast<Vehicles::VehicleHead*>(EntityManager::createEntityVehicle());
    head.mode = TransportMode::rail;
    head.tileX = -1;
    head.tileY = 123;
    head.subPosition = 17;
    head.routingHandle = Vehicles::RoutingHandle(7, 11);
    Vehicles::TrackAndDirection direction(0, 0);
    for (auto invalid : { uint16_t{ 32 }, uint16_t{ 0xFFFF } })
    {
        EXPECT_EQ(VehicleManager::placeDownVehicle(&head, 640, 640, 4, direction, invalid), VehicleManager::PlaceDownResult::Unk0);
        EXPECT_EQ(head.tileX, -1);
        EXPECT_EQ(head.tileY, 123);
        EXPECT_EQ(head.subPosition, 17);
        EXPECT_EQ(head.routingHandle, Vehicles::RoutingHandle(7, 11));
    }
    direction.track._data = 44 * 8;
    EXPECT_EQ(VehicleManager::placeDownVehicle(&head, 640, 640, 4, direction, 0), VehicleManager::PlaceDownResult::Unk0);
    EXPECT_EQ(head.tileX, -1);
    EXPECT_EQ(head.subPosition, 17);
    EntityManager::reset();
}

namespace
{
    struct TrafficHandednessScope
    {
        uint8_t original = getGameState().trafficHandedness;
        ~TrafficHandednessScope() { getGameState().trafficHandedness = original; }
    };
}

TEST(VehiclePlacementTests, innerRoadCurvePreviewCannotBeUsedAsALaneIndex)
{
    TrafficHandednessScope scope;
    getGameState().trafficHandedness = 0;
    constexpr uint16_t direction = 1 << 3;
    const auto preview = World::TrackData::getRoadPlacementSubPositon(direction);
    const auto movement = World::TrackData::getRoadSubPositon(direction);
    ASSERT_EQ(preview.size(), 22);
    ASSERT_EQ(movement.size(), 12);
    // The old cursor path submitted this out-of-range lane index to placement.
    constexpr uint16_t previewPosition = 21;
    ASSERT_GE(previewPosition, movement.size());
    const auto actual = World::TrackData::getRoadPlacementMovementPosition(direction, previewPosition);
    ASSERT_TRUE(actual);
    EXPECT_LT(*actual, movement.size());
    const auto reversed = World::TrackData::getReversedRoadSubPosition(direction, *actual);
    ASSERT_TRUE(reversed);
    EXPECT_LT(reversed->subPosition, World::TrackData::getRoadSubPositon(reversed->trackAndDirection).size());
    EXPECT_FALSE(World::TrackData::getReversedRoadSubPosition(direction, previewPosition));
}

TEST(VehiclePlacementTests, everyRoadPreviewConvertsToAValidLaneForBothTrafficSides)
{
    TrafficHandednessScope scope;
    for (uint8_t side : { uint8_t{ 0 }, uint8_t{ 1 } })
    {
        getGameState().trafficHandedness = side;
        for (uint16_t direction = 0; direction < 80; ++direction)
        {
            const auto preview = World::TrackData::getRoadPlacementSubPositon(direction);
            const auto movement = World::TrackData::getRoadSubPositon(direction);
            ASSERT_FALSE(preview.empty());
            ASSERT_FALSE(movement.empty());
            for (uint16_t position = 0; position < preview.size(); ++position)
            {
                const auto actual = World::TrackData::getRoadPlacementMovementPosition(direction, position);
                ASSERT_TRUE(actual) << int(side) << ':' << direction << ':' << position;
                EXPECT_LT(*actual, movement.size());
                const auto reversed = World::TrackData::getReversedRoadSubPosition(direction, *actual);
                ASSERT_TRUE(reversed);
                EXPECT_LT(reversed->subPosition, World::TrackData::getRoadSubPositon(reversed->trackAndDirection).size());
            }
        }
    }
}

TEST(VehiclePlacementTests, everyRoadLaneAndTurnaroundPositionReversesWithinItsDestination)
{
    TrafficHandednessScope scope;
    for (uint8_t side : { uint8_t{ 0 }, uint8_t{ 1 } })
    {
        getGameState().trafficHandedness = side;
        for (uint16_t lanes = 0; lanes < 4; ++lanes)
        {
            for (uint16_t base = 0; base < 80; ++base)
            {
                const auto direction = static_cast<uint16_t>(base | (lanes << 7));
                const auto movement = World::TrackData::getRoadSubPositon(direction);
                for (uint16_t position = 0; position < movement.size(); ++position)
                {
                    const auto reversed = World::TrackData::getReversedRoadSubPosition(direction, position);
                    ASSERT_TRUE(reversed) << int(side) << ':' << direction << ':' << position;
                    const auto reverse = World::TrackData::getRoadSubPositon(reversed->trackAndDirection);
                    EXPECT_LT(reversed->subPosition, reverse.size());
                    if (movement.size() == reverse.size())
                    {
                        EXPECT_EQ(reversed->subPosition, movement.size() - 1 - position);
                    }
                }
            }
        }
    }
}

TEST(VehiclePlacementTests, straightRoadCursorPositionsRemainUnchanged)
{
    TrafficHandednessScope scope;
    for (uint8_t side : { uint8_t{ 0 }, uint8_t{ 1 } })
    {
        getGameState().trafficHandedness = side;
        for (uint16_t direction = 0; direction < 8; ++direction)
        {
            const auto preview = World::TrackData::getRoadPlacementSubPositon(direction);
            for (uint16_t position = 0; position < preview.size(); ++position)
            {
                EXPECT_EQ(World::TrackData::getRoadPlacementMovementPosition(direction, position), position);
            }
        }
    }
}

TEST(VehiclePlacementTests, invalidRoadDirectionsAndPositionsAreRejected)
{
    TrafficHandednessScope scope;
    for (uint8_t side : { uint8_t{ 0 }, uint8_t{ 1 } })
    {
        getGameState().trafficHandedness = side;
        for (uint16_t direction = 0; direction < 80; ++direction)
        {
            const auto movement = World::TrackData::getRoadSubPositon(direction);
            const auto preview = World::TrackData::getRoadPlacementSubPositon(direction);
            EXPECT_FALSE(World::TrackData::getReversedRoadSubPosition(direction, static_cast<uint16_t>(movement.size())));
            EXPECT_FALSE(World::TrackData::getReversedRoadSubPosition(direction, UINT16_MAX));
            EXPECT_FALSE(World::TrackData::getRoadPlacementMovementPosition(direction, static_cast<uint16_t>(preview.size())));
            EXPECT_FALSE(World::TrackData::getRoadPlacementMovementPosition(direction, UINT16_MAX));
        }
        for (auto invalid : { uint16_t{ 80 }, uint16_t{ 127 }, uint16_t{ 512 }, uint16_t{ UINT16_MAX } })
        {
            EXPECT_TRUE(World::TrackData::getRoadSubPositon(invalid).empty());
            EXPECT_TRUE(World::TrackData::getRoadPlacementSubPositon(invalid).empty());
            EXPECT_FALSE(World::TrackData::getRoadPlacementMovementPosition(invalid, 0));
            EXPECT_FALSE(World::TrackData::getReversedRoadSubPosition(invalid, 0));
        }
    }
}

TEST(VehiclePlacementTests, nativeRoadPlacementRejectsPreviewOverrunsBeforeMutation)
{
    TrafficHandednessScope scope;
    EntityManager::reset();
    auto& head = *reinterpret_cast<Vehicles::VehicleHead*>(EntityManager::createEntityVehicle());
    head.mode = TransportMode::road;
    head.tileX = -1;
    head.tileY = 123;
    head.subPosition = 17;
    head.routingHandle = Vehicles::RoutingHandle(7, 11);
    for (uint8_t side : { uint8_t{ 0 }, uint8_t{ 1 } })
    {
        getGameState().trafficHandedness = side;
        Vehicles::TrackAndDirection direction(1, side ? 4 : 0);
        const auto previewEnd = static_cast<uint16_t>(World::TrackData::getRoadPlacementSubPositon(direction.road._data).size() - 1);
        for (auto invalid : { previewEnd, uint16_t{ UINT16_MAX } })
        {
            EXPECT_EQ(VehicleManager::placeDownVehicle(&head, 640, 640, 4, direction, invalid), VehicleManager::PlaceDownResult::Unk0);
            EXPECT_EQ(head.tileX, -1);
            EXPECT_EQ(head.tileY, 123);
            EXPECT_EQ(head.subPosition, 17);
            EXPECT_EQ(head.routingHandle, Vehicles::RoutingHandle(7, 11));
        }
        direction.road._data = 80;
        EXPECT_EQ(VehicleManager::placeDownVehicle(&head, 640, 640, 4, direction, 0), VehicleManager::PlaceDownResult::Unk0);
        EXPECT_EQ(head.tileX, -1);
        EXPECT_EQ(head.subPosition, 17);
    }
    EntityManager::reset();
}
