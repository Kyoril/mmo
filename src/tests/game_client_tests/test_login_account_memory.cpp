// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "mmo_client/login_account_memory.h"

using namespace mmo;

// The login screen offers a "Remember Account Name" checkbox
// (data/client/Interface/GlueUI/AccountLogin.xml). It stores the account name only, never the
// password, and unchecking it has to erase what an earlier login kept.

TEST_CASE("A login remembers the account name while the option is enabled", "[login_account_memory]")
{
	ConsoleVar remember(RememberAccountNameCVar, "", "1");
	ConsoleVar name(RememberedAccountNameCVar, "", "");

	REQUIRE(ApplyRememberedAccountName(remember, name, "Feck"));
	REQUIRE(name.GetStringValue() == "Feck");

	// Logging in again with the same account must not ask for another config write.
	REQUIRE_FALSE(ApplyRememberedAccountName(remember, name, "Feck"));
	REQUIRE(name.GetStringValue() == "Feck");

	// A different account replaces the remembered one.
	REQUIRE(ApplyRememberedAccountName(remember, name, "Kyoril"));
	REQUIRE(name.GetStringValue() == "Kyoril");
}

TEST_CASE("Turning the option off erases the remembered account name", "[login_account_memory]")
{
	ConsoleVar remember(RememberAccountNameCVar, "", "1");
	ConsoleVar name(RememberedAccountNameCVar, "", "");

	REQUIRE(ApplyRememberedAccountName(remember, name, "Feck"));

	// Unchecking the box must not merely stop updating the stored name, it has to clear it so
	// that the account name disappears from Config.cfg as well.
	remember.Set(std::string("0"));
	REQUIRE(ApplyRememberedAccountName(remember, name, name.GetStringValue()));
	REQUIRE(name.GetStringValue().empty());

	// With the option off, a login does not bring the name back either.
	REQUIRE_FALSE(ApplyRememberedAccountName(remember, name, "Feck"));
	REQUIRE(name.GetStringValue().empty());
}

TEST_CASE("A hand edited config cannot keep an account name the option forbids", "[login_account_memory]")
{
	// Config.cfg is plain text: someone may leave "set accountName Feck" in it while
	// rememberAccountName is off. Startup applies the option, not the leftover value.
	ConsoleVar remember(RememberAccountNameCVar, "", "0");
	ConsoleVar name(RememberedAccountNameCVar, "", "");
	name.Set(std::string("Feck"));

	REQUIRE(ApplyRememberedAccountName(remember, name, name.GetStringValue()));
	REQUIRE(name.GetStringValue().empty());
}
