// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "srp_server.h"

#include "base/constants.h"
#include "base/sha1.h"

namespace mmo
{
	SrpChallenge SrpServer::GenerateChallenge()
	{
		// Randomise the server private ephemeral (19 bytes = 152 bits).
		m_b.setRand(19 * 8);

		// Compute g^b mod N
		const BigNumber gmod = constants::srp::g.modExp(m_b, constants::srp::N);

		// B = (3*v + g^b) mod N
		m_B = ((m_v * 3) + gmod) % constants::srp::N;

		return SrpChallenge{ m_B, m_s };
	}

	std::optional<SrpResult> SrpServer::VerifyProof(
		const std::array<uint8, 32>& A_bytes,
		const std::array<uint8, 20>& rec_M1,
		std::string_view accountName)
	{
		// Reconstruct the client public ephemeral from the raw bytes.
		BigNumber A{ A_bytes.data(), A_bytes.size() };

		// SRP6 safeguard: abort if A % N == 0.
		if ((A % constants::srp::N).isZero())
		{
			return std::nullopt;
		}

		// u = H(A | B)
		SHA1Hash hash = Sha1_BigNumbers({ A, m_B });
		BigNumber u{ hash.data(), hash.size() };

		// S = (A * v^u) ^ b  mod N
		BigNumber S = (A * (m_v.modExp(u, constants::srp::N))).modExp(m_b, constants::srp::N);

		// Interleave-hash S into the 40-byte session key vK.
		const std::vector<uint8> t = S.asByteArray(32);

		std::array<uint8, 16> t1;
		for (size_t i = 0; i < t1.size(); ++i)
		{
			t1[i] = t[i * 2];
		}
		hash = sha1(reinterpret_cast<const char*>(t1.data()), t1.size());

		std::array<uint8, 40> vK;
		for (size_t i = 0; i < 20; ++i)
		{
			vK[i * 2] = hash[i];
		}

		for (size_t i = 0; i < t1.size(); ++i)
		{
			t1[i] = t[i * 2 + 1];
		}
		hash = sha1(reinterpret_cast<const char*>(t1.data()), t1.size());
		for (size_t i = 0; i < 20; ++i)
		{
			vK[i * 2 + 1] = hash[i];
		}

		BigNumber K{ vK.data(), vK.size() };

		// t3 = H(N) XOR H(g)
		SHA1Hash h = Sha1_BigNumbers({ constants::srp::N });
		hash = Sha1_BigNumbers({ constants::srp::g });
		for (size_t i = 0; i < h.size(); ++i)
		{
			h[i] ^= hash[i];
		}
		BigNumber t3{ h.data(), h.size() };

		// Compute expected M1 = H(t3 | H(account) | s | A | B | K)
		//
		// IMPORTANT: the session key K must be hashed as the full, fixed-size 40-byte
		// interleaved key (vK). Routing it through a BigNumber (K.asByteArray()) would
		// strip leading zero bytes whenever the high byte happens to be 0x00 (~1/256 of
		// logins, since K is derived from random ephemerals). The client always hashes the
		// full 40 bytes, so a stripped K here yields a divergent M1 and a spurious
		// "wrong password" failure. Same reasoning applies to the M2 computation below.
		HashGeneratorSha1 sha;
		Sha1_Add_BigNumbers(sha, { t3 });
		const auto t4 = sha1(reinterpret_cast<const char*>(accountName.data()), accountName.size());
		sha.update(reinterpret_cast<const char*>(t4.data()), t4.size());
		Sha1_Add_BigNumbers(sha, { m_s, A, m_B });
		sha.update(reinterpret_cast<const char*>(vK.data()), vK.size());
		hash = sha.finalize();

		// Compare the raw 20-byte M1 digest against the value received from the client.
		// Comparing the raw digest (rather than a BigNumber round-trip) keeps any leading
		// zero bytes of the hash intact.
		if (!std::equal(hash.begin(), hash.end(), rec_M1.begin()))
		{
			return std::nullopt;
		}

		// M2 = H(A | M1 | K)  — sent back to the client for mutual authentication.
		// M1 is fed as its raw 20-byte digest and K as the full 40-byte key, matching the
		// client and avoiding the same leading-zero stripping issue described above.
		HashGeneratorSha1 m2gen;
		Sha1_Add_BigNumbers(m2gen, { A });
		m2gen.update(reinterpret_cast<const char*>(hash.data()), hash.size());
		m2gen.update(reinterpret_cast<const char*>(vK.data()), vK.size());
		const SHA1Hash m2 = m2gen.finalize();

		return SrpResult{ std::move(K), m2 };
	}

} // namespace mmo
