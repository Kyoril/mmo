// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "updater/update_url.h"

#include <stdexcept>

using namespace mmo::updating;

TEST_CASE("UpdateURL parses an http url", "[updater][url]")
{
	const UpdateURL url("http://updates.example.com/patches/list.txt");

	CHECK(url.scheme == UP_HTTP);
	CHECK(url.host == "updates.example.com");
	CHECK(url.path == "/patches/list.txt");

	// No port in the url leaves the field zero; the caller supplies the scheme default.
	CHECK(url.port == 0);
}

TEST_CASE("UpdateURL parses an https url", "[updater][url]")
{
	const UpdateURL url("https://updates.example.com/list.txt");

	CHECK(url.scheme == UP_HTTPS);
	CHECK(url.host == "updates.example.com");
	CHECK(url.path == "/list.txt");
	CHECK(url.port == 0);
}

TEST_CASE("UpdateURL parses an explicit port", "[updater][url]")
{
	SECTION("http")
	{
		const UpdateURL url("http://updates.example.com:8080/list.txt");
		CHECK(url.host == "updates.example.com");
		CHECK(url.port == 8080);
		CHECK(url.path == "/list.txt");
	}

	SECTION("https")
	{
		const UpdateURL url("https://updates.example.com:8443/list.txt");
		CHECK(url.host == "updates.example.com");
		CHECK(url.port == 8443);
		CHECK(url.path == "/list.txt");
	}
}

TEST_CASE("UpdateURL defaults a missing path to root", "[updater][url]")
{
	SECTION("no path at all")
	{
		const UpdateURL url("http://updates.example.com");
		CHECK(url.host == "updates.example.com");
		CHECK(url.path == "/");
	}

	SECTION("no path but a port")
	{
		const UpdateURL url("http://updates.example.com:8080");
		CHECK(url.host == "updates.example.com");
		CHECK(url.port == 8080);
		CHECK(url.path == "/");
	}
}

TEST_CASE("UpdateURL keeps a trailing slash as the path", "[updater][url]")
{
	const UpdateURL url("http://updates.example.com/");

	CHECK(url.host == "updates.example.com");
	CHECK(url.path == "/");
}

TEST_CASE("UpdateURL treats anything without a known scheme as a filesystem path",
	"[updater][url]")
{
	SECTION("relative path")
	{
		const UpdateURL url("patches/list.txt");
		CHECK(url.scheme == UP_FileSystem);
		CHECK(url.path == "patches/list.txt");
		CHECK(url.host.empty());
		CHECK(url.port == 0);
	}

	SECTION("absolute windows path")
	{
		const UpdateURL url("C:/updates/list.txt");
		CHECK(url.scheme == UP_FileSystem);
		CHECK(url.path == "C:/updates/list.txt");
	}

	SECTION("an unhandled scheme is a path, not an error")
	{
		// ftp:// is not recognised, so it falls through to the filesystem branch verbatim
		// rather than being rejected.
		const UpdateURL url("ftp://updates.example.com/list.txt");
		CHECK(url.scheme == UP_FileSystem);
		CHECK(url.path == "ftp://updates.example.com/list.txt");
	}
}

TEST_CASE("UpdateURL rejects a url with no host", "[updater][url]")
{
	CHECK_THROWS_AS(UpdateURL("http:///list.txt"), std::invalid_argument);
	CHECK_THROWS_AS(UpdateURL("https:///list.txt"), std::invalid_argument);
	CHECK_THROWS_AS(UpdateURL("http://:8080/list.txt"), std::invalid_argument);
}

TEST_CASE("UpdateURL scheme matching is prefix-anchored", "[updater][url]")
{
	// rfind(prefix, 0) only matches at position zero, so a url merely containing "http://"
	// is not treated as one.
	const UpdateURL url("mirror-of-http://example.com/list.txt");

	CHECK(url.scheme == UP_FileSystem);
	CHECK(url.path == "mirror-of-http://example.com/list.txt");
}

TEST_CASE("UpdateURL reads a non-numeric port as zero", "[updater][url]")
{
	// Parsed with atoi, which reports no error. A garbage port is therefore silently the
	// same as no port at all rather than a rejected url.
	const UpdateURL url("http://updates.example.com:notaport/list.txt");

	CHECK(url.host == "updates.example.com");
	CHECK(url.port == 0);
	CHECK(url.path == "/list.txt");
}

TEST_CASE("UpdateURL only splits the port before the first slash", "[updater][url]")
{
	// A colon inside the path must not be mistaken for the port separator.
	const UpdateURL url("http://updates.example.com/patches/a:b.txt");

	CHECK(url.host == "updates.example.com");
	CHECK(url.port == 0);
	CHECK(url.path == "/patches/a:b.txt");
}

TEST_CASE("UpdateURL equality compares every field", "[updater][url]")
{
	const UpdateURL base(UP_HTTP, "example.com", 80, "/list.txt");

	CHECK(base == UpdateURL(UP_HTTP, "example.com", 80, "/list.txt"));
	CHECK_FALSE(base == UpdateURL(UP_HTTPS, "example.com", 80, "/list.txt"));
	CHECK_FALSE(base == UpdateURL(UP_HTTP, "other.com", 80, "/list.txt"));
	CHECK_FALSE(base == UpdateURL(UP_HTTP, "example.com", 8080, "/list.txt"));
	CHECK_FALSE(base == UpdateURL(UP_HTTP, "example.com", 80, "/other.txt"));
}

TEST_CASE("UpdateURL parsed and constructed forms agree", "[updater][url]")
{
	CHECK(UpdateURL("http://example.com:8080/list.txt")
		== UpdateURL(UP_HTTP, "example.com", 8080, "/list.txt"));
}
