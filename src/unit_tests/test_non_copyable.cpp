// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file test_non_copyable.cpp
 *
 * @brief Unit tests for the NonCopyable class.
 *
 * This file contains unit tests for the NonCopyable class defined in non_copyable.h,
 * which prevents derived classes from being copied.
 */

#include "catch.hpp"
#include "base/non_copyable.h"

using namespace mmo;

// Helper class that inherits from NonCopyable
class TestNonCopyable : public NonCopyable
{
public:
    TestNonCopyable() : value(0) {}
    explicit TestNonCopyable(int val) : value(val) {}
    
    int getValue() const { return value; }
    void setValue(int val) { value = val; }
    
private:
    int value;
};

TEST_CASE("NonCopyable prevents copying", "[non_copyable]")
{
    TestNonCopyable original(42);
    REQUIRE(original.getValue() == 42);
    
    // The following lines would not compile due to deleted copy constructor and assignment operator
    // Uncomment to verify compilation failure
    
    // Copy construction
    // TestNonCopyable copy(original);
    
    // Copy assignment
    // TestNonCopyable another;
    // another = original;
    
    // Verify that we can still create and use non-copyable objects normally
    TestNonCopyable another(10);
    REQUIRE(another.getValue() == 10);
    
    another.setValue(20);
    REQUIRE(another.getValue() == 20);
}

TEST_CASE("NonCopyable allows move construction if implemented", "[non_copyable]")
{
    // This test demonstrates that NonCopyable doesn't prevent move operations
    // A class derived from NonCopyable can still implement move operations
    
    class MoveableNonCopyable : public NonCopyable
    {
    public:
        MoveableNonCopyable() : value(0) {}
        explicit MoveableNonCopyable(int val) : value(val) {}
        
        // Move constructor
        MoveableNonCopyable(MoveableNonCopyable&& other) noexcept
            : value(other.value)
        {
            other.value = 0;
        }
        
        // Move assignment
        MoveableNonCopyable& operator=(MoveableNonCopyable&& other) noexcept
        {
            if (this != &other)
            {
                value = other.value;
                other.value = 0;
            }
            return *this;
        }
        
        int getValue() const { return value; }
        
    private:
        int value;
    };
    
    MoveableNonCopyable original(42);
    REQUIRE(original.getValue() == 42);
    
    // Move construction
    MoveableNonCopyable moved(std::move(original));
    REQUIRE(moved.getValue() == 42);
    REQUIRE(original.getValue() == 0); // Original should be reset
    
    // Move assignment
    MoveableNonCopyable another;
    REQUIRE(another.getValue() == 0);
    
    another = std::move(moved);
    REQUIRE(another.getValue() == 42);
    REQUIRE(moved.getValue() == 0); // Moved should be reset
}
