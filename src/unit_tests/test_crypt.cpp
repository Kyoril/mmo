// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "catch.hpp"

#include "base/big_number.h"
#include "game_protocol/game_crypt.h"

#include <array>

using namespace mmo;


/// This is the sample session key for encryption testing.
static const BigNumber CryptSessionKey{ "C02F4DFBE9512A59D60E61882C45B8FAFF93CB1E85F925B0C92E9BBF741FCEA1C3A6A0408DE992C4" };


// This test ensures that packet header encryption is working as expected.
TEST_CASE("EncryptionCheck", "[crypt]")
{
	game::Crypt crypt;

	// Generate key and initialize crypt
	HMACHash hash;
	crypt.GenerateKey(hash, CryptSessionKey);
	crypt.SetKey(hash.data(), hash.size());
	crypt.Init();

	// Ensure the crypted send length still matches
	REQUIRE( game::Crypt::CryptedSendLength == 6 );

	// Encrypt the header
	std::array<uint8, 6> header{ 0x00, 0x10, 0xfe, 0xdd, 0xaa, 0xbe };
	crypt.EncryptSend(header.data(), header.size());

	// Verify that the results match
	const std::array<uint8, 6> expected{ 0xed, 0x58, 0xa4, 0x3e, 0x17, 0xaa };
	CHECK( header == expected );
}

// This test ensures that packet header decryption is working as expected
TEST_CASE("DecryptionCheck", "[crypt]")
{
	mmo::game::Crypt crypt;

	// Generate key and initialize crypt
	HMACHash hash;
	crypt.GenerateKey(hash, CryptSessionKey);
	crypt.SetKey(hash.data(), hash.size());
	crypt.Init();

	// Ensure the crypted receive length still matches
	REQUIRE(game::Crypt::CryptedReceiveLength == 6);

	// Encrypt the header
	std::array<uint8, 6> header{ 0xed, 0x58, 0xa4, 0x3e, 0x17, 0xaa };
	crypt.DecryptReceive(header.data(), header.size());

	// Verify that the results match
	const std::array<uint8, 6> expected{ 0x00, 0x10, 0xfe, 0xdd, 0xaa, 0xbe };
	CHECK(header == expected);
}
