// HTTP handler tests for LoginHttpHandlers — covers all six handlers,
// happy path + every documented error code.

#include "catch.hpp"

#include "login_server/login_http_handlers.h"
#include "login_server/realm_manager.h"
#include "login_server/player_manager.h"
#include "binary_io/memory_source.h"
#include "binary_io/vector_sink.h"
#include "mock_database.h"

#include <functional>
#include <string>
#include <vector>

namespace mmo
{

// ---------------------------------------------------------------------------
// Request construction helpers
// ---------------------------------------------------------------------------

/// Build a POST IncomingRequest and parse it into `req`.
static void makePost(const std::string& path, const std::string& body, net::http::IncomingRequest& req)
{
    std::string raw = "POST " + path + " HTTP/1.1\r\nHost: localhost\r\nContent-Length: "
        + std::to_string(body.size()) + "\r\n\r\n" + body;
    io::MemorySource src(raw.data(), raw.data() + raw.size());
    net::http::IncomingRequest::Start(req, src);
}

/// Build a GET IncomingRequest (path may contain query string) and parse it into `req`.
static void makeGet(const std::string& pathWithQuery, net::http::IncomingRequest& req)
{
    std::string raw = "GET " + pathWithQuery + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
    io::MemorySource src(raw.data(), raw.data() + raw.size());
    net::http::IncomingRequest::Start(req, src);
}

/// Invoke `handler` with `req` and capture the raw HTTP response bytes as a string.
static std::string captureResponse(
    std::function<void(const net::http::IncomingRequest&, web::WebResponse&)> handler,
    const net::http::IncomingRequest& req)
{
    std::vector<char> buf;
    io::VectorSink<char> sink(buf);
    web::WebResponse resp(sink);
    handler(req, resp);
    return std::string(buf.begin(), buf.end());
}

// ---------------------------------------------------------------------------
// HandleCreateAccount
// ---------------------------------------------------------------------------

TEST_CASE("HandleCreateAccount", "[http_handlers]")
{
    MockDatabase db;
    RealmManager realmMgr(0);
    PlayerManager playerMgr(0);
    LoginHttpHandlers handlers(db, realmMgr, playerMgr);

    SECTION("Happy path returns 200")
    {
        db.accountCreateResult = AccountCreationResult::Success;
        net::http::IncomingRequest req;
        makePost("/account/create", "id=ACCT&password=PASS", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleCreateAccount(r, w); }, req);
        REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
    }

