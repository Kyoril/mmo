// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file test_clock.cpp
 *
 * @brief Unit tests for the clock functionality.
 *
 * This file contains unit tests for the time-related utilities defined in clock.h,
 * including time conversion functions and time measurement.
 */

#include "catch.hpp"
#include "base/clock.h"

// Include platform-specific headers for sleep functions
#if defined(WIN32) || defined(_WIN32)
    #include <Windows.h>
#else
    #include <unistd.h>
#endif

using namespace mmo;

TEST_CASE("GameTime constants are correctly defined", "[clock]")
{
    REQUIRE(constants::OneSecond == 1000);
    REQUIRE(constants::OneMinute == 60000);
    REQUIRE(constants::OneHour == 3600000);
    REQUIRE(constants::OneDay == 86400000);
}

TEST_CASE("gameTimeToSeconds converts milliseconds to seconds", "[clock]")
{
    // Test with integer type
    REQUIRE(gameTimeToSeconds<int>(2000) == 2);
    REQUIRE(gameTimeToSeconds<int>(500) == 0); // Integer division truncates
    
    // Test with floating point type
    REQUIRE(gameTimeToSeconds<float>(2000) == 2.0f);
    REQUIRE(gameTimeToSeconds<float>(500) == 0.5f);
    REQUIRE(gameTimeToSeconds<double>(1500) == 1.5);
}

TEST_CASE("gameTimeFromSeconds converts seconds to milliseconds", "[clock]")
{
    // Test with integer type
    REQUIRE(gameTimeFromSeconds<int>(2) == 2000);
    
    // Test with floating point type
    REQUIRE(gameTimeFromSeconds<float>(0.5f) == 500);
    REQUIRE(gameTimeFromSeconds<double>(1.5) == 1500);
}

TEST_CASE("GetAsyncTimeMs returns increasing values", "[clock]")
{
    // Get two time measurements with a small delay in between
    GameTime time1 = GetAsyncTimeMs();
    
    // Sleep for a small amount of time (10ms should be enough)
    #if defined(WIN32) || defined(_WIN32)
        Sleep(10);
    #else
        usleep(10000); // 10000 microseconds = 10ms
    #endif
    
    GameTime time2 = GetAsyncTimeMs();
    
    // The second time should be greater than the first
    REQUIRE(time2 > time1);
}
