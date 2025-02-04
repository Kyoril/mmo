#include "catch.hpp"
#include "base64/base64.h"

TEST_CASE("Base64 Encoding", "[base64]")
{
    SECTION("Encode empty string")
	{
        std::string input = "";
        std::string expected = "";
        REQUIRE(mmo::base64_encode(reinterpret_cast<const unsigned char*>(input.c_str()), input.length()) == expected);
    }

    SECTION("Encode 'f'")
	{
        std::string input = "f";
        std::string expected = "Zg==";
        REQUIRE(mmo::base64_encode(reinterpret_cast<const unsigned char*>(input.c_str()), input.length()) == expected);
    }

    SECTION("Encode 'fo'")
	{
        std::string input = "fo";
        std::string expected = "Zm8=";
        REQUIRE(mmo::base64_encode(reinterpret_cast<const unsigned char*>(input.c_str()), input.length()) == expected);
    }

    SECTION("Encode 'foo'")
	{
        std::string input = "foo";
        std::string expected = "Zm9v";
        REQUIRE(mmo::base64_encode(reinterpret_cast<const unsigned char*>(input.c_str()), input.length()) == expected);
    }

    SECTION("Encode 'foob'")
	{
        std::string input = "foob";
        std::string expected = "Zm9vYg==";
        REQUIRE(mmo::base64_encode(reinterpret_cast<const unsigned char*>(input.c_str()), input.length()) == expected);
    }

    SECTION("Encode 'fooba'")
	{
        std::string input = "fooba";
        std::string expected = "Zm9vYmE=";
        REQUIRE(mmo::base64_encode(reinterpret_cast<const unsigned char*>(input.c_str()), input.length()) == expected);
    }

    SECTION("Encode 'foobar'")
	{
        std::string input = "foobar";
        std::string expected = "Zm9vYmFy";
        REQUIRE(mmo::base64_encode(reinterpret_cast<const unsigned char*>(input.c_str()), input.length()) == expected);
    }
}

TEST_CASE("Base64 Decoding", "[base64]")
{
    SECTION("Decode empty string")
	{
        std::string input = "";
        std::string expected = "";
        REQUIRE(mmo::base64_decode(input) == expected);
    }

    SECTION("Decode 'Zg=='")
	{
        std::string input = "Zg==";
        std::string expected = "f";
        REQUIRE(mmo::base64_decode(input) == expected);
    }

    SECTION("Decode 'Zm8='")
	{
        std::string input = "Zm8=";
        std::string expected = "fo";
        REQUIRE(mmo::base64_decode(input) == expected);
    }

    SECTION("Decode 'Zm9v'")
	{
        std::string input = "Zm9v";
        std::string expected = "foo";
        REQUIRE(mmo::base64_decode(input) == expected);
    }

    SECTION("Decode 'Zm9vYg=='")
	{
        std::string input = "Zm9vYg==";
        std::string expected = "foob";
        REQUIRE(mmo::base64_decode(input) == expected);
    }

    SECTION("Decode 'Zm9vYmE='")
	{
        std::string input = "Zm9vYmE=";
        std::string expected = "fooba";
        REQUIRE(mmo::base64_decode(input) == expected);
    }

    SECTION("Decode 'Zm9vYmFy'")
	{
        std::string input = "Zm9vYmFy";
        std::string expected = "foobar";
        REQUIRE(mmo::base64_decode(input) == expected);
    }
}
