#include <OpenLoco/Config.h>
#include <OpenLoco/Date.h>
#include <gtest/gtest.h>

using namespace OpenLoco;

TEST(GameplayOptionsTests, obsoleteOptionSuppressesYearChecks)
{
    auto& config = Config::get();
    const auto previous = config.obsoleteDisabled;

    config.obsoleteDisabled = true;
    EXPECT_FALSE(isPastObsoleteYear(2000, 1950));
    EXPECT_FALSE(isAtOrPastObsoleteYear(1950, 1950));

    config.obsoleteDisabled = false;
    EXPECT_TRUE(isPastObsoleteYear(2000, 1950));
    EXPECT_FALSE(isPastObsoleteYear(1950, 1950));
    EXPECT_FALSE(isPastObsoleteYear(1900, 1950));
    EXPECT_TRUE(isAtOrPastObsoleteYear(1950, 1950));
    EXPECT_FALSE(isAtOrPastObsoleteYear(1949, 1950));

    config.obsoleteDisabled = previous;
}

TEST(GameplayOptionsTests, calendarIncrementFollowsSpeed)
{
    auto& config = Config::get();
    const auto previous = config.calendarSpeed;

    config.calendarSpeed = Config::CalendarSpeed::normal;
    EXPECT_EQ(calendarDayIncrement(), 682);

    config.calendarSpeed = Config::CalendarSpeed::slow;
    EXPECT_EQ(calendarDayIncrement(), 170);

    config.calendarSpeed = Config::CalendarSpeed::frozen;
    EXPECT_EQ(calendarDayIncrement(), 0);

    config.calendarSpeed = previous;
}
