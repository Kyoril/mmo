// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/sha1.h"

#include <string>
#include <sstream>
#include <cstring>

using namespace mmo;

namespace
{
	void CheckHash(const SHA1Hash &expected, const std::string &source)
	{
		REQUIRE(expected.size() == 20);

		SHA1Hash calculated;
		{
			calculated = sha1(source.c_str(), source.length());
		}

		CHECK(expected == calculated);
	}

	void CheckParse(const std::string &str, const SHA1Hash &raw)
	{
		std::istringstream sourceStream(str);
		const auto found = sha1ParseHex(sourceStream);
		REQUIRE(!sourceStream.bad());
		CHECK(found == raw);
	}
}

// This test ensures that sha1 hash generation is working as expected.
TEST_CASE("Sha1", "[sha1]")
{
	static const SHA1Hash EmptyDigest = { {
			0xda,
			0x39,
			0xa3,
			0xee,
			0x5e,
			0x6b,
			0x4b,
			0x0d,
			0x32,
			0x55,
			0xbf,
			0xef,
			0x95,
			0x60,
			0x18,
			0x90,
			0xaf,
			0xd8,
			0x07,
			0x09
		}
	};

	{
		const SHA1Hash raw = { {0} };
		CheckParse(std::string(2 * 20, '0'), raw);
	}

	{
		CheckParse("da39a3ee5e6b4b0d3255bfef95601890afd80709", EmptyDigest);
	}

	CheckHash(EmptyDigest, "");

	std::istringstream digest("a94a8fe5ccb19ba61c4c0873d391e987982fbbd3");
	CheckHash(sha1ParseHex(digest), "test");
}

TEST_CASE("SHA1 hash of a string", "[sha1]")
{
	const char* data = "Hello, world!";
	SHA1Hash hash = sha1(data, strlen(data));

	std::ostringstream oss;
	sha1PrintHex(oss, hash);
	REQUIRE(oss.str() == "943a702d06f34599aee1f8da8ef9f7296031d699");
}

TEST_CASE("SHA1 hash of a stream", "[sha1]")
{
	std::istringstream iss("Hello, world!");
	SHA1Hash hash = sha1(iss);

	std::ostringstream oss;
	sha1PrintHex(oss, hash);
	REQUIRE(oss.str() == "943a702d06f34599aee1f8da8ef9f7296031d699");
}

TEST_CASE("SHA1 parse hex string", "[sha1]")
{
	std::string hexString = "943a702d06f34599aee1f8da8ef9f7296031d699";
	bool error = false;
	SHA1Hash hash = sha1ParseHex(hexString, &error);

	REQUIRE(error == false);

	std::ostringstream oss;
	sha1PrintHex(oss, hash);
	REQUIRE(oss.str() == hexString);
}

TEST_CASE("SHA1 parse hex stream", "[sha1]")
{
	std::istringstream iss("943a702d06f34599aee1f8da8ef9f7296031d699");
	SHA1Hash hash = sha1ParseHex(iss);

	std::ostringstream oss;
	sha1PrintHex(oss, hash);
	REQUIRE(oss.str() == "943a702d06f34599aee1f8da8ef9f7296031d699");
}
