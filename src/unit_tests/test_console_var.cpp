#include "catch.hpp"
#include "mmo_client/console/console_var.h"

namespace mmo
{
ConsoleVar::ConsoleVar(std::string name, std::string description, std::string defaultValue)
    : m_name(std::move(name))
    , m_description(std::move(description))
    , m_defaultValue(std::move(defaultValue))
{
    Reset();
}
}

using namespace mmo;

TEST_CASE("ConsoleVar Reset notifies once", "[console]")
{
    ConsoleVar var("test", "", "10");

    int notifyCount = 0;
    std::string lastOld;
    var.Changed.connect([&](ConsoleVar&, const std::string& oldValue) {
        ++notifyCount;
        lastOld = oldValue;
    });

    var.Set(std::string("20"));
    REQUIRE(notifyCount == 1);
    REQUIRE(lastOld == "10");

    var.Reset();
    REQUIRE(notifyCount == 2);
    REQUIRE(lastOld == "20");
}
