#include "catch.hpp"
#include "base/box.h"

using namespace mmo;


TEST_CASE("Box default constructor initializes to zero", "[box]")
{
    Box<int> box;
    REQUIRE(box.minimum == 0);
    REQUIRE(box.maximum == 0);
}

TEST_CASE("Box parameterized constructor initializes correctly", "[box]")
{
    Box<int> box(1, 2);
    REQUIRE(box.minimum == 1);
    REQUIRE(box.maximum == 2);
}

TEST_CASE("Box Clear method sets values to zero", "[box]")
{
    Box<int> box(1, 2);
    box.Clear();
    REQUIRE(box.minimum == 0);
    REQUIRE(box.maximum == 0);
}

TEST_CASE("Box unary plus operator returns the same box", "[box]")
{
    Box<int> box(1, 2);
    Box<int> result = +box;
    REQUIRE(result.minimum == 1);
    REQUIRE(result.maximum == 2);
}

TEST_CASE("Box unary minus operator negates the values", "[box]")
{
    Box<int> box(1, 2);
    Box<int> result = -box;
    REQUIRE(result.minimum == -1);
    REQUIRE(result.maximum == -2);
}

TEST_CASE("Box addition operator adds the values", "[box]")
{
    Box<int> box1(1, 2);
    Box<int> box2(3, 4);
    Box<int> result = box1 + box2;
    REQUIRE(result.minimum == 4);
    REQUIRE(result.maximum == 6);
}

TEST_CASE("Box subtraction operator subtracts the values", "[box]")
{
    Box<int> box1(5, 6);
    Box<int> box2(3, 4);
    Box<int> result = box1 - box2;
    REQUIRE(result.minimum == 2);
    REQUIRE(result.maximum == 2);
}

TEST_CASE("Box equality operator works correctly", "[box]")
{
    Box<int> box1(1, 2);
    Box<int> box2(1, 2);
    REQUIRE(box1 == box2);
}

TEST_CASE("Box inequality operator works correctly", "[box]")
{
    Box<int> box1(1, 2);
    Box<int> box2(3, 4);
    REQUIRE(box1 != box2);
}

TEST_CASE("Box stream insertion operator works correctly", "[box]")
{
    Box<int> box(1, 2);
    std::ostringstream os;
    os << box;
    REQUIRE(os.str() == "(1, 2)");
}

TEST_CASE("makeBox function creates a Box correctly", "[box]")
{
    Box<int> box = makeBox(1, 2);
    REQUIRE(box.minimum == 1);
    REQUIRE(box.maximum == 2);
}
