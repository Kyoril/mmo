// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/big_number.h"
#include "base/sha1.h"
#include "base/constants.h"

#include <array>
#include <optional>
#include <string_view>

namespace mmo
{
	/// Holds the server-side SRP6-A challenge values sent to the client.
	struct SrpChallenge
	{
		BigNumber B;
		BigNumber s;
	};

	/// Holds the verified session key and server proof hash (M2).
	struct SrpResult
	{
		BigNumber K;
		SHA1Hash  m2;
	};

	/// Standalone SRP6-A server state machine (pure value type, no async/network/DB deps).
	/// Usage:
	///   1. Construct with (s, v) from the account record.
	///   2. Call GenerateChallenge() to obtain B and s to send to the client.
	///   3. Call VerifyProof() with client's A, M1, and account name to verify the login.
	class SrpServer
	{
	public:
		/// Construct the SRP server state from the account's salt and verifier.
		SrpServer(BigNumber s, BigNumber v)
			: m_s(std::move(s))
			, m_v(std::move(v))
		{
		}

		/// Generate the server challenge: randomise m_b, compute m_B, return {B, s}.
		SrpChallenge GenerateChallenge();

		/// Verify the client proof received during LogonProof.
		/// @param A_bytes    32-byte client public ephemeral (raw bytes, little-endian).
		/// @param rec_M1     20-byte M1 hash received from the client.
		/// @param accountName  Account name (used in M1 hash computation).
		/// @return SrpResult{K, m2} on success, std::nullopt on SRP safeguard failure or M1 mismatch.
		std::optional<SrpResult> VerifyProof(
			const std::array<uint8, 32>& A_bytes,
			const std::array<uint8, 20>& rec_M1,
			std::string_view accountName);

	private:
		BigNumber m_s;   ///< Account salt
		BigNumber m_v;   ///< Account verifier
		BigNumber m_b;   ///< Server private ephemeral (set in GenerateChallenge)
		BigNumber m_B;   ///< Server public ephemeral  (set in GenerateChallenge)
	};

} // namespace mmo
