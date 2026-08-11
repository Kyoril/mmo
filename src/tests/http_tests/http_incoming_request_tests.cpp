// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "http/http_incoming_request.h"

#include "base64/base64.h"

#include <string>

using namespace mmo;
using namespace mmo::net::http;

namespace
{
	struct ParseResult
	{
		ReceiveState state = receive_state::Incomplete;
		IncomingRequest request;
	};

	/// Feeds a raw request through IncomingRequest::Start. The buffer is kept alive by the
	/// caller's string, which MemorySource only borrows.
	ReceiveState Parse(const std::string& raw, IncomingRequest& request)
	{
		io::MemorySource source(raw.data(), raw.data() + raw.size());
		return IncomingRequest::Start(request, source);
	}

	std::string BasicCredentials(const std::string& user, const std::string& password)
	{
		const std::string joined = user + ":" + password;
		return "Basic " + base64_encode(
			reinterpret_cast<const unsigned char*>(joined.data()), joined.size());
	}
}

TEST_CASE("IncomingRequest parses each supported method", "[http][request]")
{
	struct Case { const char* method; IncomingRequest::Type type; };
	const Case cases[] = {
		{ "GET", IncomingRequest::Get },
		{ "HEAD", IncomingRequest::Head },
		{ "POST", IncomingRequest::Post },
		{ "OPTIONS", IncomingRequest::Options },
	};

	for (const auto& testCase : cases)
	{
		IncomingRequest request;
		const std::string raw =
			std::string(testCase.method) + " /index.html HTTP/1.1\r\n\r\n";

		CHECK(Parse(raw, request) == receive_state::Complete);
		CHECK(request.getType() == testCase.type);
		CHECK(request.getPath() == "/index.html");
	}
}

TEST_CASE("IncomingRequest rejects an unknown method", "[http][request]")
{
	IncomingRequest request;
	CHECK(Parse("DELETE /index.html HTTP/1.1\r\n\r\n", request) == receive_state::Malformed);
}

TEST_CASE("IncomingRequest reports an empty buffer as incomplete", "[http][request]")
{
	IncomingRequest request;
	CHECK(Parse("", request) == receive_state::Incomplete);
}

TEST_CASE("IncomingRequest reports a truncated request line as incomplete", "[http][request]")
{
	SECTION("no path")
	{
		IncomingRequest request;
		CHECK(Parse("GET ", request) == receive_state::Incomplete);
	}

	SECTION("no version")
	{
		IncomingRequest request;
		CHECK(Parse("GET /index.html ", request) == receive_state::Incomplete);
	}

	SECTION("no blank line after the headers")
	{
		IncomingRequest request;
		CHECK(Parse("GET /index.html HTTP/1.1\r\n", request) == receive_state::Incomplete);
	}
}

TEST_CASE("IncomingRequest requires CRLF, not a bare newline", "[http][request]")
{
	IncomingRequest request;
	CHECK(Parse("GET /index.html HTTP/1.1\n\n", request) == receive_state::Malformed);
}

TEST_CASE("IncomingRequest parses headers and trims the value", "[http][request]")
{
	IncomingRequest request;
	const std::string raw =
		"GET /index.html HTTP/1.1\r\n"
		"Host:   example.com\r\n"
		"User-Agent: mmo-launcher\r\n"
		"\r\n";

	REQUIRE(Parse(raw, request) == receive_state::Complete);

	const auto& headers = request.getHeaders();
	REQUIRE(headers.count("Host") == 1);
	CHECK(headers.at("Host") == "example.com");
	CHECK(headers.at("User-Agent") == "mmo-launcher");
}

TEST_CASE("IncomingRequest header lookup is case sensitive", "[http][request]")
{
	// The headers land in a plain std::map keyed by the name exactly as it arrived, so a
	// client sending "content-length" is not found by a lookup for "Content-Length".
	IncomingRequest request;
	const std::string raw =
		"GET /index.html HTTP/1.1\r\n"
		"Host: example.com\r\n"
		"\r\n";

	REQUIRE(Parse(raw, request) == receive_state::Complete);
	CHECK(request.getHeaders().count("Host") == 1);
	CHECK(request.getHeaders().count("host") == 0);
}

TEST_CASE("IncomingRequest keeps the first of a duplicated header", "[http][request]")
{
	// Inserted with map::insert, which does not overwrite. A second X-Token is dropped.
	IncomingRequest request;
	const std::string raw =
		"GET /index.html HTTP/1.1\r\n"
		"X-Token: first\r\n"
		"X-Token: second\r\n"
		"\r\n";

	REQUIRE(Parse(raw, request) == receive_state::Complete);
	CHECK(request.getHeaders().at("X-Token") == "first");
}

