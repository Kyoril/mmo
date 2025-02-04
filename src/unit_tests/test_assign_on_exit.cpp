#include "catch.hpp"
#include "base/assign_on_exit.h"

using namespace mmo;

TEST_CASE("AssignOnExit assigns value on destruction", "[assign_on_exit]")
{
    int originalValue = 10;
    int newValue = 20;

    {
        AssignOnExit assignOnExit(originalValue, newValue);
        REQUIRE(originalValue == 10); // Value should not change until destruction
    }

    REQUIRE(originalValue == 20); // Value should be assigned on destruction
}

TEST_CASE("AssignOnExit works with custom types", "[assign_on_exit]")
{
    struct CustomType
	{
        int value;
        bool operator==(const CustomType& other) const
    	{
            return value == other.value;
        }
    };

    CustomType originalValue{ 10 };
    CustomType newValue{ 20 };

    {
        AssignOnExit assignOnExit(originalValue, newValue);
        REQUIRE(originalValue.value == 10); // Value should not change until destruction
    }

    REQUIRE(originalValue.value == 20); // Value should be assigned on destruction
}

TEST_CASE("AssignOnExit does not allow copying", "[assign_on_exit]")
{
    int originalValue = 10;
    int newValue = 20;

    AssignOnExit assignOnExit(originalValue, newValue);

    // The following lines should not compile, uncomment to test
    // mmo::AssignOnExit<int> copy(assignOnExit);
    // mmo::AssignOnExit<int> assignOnExit2 = assignOnExit;
}
