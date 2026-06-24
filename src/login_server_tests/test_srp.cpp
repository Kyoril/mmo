// SRP6-A round-trip test: verifies that client and server agree on the session key
// and that VerifyProof returns a valid M2 for a known account/password pair.

#include "catch.hpp"

#include "auth_protocol/srp_server.h"
#include "base/big_number.h"
#include "base/sha1.h"
#include "base/constants.h"

#include <algorithm>
#include <array>
#include <string>

namespace mmo
{

/// Derives (s, v) the same way login_http_handlers.cpp::calculateSV does.
static std::pair<BigNumber, BigNumber> deriveVerifier(const std::string& accountName, const std::string& password)
{
    std::string id = accountName;
    std::string pw = password;
    std::transform(id.begin(), id.end(), id.begin(), ::toupper);
    std::transform(pw.begin(), pw.end(), pw.begin(), ::toupper);

    const std::string authString = id + ":" + pw;
    const SHA1Hash authHash = sha1(authString.c_str(), authString.size());

    BigNumber s;
    s.setRand(32 * 8);

    HashGeneratorSha1 gen;
    gen.update(reinterpret_cast<const char*>(s.asByteArray().data()), s.getNumBytes());
    gen.update(reinterpret_cast<const char*>(authHash.data()), authHash.size());
    const SHA1Hash x_hash = gen.finalize();

    BigNumber x;
    x.setBinary(x_hash.data(), x_hash.size());

    BigNumber v_base = constants::srp::g;
    BigNumber v_mod = constants::srp::N;
    BigNumber v = v_base.modExp(x, v_mod);
    return { s, v };
}

TEST_CASE("SRP6-A round-trip: client and server agree on session key and M2", "[srp]")
{
    const std::string accountName = "TESTACCOUNT";
    const std::string password    = "PASSWORD";

    // -----------------------------------------------------------------------
    // Step 1: Server derives salt + verifier from the account record.
    // -----------------------------------------------------------------------
    auto [s, v] = deriveVerifier(accountName, password);

    // -----------------------------------------------------------------------
    // Step 2: Server generates the challenge (B, s).
    // -----------------------------------------------------------------------
    SrpServer server(s, v);
    SrpChallenge challenge = server.GenerateChallenge();
    BigNumber B = challenge.B;

    // -----------------------------------------------------------------------
    // Step 3: Client-side SRP6-A computation.
    // -----------------------------------------------------------------------

    // Client private ephemeral a and public ephemeral A = g^a mod N
    BigNumber a;
    a.setRand(19 * 8);
    BigNumber g2 = constants::srp::g;
    BigNumber N2 = constants::srp::N;
    BigNumber A = g2.modExp(a, N2);

    // x = H(s | H(ACCOUNT:PASSWORD))
    const std::string authStr = accountName + ":" + password;  // already upper
    const SHA1Hash authHash = sha1(authStr.c_str(), authStr.size());

    HashGeneratorSha1 xGen;
    xGen.update(reinterpret_cast<const char*>(s.asByteArray().data()), s.getNumBytes());
    xGen.update(reinterpret_cast<const char*>(authHash.data()), authHash.size());
    BigNumber x;
    x.setBinary(xGen.finalize().data(), 20);

    // u = H(A | B)
    SHA1Hash u_hash = Sha1_BigNumbers({ A, B });
    BigNumber u;
    u.setBinary(u_hash.data(), u_hash.size());

    // Client S = (B - 3*g^x) ^ (a + u*x)  mod N
    // Note: BigNumber operators are not const-qualified, so we need local copies.
    BigNumber N_copy = constants::srp::N;
    BigNumber g_copy = constants::srp::g;
    BigNumber gx = g_copy.modExp(x, N_copy);
    BigNumber k3gx = gx * BigNumber(3);
    // Ensure positive: add N before subtracting to avoid negative intermediates
    BigNumber B_copy = B;
    BigNumber base = (B_copy + N_copy - k3gx) % N_copy;
    BigNumber exp  = a + (u * x);
    BigNumber S_client = base.modExp(exp, N_copy);

    // K = interleave-hash(S) — must match server's derivation exactly
    const std::vector<uint8> t = S_client.asByteArray(32);

    std::array<uint8, 16> t1;
    for (size_t i = 0; i < t1.size(); ++i)
        t1[i] = t[i * 2];

    SHA1Hash h1 = sha1(reinterpret_cast<const char*>(t1.data()), t1.size());

    std::array<uint8, 40> vK;
    for (size_t i = 0; i < 20; ++i)
        vK[i * 2] = h1[i];

    for (size_t i = 0; i < t1.size(); ++i)
        t1[i] = t[i * 2 + 1];

    SHA1Hash h2 = sha1(reinterpret_cast<const char*>(t1.data()), t1.size());
    for (size_t i = 0; i < 20; ++i)
        vK[i * 2 + 1] = h2[i];

    BigNumber K_client{ vK.data(), vK.size() };

    // t3 = H(N) XOR H(g)
    SHA1Hash hN = Sha1_BigNumbers({ constants::srp::N });
    SHA1Hash hg = Sha1_BigNumbers({ constants::srp::g });
    for (size_t i = 0; i < hN.size(); ++i)
        hN[i] ^= hg[i];
    BigNumber t3{ hN.data(), hN.size() };

    // M1 = H(t3 | H(account) | s | A | B | K)
    // K is hashed as the full, fixed 40-byte interleaved session key (vK) exactly like the
    // real client does — NOT via a BigNumber, which would strip leading zero bytes.
    HashGeneratorSha1 m1Gen;
    Sha1_Add_BigNumbers(m1Gen, { t3 });
    SHA1Hash t4 = sha1(reinterpret_cast<const char*>(accountName.data()), accountName.size());
    m1Gen.update(reinterpret_cast<const char*>(t4.data()), t4.size());
    Sha1_Add_BigNumbers(m1Gen, { s, A, B });
    m1Gen.update(reinterpret_cast<const char*>(vK.data()), vK.size());
    SHA1Hash m1_hash = m1Gen.finalize();

    std::array<uint8, 32> A_bytes;
    {
        auto ab = A.asByteArray(32);
        std::copy(ab.begin(), ab.end(), A_bytes.begin());
    }
    std::array<uint8, 20> M1_array;
    std::copy(m1_hash.begin(), m1_hash.end(), M1_array.begin());

    // -----------------------------------------------------------------------
    // Step 4: Server verifies the proof.
    // -----------------------------------------------------------------------
    auto result = server.VerifyProof(A_bytes, M1_array, accountName);

    REQUIRE(result.has_value());

    // -----------------------------------------------------------------------
    // Step 5: Verify M2 = H(A | M1 | K)
    // M1 is hashed as its raw 20-byte digest and K as the full 40-byte key, matching the
    // client and the server's leading-zero-safe implementation.
    // -----------------------------------------------------------------------
    HashGeneratorSha1 m2Gen;
    Sha1_Add_BigNumbers(m2Gen, { A });
    m2Gen.update(reinterpret_cast<const char*>(m1_hash.data()), m1_hash.size());
    m2Gen.update(reinterpret_cast<const char*>(vK.data()), vK.size());
    SHA1Hash expected_m2 = m2Gen.finalize();

    REQUIRE(result->m2 == expected_m2);
}

// Regression test for the SRP6-A leading-zero bug: K, A, S and the proof hashes are derived
// from random ephemerals, so ~1/256 of logins produce a value with a high zero byte. Stripping
// that byte on one side only used to cause spurious "wrong password" failures. Running many
// independent handshakes makes it overwhelmingly likely we exercise at least one such case.
TEST_CASE("SRP6-A round-trip survives leading-zero ephemerals", "[srp]")
{
    const std::string accountName = "TESTACCOUNT";
    const std::string password    = "PASSWORD";

    auto [s, v] = deriveVerifier(accountName, password);

    const std::string authStr = accountName + ":" + password;
    const SHA1Hash authHash = sha1(authStr.c_str(), authStr.size());

    for (int iteration = 0; iteration < 1024; ++iteration)
    {
        SrpServer server(s, v);
        SrpChallenge challenge = server.GenerateChallenge();
        BigNumber B = challenge.B;

        BigNumber a;
        a.setRand(19 * 8);
        BigNumber g2 = constants::srp::g;
        BigNumber N2 = constants::srp::N;
        BigNumber A = g2.modExp(a, N2);

        HashGeneratorSha1 xGen;
        xGen.update(reinterpret_cast<const char*>(s.asByteArray().data()), s.getNumBytes());
        xGen.update(reinterpret_cast<const char*>(authHash.data()), authHash.size());
        BigNumber x;
        x.setBinary(xGen.finalize().data(), 20);

        SHA1Hash u_hash = Sha1_BigNumbers({ A, B });
        BigNumber u;
        u.setBinary(u_hash.data(), u_hash.size());

        BigNumber N_copy = constants::srp::N;
        BigNumber g_copy = constants::srp::g;
        BigNumber gx = g_copy.modExp(x, N_copy);
        BigNumber k3gx = gx * BigNumber(3);
        BigNumber B_copy = B;
        BigNumber base = (B_copy + N_copy - k3gx) % N_copy;
        BigNumber expo = a + (u * x);
        BigNumber S_client = base.modExp(expo, N_copy);

        const std::vector<uint8> t = S_client.asByteArray(32);
        std::array<uint8, 16> t1;
        for (size_t i = 0; i < t1.size(); ++i)
            t1[i] = t[i * 2];
        SHA1Hash h1 = sha1(reinterpret_cast<const char*>(t1.data()), t1.size());
        std::array<uint8, 40> vK;
        for (size_t i = 0; i < 20; ++i)
            vK[i * 2] = h1[i];
        for (size_t i = 0; i < t1.size(); ++i)
            t1[i] = t[i * 2 + 1];
        SHA1Hash h2 = sha1(reinterpret_cast<const char*>(t1.data()), t1.size());
        for (size_t i = 0; i < 20; ++i)
            vK[i * 2 + 1] = h2[i];

        SHA1Hash hN = Sha1_BigNumbers({ constants::srp::N });
        SHA1Hash hg = Sha1_BigNumbers({ constants::srp::g });
        for (size_t i = 0; i < hN.size(); ++i)
            hN[i] ^= hg[i];
        BigNumber t3{ hN.data(), hN.size() };

        HashGeneratorSha1 m1Gen;
        Sha1_Add_BigNumbers(m1Gen, { t3 });
        SHA1Hash t4 = sha1(reinterpret_cast<const char*>(accountName.data()), accountName.size());
        m1Gen.update(reinterpret_cast<const char*>(t4.data()), t4.size());
        Sha1_Add_BigNumbers(m1Gen, { s, A, B });
        m1Gen.update(reinterpret_cast<const char*>(vK.data()), vK.size());
        SHA1Hash m1_hash = m1Gen.finalize();

        std::array<uint8, 32> A_bytes{};
        {
            auto ab = A.asByteArray(32);
            std::copy(ab.begin(), ab.end(), A_bytes.begin());
        }
        std::array<uint8, 20> M1_array;
        std::copy(m1_hash.begin(), m1_hash.end(), M1_array.begin());

        auto result = server.VerifyProof(A_bytes, M1_array, accountName);
        INFO("iteration " << iteration);
        REQUIRE(result.has_value());
    }
}

} // namespace mmo
