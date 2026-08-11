#include "catch.hpp"
#include "base/erase_by_move.h"

TEST_CASE("EraseByMove removes the last element correctly", "[erase_by_move]")
{
    std::vector vec = { 1, 2, 3, 4, 5 };
    const auto it = mmo::EraseByMove(vec, vec.end() - 1);
    REQUIRE(vec == std::vector<int>({ 1, 2, 3, 4 }));
    REQUIRE(it == vec.end());
}

TEST_CASE("EraseByMove removes an element in the middle correctly", "[erase_by_move]")
{
    std::vector vec = { 1, 2, 3, 4, 5 };
    const auto it = mmo::EraseByMove(vec, vec.begin() + 2);
    REQUIRE(vec == std::vector<int>({ 1, 2, 5, 4 }));
    REQUIRE(it == vec.begin() + 2);
}

TEST_CASE("EraseByMove removes the first element correctly", "[erase_by_move]")
{
    std::vector vec = { 1, 2, 3, 4, 5 };
    const auto it = mmo::EraseByMove(vec, vec.begin());
    REQUIRE(vec == std::vector<int>({ 5, 2, 3, 4 }));
    REQUIRE(it == vec.begin());
}

TEST_CASE("EraseByMove works with an empty vector", "[erase_by_move]")
{
    std::vector<int> vec;
    const auto it = mmo::EraseByMove(vec, vec.begin());
    REQUIRE(vec.empty());
    REQUIRE(it == vec.end());
}

TEST_CASE("EraseByMove works with a single element vector", "[erase_by_move]")
{
    std::vector vec = { 1 };
    const auto it = mmo::EraseByMove(vec, vec.begin());
    REQUIRE(vec.empty());
    REQUIRE(it == vec.end());
}