    SECTION("Missing id returns 400 MISSING_PARAMETER")
    {
        net::http::IncomingRequest req;
        makePost("/account/create", "password=PASS", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleCreateAccount(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("MISSING_PARAMETER") != std::string::npos);
    }

    SECTION("Missing password returns 400 MISSING_PARAMETER")
    {
        net::http::IncomingRequest req;
        makePost("/account/create", "id=ACCT", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleCreateAccount(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("MISSING_PARAMETER") != std::string::npos);
    }

    SECTION("Name already in use returns 409 ACCOUNT_NAME_ALREADY_IN_USE")
    {
        db.accountCreateResult = AccountCreationResult::AccountNameAlreadyInUse;
        net::http::IncomingRequest req;
        makePost("/account/create", "id=ACCT&password=PASS", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleCreateAccount(r, w); }, req);
        REQUIRE(resp.find("409") != std::string::npos);
        REQUIRE(resp.find("ACCOUNT_NAME_ALREADY_IN_USE") != std::string::npos);
    }

    SECTION("Nullopt result returns 500 INTERNAL_SERVER_ERROR")
    {
        db.accountCreateResult = std::nullopt;
        net::http::IncomingRequest req;
        makePost("/account/create", "id=ACCT&password=PASS", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleCreateAccount(r, w); }, req);
        REQUIRE(resp.find("500") != std::string::npos);
        REQUIRE(resp.find("INTERNAL_SERVER_ERROR") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// HandleCreateRealm
// ---------------------------------------------------------------------------

TEST_CASE("HandleCreateRealm", "[http_handlers]")
{
    MockDatabase db;
    RealmManager realmMgr(0);
    PlayerManager playerMgr(0);
    LoginHttpHandlers handlers(db, realmMgr, playerMgr);

    SECTION("Happy path returns 200")
    {
        db.realmCreateResult = RealmCreationResult::Success;
        net::http::IncomingRequest req;
        makePost("/realm/create", "id=REALM&password=PASS&address=127.0.0.1&port=8085", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleCreateRealm(r, w); }, req);
        REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
    }

    SECTION("Missing id returns 400")
    {
        net::http::IncomingRequest req;
        makePost("/realm/create", "password=PASS&address=127.0.0.1&port=8085", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleCreateRealm(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("MISSING_PARAMETER") != std::string::npos);
    }

    SECTION("Missing password returns 400")
    {
        net::http::IncomingRequest req;
        makePost("/realm/create", "id=REALM&address=127.0.0.1&port=8085", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleCreateRealm(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("MISSING_PARAMETER") != std::string::npos);
    }

    SECTION("Missing address returns 400")
    {
        net::http::IncomingRequest req;
        makePost("/realm/create", "id=REALM&password=PASS&port=8085", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleCreateRealm(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("MISSING_PARAMETER") != std::string::npos);
    }

    SECTION("Missing port returns 400")
    {
        net::http::IncomingRequest req;
        makePost("/realm/create", "id=REALM&password=PASS&address=127.0.0.1", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleCreateRealm(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("MISSING_PARAMETER") != std::string::npos);
    }

    SECTION("Realm name in use returns 409 REALM_NAME_ALREADY_IN_USE")
    {
        db.realmCreateResult = RealmCreationResult::RealmNameAlreadyInUse;
        net::http::IncomingRequest req;
        makePost("/realm/create", "id=REALM&password=PASS&address=127.0.0.1&port=8085", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleCreateRealm(r, w); }, req);
        REQUIRE(resp.find("409") != std::string::npos);
        REQUIRE(resp.find("REALM_NAME_ALREADY_IN_USE") != std::string::npos);
    }

    SECTION("Nullopt result returns 500")
    {
        db.realmCreateResult = std::nullopt;
        net::http::IncomingRequest req;
        makePost("/realm/create", "id=REALM&password=PASS&address=127.0.0.1&port=8085", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleCreateRealm(r, w); }, req);
        REQUIRE(resp.find("500") != std::string::npos);
        REQUIRE(resp.find("INTERNAL_SERVER_ERROR") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// HandleBanAccount
// ---------------------------------------------------------------------------

TEST_CASE("HandleBanAccount", "[http_handlers]")
{
    MockDatabase db;
    RealmManager realmMgr(0);
    PlayerManager playerMgr(0);
    LoginHttpHandlers handlers(db, realmMgr, playerMgr);

    SECTION("Happy path returns 200 SUCCESS")
    {
        AccountData acct;
        acct.id = 1;
        acct.name = "ACCT";
        acct.banned = BanState::None;
        db.accountData = acct;
        net::http::IncomingRequest req;
        makePost("/account/ban", "account_name=ACCT", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleBanAccount(r, w); }, req);
        REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
        REQUIRE(resp.find("SUCCESS") != std::string::npos);
    }

    SECTION("Missing account_name returns 400")
    {
        net::http::IncomingRequest req;
        makePost("/account/ban", "", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleBanAccount(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("MISSING_PARAMETER") != std::string::npos);
    }

    SECTION("Invalid expiration returns 400 INVALID_PARAMETER")
    {
        AccountData acct;
        acct.id = 1;
        acct.name = "ACCT";
        acct.banned = BanState::None;
        db.accountData = acct;
        net::http::IncomingRequest req;
        makePost("/account/ban", "account_name=ACCT&expiration=NOTADATE", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleBanAccount(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("INVALID_PARAMETER") != std::string::npos);
    }

    SECTION("Reason too long returns 400 INVALID_PARAMETER")
    {
        AccountData acct;
        acct.id = 1;
        acct.name = "ACCT";
        acct.banned = BanState::None;
        db.accountData = acct;
        std::string longReason(257, 'x');
        net::http::IncomingRequest req;
        makePost("/account/ban", "account_name=ACCT&reason=" + longReason, req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleBanAccount(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("INVALID_PARAMETER") != std::string::npos);
    }

    SECTION("Account not found returns 404 ACCOUNT_DOES_NOT_EXIST")
    {
        db.accountData = std::nullopt;
        net::http::IncomingRequest req;
        makePost("/account/ban", "account_name=ACCT", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleBanAccount(r, w); }, req);
        REQUIRE(resp.find("404") != std::string::npos);
        REQUIRE(resp.find("ACCOUNT_DOES_NOT_EXIST") != std::string::npos);
    }

    SECTION("Already banned returns 409 ACCOUNT_ALREADY_BANNED")
    {
        AccountData acct;
        acct.id = 1;
        acct.name = "ACCT";
        acct.banned = BanState::Permanent;
        db.accountData = acct;
        net::http::IncomingRequest req;
        makePost("/account/ban", "account_name=ACCT", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleBanAccount(r, w); }, req);
        REQUIRE(resp.find("409") != std::string::npos);
        REQUIRE(resp.find("ACCOUNT_ALREADY_BANNED") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// HandleUnbanAccount
// ---------------------------------------------------------------------------

TEST_CASE("HandleUnbanAccount", "[http_handlers]")
{
    MockDatabase db;
    RealmManager realmMgr(0);
    PlayerManager playerMgr(0);
    LoginHttpHandlers handlers(db, realmMgr, playerMgr);

    SECTION("Happy path returns 200 SUCCESS")
    {
        net::http::IncomingRequest req;
        makePost("/account/unban", "account_name=ACCT", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleUnbanAccount(r, w); }, req);
        REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
        REQUIRE(resp.find("SUCCESS") != std::string::npos);
    }

    SECTION("Missing account_name returns 400")
    {
        net::http::IncomingRequest req;
        makePost("/account/unban", "", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleUnbanAccount(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("MISSING_PARAMETER") != std::string::npos);
    }

    SECTION("Reason too long returns 400 INVALID_PARAMETER")
    {
        std::string longReason(257, 'x');
        net::http::IncomingRequest req;
        makePost("/account/unban", "account_name=ACCT&reason=" + longReason, req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleUnbanAccount(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("INVALID_PARAMETER") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// HandleGetGMLevel
// ---------------------------------------------------------------------------

TEST_CASE("HandleGetGMLevel", "[http_handlers]")
{
    MockDatabase db;
    RealmManager realmMgr(0);
    PlayerManager playerMgr(0);
    LoginHttpHandlers handlers(db, realmMgr, playerMgr);

    SECTION("Happy path returns 200 SUCCESS with gm_level")
    {
        AccountData acct;
        acct.id = 1;
        acct.name = "ACCT";
        acct.banned = BanState::None;
        db.accountData = acct;
        db.sessionKeyResult = std::make_tuple(uint64(1), std::string("sessionkey"), uint8(3));
        net::http::IncomingRequest req;
        makeGet("/account/gmlevel?account_name=ACCT", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleGetGMLevel(r, w); }, req);
        REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
        REQUIRE(resp.find("SUCCESS") != std::string::npos);
        REQUIRE(resp.find("gm_level") != std::string::npos);
    }

    SECTION("Missing account_name returns 400")
    {
        net::http::IncomingRequest req;
        makeGet("/account/gmlevel", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleGetGMLevel(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("MISSING_PARAMETER") != std::string::npos);
    }

    SECTION("Account not found returns 404")
    {
        db.accountData = std::nullopt;
        net::http::IncomingRequest req;
        makeGet("/account/gmlevel?account_name=ACCT", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleGetGMLevel(r, w); }, req);
        REQUIRE(resp.find("404") != std::string::npos);
        REQUIRE(resp.find("ACCOUNT_DOES_NOT_EXIST") != std::string::npos);
    }

    SECTION("Session key nullopt returns 500")
    {
        AccountData acct;
        acct.id = 1;
        acct.name = "ACCT";
        acct.banned = BanState::None;
        db.accountData = acct;
        db.sessionKeyResult = std::nullopt;
        net::http::IncomingRequest req;
        makeGet("/account/gmlevel?account_name=ACCT", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleGetGMLevel(r, w); }, req);
        REQUIRE(resp.find("500") != std::string::npos);
        REQUIRE(resp.find("INTERNAL_SERVER_ERROR") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// HandleSetGMLevel
// ---------------------------------------------------------------------------

TEST_CASE("HandleSetGMLevel", "[http_handlers]")
{
    MockDatabase db;
    RealmManager realmMgr(0);
    PlayerManager playerMgr(0);
    LoginHttpHandlers handlers(db, realmMgr, playerMgr);

    SECTION("Happy path returns 200 SUCCESS with gm_level")
    {
        AccountData acct;
        acct.id = 1;
        acct.name = "ACCT";
        acct.banned = BanState::None;
        db.accountData = acct;
        db.setGmLevelResult = true;
        net::http::IncomingRequest req;
        makePost("/account/setgmlevel", "account_name=ACCT&gm_level=2", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleSetGMLevel(r, w); }, req);
        REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
        REQUIRE(resp.find("SUCCESS") != std::string::npos);
        REQUIRE(resp.find("gm_level") != std::string::npos);
    }

    SECTION("Missing account_name returns 400")
    {
        net::http::IncomingRequest req;
        makePost("/account/setgmlevel", "gm_level=2", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleSetGMLevel(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("MISSING_PARAMETER") != std::string::npos);
    }

    SECTION("Missing gm_level returns 400")
    {
        net::http::IncomingRequest req;
        makePost("/account/setgmlevel", "account_name=ACCT", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleSetGMLevel(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("MISSING_PARAMETER") != std::string::npos);
    }

    SECTION("Non-integer gm_level returns 400 INVALID_PARAMETER")
    {
        net::http::IncomingRequest req;
        makePost("/account/setgmlevel", "account_name=ACCT&gm_level=abc", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleSetGMLevel(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("INVALID_PARAMETER") != std::string::npos);
    }

    SECTION("Out-of-range gm_level (>255) returns 400 INVALID_PARAMETER")
    {
        net::http::IncomingRequest req;
        makePost("/account/setgmlevel", "account_name=ACCT&gm_level=999", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleSetGMLevel(r, w); }, req);
        REQUIRE(resp.find("400") != std::string::npos);
        REQUIRE(resp.find("INVALID_PARAMETER") != std::string::npos);
    }

    SECTION("Account not found returns 404")
    {
        db.accountData = std::nullopt;
        net::http::IncomingRequest req;
        makePost("/account/setgmlevel", "account_name=ACCT&gm_level=2", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleSetGMLevel(r, w); }, req);
        REQUIRE(resp.find("404") != std::string::npos);
        REQUIRE(resp.find("ACCOUNT_DOES_NOT_EXIST") != std::string::npos);
    }

    SECTION("SetGMLevel returns false returns 500 INTERNAL_SERVER_ERROR")
    {
        AccountData acct;
        acct.id = 1;
        acct.name = "ACCT";
        acct.banned = BanState::None;
        db.accountData = acct;
        db.setGmLevelResult = false;
        net::http::IncomingRequest req;
        makePost("/account/setgmlevel", "account_name=ACCT&gm_level=2", req);
        std::string resp = captureResponse(
            [&](const net::http::IncomingRequest& r, web::WebResponse& w){ handlers.HandleSetGMLevel(r, w); }, req);
        REQUIRE(resp.find("500") != std::string::npos);
        REQUIRE(resp.find("INTERNAL_SERVER_ERROR") != std::string::npos);
    }
}

} // namespace mmo
