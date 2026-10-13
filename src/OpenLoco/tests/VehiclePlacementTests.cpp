#include <OpenLoco/Entities/EntityManager.h>
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