TEST_CASE("IncomingRequest splits query arguments off the path", "[http][request]")
{
	IncomingRequest request;
	const std::string raw = "GET /realms?name=Test&id=7 HTTP/1.1\r\n\r\n";

	REQUIRE(Parse(raw, request) == receive_state::Complete);

	// The path is truncated at the '?', so handlers match on the route alone.
	CHECK(request.getPath() == "/realms");

	const auto& args = request.getPathArguments();
	REQUIRE(args.size() == 2);
	CHECK(args.at("name") == "Test");
	CHECK(args.at("id") == "7");
}

TEST_CASE("IncomingRequest drops query arguments that have no value", "[http][request]")
{
	// Only name=value pairs are stored, so a bare flag is silently discarded rather than
	// arriving with an empty value.
	IncomingRequest request;
	const std::string raw = "GET /realms?verbose&id=7 HTTP/1.1\r\n\r\n";

	REQUIRE(Parse(raw, request) == receive_state::Complete);
	CHECK(request.getPath() == "/realms");

	const auto& args = request.getPathArguments();
	CHECK(args.count("verbose") == 0);
	REQUIRE(args.count("id") == 1);
	CHECK(args.at("id") == "7");
}

TEST_CASE("IncomingRequest keeps an empty query argument value", "[http][request]")
{
	IncomingRequest request;
	const std::string raw = "GET /realms?name= HTTP/1.1\r\n\r\n";

	REQUIRE(Parse(raw, request) == receive_state::Complete);
	REQUIRE(request.getPathArguments().count("name") == 1);
	CHECK(request.getPathArguments().at("name").empty());
}

TEST_CASE("IncomingRequest tolerates a trailing ampersand", "[http][request]")
{
	IncomingRequest request;
	const std::string raw = "GET /realms?a=1&b=2& HTTP/1.1\r\n\r\n";

	REQUIRE(Parse(raw, request) == receive_state::Complete);
	CHECK(request.getPathArguments().size() == 2);
}

TEST_CASE("IncomingRequest reads a post body once Content-Length is satisfied",
	"[http][request]")
{
	IncomingRequest request;
	const std::string body = "name=Test&id=7";
	const std::string raw =
		"POST /create HTTP/1.1\r\n"
		"Content-Length: " + std::to_string(body.size()) + "\r\n"
		"\r\n" + body;

	REQUIRE(Parse(raw, request) == receive_state::Complete);
	CHECK(request.getPostData() == body);

	const auto& args = request.getPostFormArguments();
	REQUIRE(args.size() == 2);
	CHECK(args.at("name") == "Test");
	CHECK(args.at("id") == "7");
}

TEST_CASE("IncomingRequest waits for a post body that has not fully arrived",
	"[http][request]")
{
	IncomingRequest request;
	const std::string raw =
		"POST /create HTTP/1.1\r\n"
		"Content-Length: 32\r\n"
		"\r\n"
		"name=Test";

	CHECK(Parse(raw, request) == receive_state::Incomplete);
}

TEST_CASE("IncomingRequest matches Content-Length case insensitively", "[http][request]")
{
	// Unlike the header map itself, the Content-Length probe uses a case-insensitive
	// compare, so a lowercased header still gates the body read.
	IncomingRequest request;
	const std::string raw =
		"POST /create HTTP/1.1\r\n"
		"content-length: 9\r\n"
		"\r\n"
		"name=Test";

	REQUIRE(Parse(raw, request) == receive_state::Complete);
	CHECK(request.getPostData() == "name=Test");
}

TEST_CASE("IncomingRequest url-decodes post form values", "[http][request]")
{
	IncomingRequest request;
	const std::string body = "name=Hello+World%21";
	const std::string raw =
		"POST /create HTTP/1.1\r\n"
		"Content-Length: " + std::to_string(body.size()) + "\r\n"
		"\r\n" + body;

	REQUIRE(Parse(raw, request) == receive_state::Complete);
	REQUIRE(request.getPostFormArguments().count("name") == 1);
	CHECK(request.getPostFormArguments().at("name") == "Hello World!");
}

