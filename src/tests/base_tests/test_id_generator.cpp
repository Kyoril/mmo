#include "catch.hpp"
#include "base/id_generator.h"

TEST_CASE("IdGenerator generates sequential ids", "[id_generator]")
{
    mmo::IdGenerator<int> generator;

    REQUIRE(generator.GenerateId() == 0);
    REQUIRE(generator.GenerateId() == 1);
    REQUIRE(generator.GenerateId() == 2);
}

TEST_CASE("IdGenerator starts from given offset", "[id_generator]")
{
    mmo::IdGenerator<int> generator(10);

    REQUIRE(generator.GenerateId() == 10);
    REQUIRE(generator.GenerateId() == 11);
    REQUIRE(generator.GenerateId() == 12);
}

TEST_CASE("IdGenerator notifies used id", "[id_generator]")
{
    mmo::IdGenerator<int> generator;

    generator.NotifyId(5);
    REQUIRE(generator.GenerateId() == 6);
}

TEST_CASE("IdGenerator resets to initial value", "[id_generator]")
{
    mmo::IdGenerator<int> generator(5);

    generator.GenerateId();
    generator.GenerateId();
    generator.Reset();
    REQUIRE(generator.GenerateId() == 5);
}

TEST_CASE("IdGenerator move constructor", "[id_generator]")
{
    mmo::IdGenerator<int> generator1(5);
    mmo::IdGenerator<int> generator2(std::move(generator1));

    REQUIRE(generator2.GenerateId() == 5);
    REQUIRE(generator2.GenerateId() == 6);
}

TEST_CASE("IdGenerator move assignment", "[id_generator]")
{
    mmo::IdGenerator<int> generator1(5);
    mmo::IdGenerator<int> generator2;
    generator2 = std::move(generator1);

    REQUIRE(generator2.GenerateId() == 5);
    REQUIRE(generator2.GenerateId() == 6);
}