TEST_CASE("IncomingRequest discards every post form argument if one lacks a value",
	"[http][request]")
{
	// All-or-nothing: a single malformed pair clears the whole map rather than being
	// skipped, so a handler sees no arguments at all instead of a partial set.
	IncomingRequest request;
	const std::string body = "name=Test&broken";
	const std::string raw =
		"POST /create HTTP/1.1\r\n"
		"Content-Length: " + std::to_string(body.size()) + "\r\n"
		"\r\n" + body;

	REQUIRE(Parse(raw, request) == receive_state::Complete);
	CHECK(request.getPostFormArguments().empty());
}

TEST_CASE("IncomingRequest leaves post data empty without a Content-Length",
	"[http][request]")
{
	IncomingRequest request;
	const std::string raw =
		"POST /create HTTP/1.1\r\n"
		"\r\n"
		"name=Test";

	REQUIRE(Parse(raw, request) == receive_state::Complete);
	CHECK(request.getPostData().empty());
	CHECK(request.getPostFormArguments().empty());
}

TEST_CASE("IncomingRequest rejects a header line with no colon", "[http][request]")
{
	IncomingRequest request;
	const std::string raw =
		"GET /index.html HTTP/1.1\r\n"
		"NotAHeaderLine\r\n"
		"\r\n";

	CHECK(Parse(raw, request) == receive_state::Malformed);
}

TEST_CASE("authorize decodes basic credentials", "[http][authorize]")
{
	IncomingRequest request;
	const std::string raw =
		"GET /admin HTTP/1.1\r\n"
		"Authorization: " + BasicCredentials("admin", "s3cret") + "\r\n"
		"\r\n";

	REQUIRE(Parse(raw, request) == receive_state::Complete);

	std::string seenUser;
	std::string seenPassword;
	const bool result = authorize(request,
		[&](const std::string& user, const std::string& password)
		{
			seenUser = user;
			seenPassword = password;
			return true;
		});

	CHECK(result);
	CHECK(seenUser == "admin");
	CHECK(seenPassword == "s3cret");
}

TEST_CASE("authorize returns the verdict of the credential check", "[http][authorize]")
{
	IncomingRequest request;
	const std::string raw =
		"GET /admin HTTP/1.1\r\n"
		"Authorization: " + BasicCredentials("admin", "wrong") + "\r\n"
		"\r\n";

	REQUIRE(Parse(raw, request) == receive_state::Complete);
	CHECK_FALSE(authorize(request, [](const std::string&, const std::string&) { return false; }));
}

TEST_CASE("authorize fails without ever consulting the check when the header is absent",
	"[http][authorize]")
{
	IncomingRequest request;
	REQUIRE(Parse("GET /admin HTTP/1.1\r\n\r\n", request) == receive_state::Complete);

	bool consulted = false;
	const bool result = authorize(request,
		[&](const std::string&, const std::string&) { consulted = true; return true; });

	CHECK_FALSE(result);
	CHECK_FALSE(consulted);
}

TEST_CASE("authorize yields empty credentials for a non-basic scheme", "[http][authorize]")
{
	// Only "Basic " is understood. Anything else decodes to a pair of empty strings and is
	// still handed to the check, which is expected to reject it.
	IncomingRequest request;
	const std::string raw =
		"GET /admin HTTP/1.1\r\n"
		"Authorization: Bearer sometoken\r\n"
		"\r\n";

	REQUIRE(Parse(raw, request) == receive_state::Complete);

	std::string seenUser = "unset";
	std::string seenPassword = "unset";
	authorize(request,
		[&](const std::string& user, const std::string& password)
		{
			seenUser = user;
			seenPassword = password;
			return false;
		});

	CHECK(seenUser.empty());
	CHECK(seenPassword.empty());
}

TEST_CASE("authorize handles credentials with no colon", "[http][authorize]")
{
	const std::string encoded = "Basic " + base64_encode(
		reinterpret_cast<const unsigned char*>("adminonly"), 9);

	IncomingRequest request;
	const std::string raw =
		"GET /admin HTTP/1.1\r\n"
		"Authorization: " + encoded + "\r\n"
		"\r\n";

	REQUIRE(Parse(raw, request) == receive_state::Complete);

	std::string seenUser;
	std::string seenPassword = "unset";
	authorize(request,
		[&](const std::string& user, const std::string& password)
		{
			seenUser = user;
			seenPassword = password;
			return false;
		});

	// Everything before the (missing) colon is the user; the password is empty.
	CHECK(seenUser == "adminonly");
	CHECK(seenPassword.empty());
}
