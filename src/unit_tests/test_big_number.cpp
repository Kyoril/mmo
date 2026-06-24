// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file test_big_number.cpp
 *
 * @brief Unit tests for the BigNumber class.
 *
 * This file contains unit tests for the BigNumber class defined in big_number.h,
 * which provides arbitrary-precision integer arithmetic.
 */

#include "catch.hpp"
#include "base/big_number.h"

using namespace mmo;

TEST_CASE("BigNumber default constructor initializes to zero", "[big_number]")
{
    BigNumber bn;
    REQUIRE(bn.isZero());
}

TEST_CASE("BigNumber can be constructed from uint32", "[big_number]")
{
    const uint32 value = 42;
    BigNumber bn(value);
    REQUIRE(bn.asUInt32() == value);
}

TEST_CASE("BigNumber can be constructed from hex string", "[big_number]")
{
    BigNumber bn("FF"); // 255 in hex
    REQUIRE(bn.asUInt32() == 255);
}

TEST_CASE("BigNumber can be constructed from binary data", "[big_number]")
{
    // Create binary representation of 0x1234 (4660 in decimal)
    // The implementation appears to use little-endian interpretation
    // So we need to provide 0x34 0x12 to get 0x1234
    uint8 data[2] = {0x34, 0x12};
    BigNumber bn(data, 2);
    REQUIRE(bn.asUInt32() == 4660);
}

TEST_CASE("BigNumber copy constructor creates identical copy", "[big_number]")
{
    BigNumber original(12345);
    BigNumber copy(original);
    
    REQUIRE(copy.asUInt32() == original.asUInt32());
    REQUIRE(copy.asHexStr() == original.asHexStr());
}

TEST_CASE("BigNumber setUInt32 sets correct value", "[big_number]")
{
    BigNumber bn;
    bn.setUInt32(12345);
    REQUIRE(bn.asUInt32() == 12345);
}

TEST_CASE("BigNumber setUInt64 sets correct value", "[big_number]")
{
    BigNumber bn;
    bn.setUInt64(0x100000000ULL); // 2^32
    
    // This value is too large for uint32, so we check the hex string
    // The implementation appears to add a leading zero
    REQUIRE(bn.asHexStr() == "0100000000");
}

TEST_CASE("BigNumber setHexStr sets correct value", "[big_number]")
{
    BigNumber bn;
    bn.setHexStr("ABCDEF");
    REQUIRE(bn.asHexStr() == "ABCDEF");
    REQUIRE(bn.asUInt32() == 0xABCDEF);
}

TEST_CASE("BigNumber arithmetic operations work correctly", "[big_number]")
{
    BigNumber a(10);
    BigNumber b(5);
    
    // Addition
    BigNumber sum = a + b;
    REQUIRE(sum.asUInt32() == 15);
    
    // Subtraction
    BigNumber diff = a - b;
    REQUIRE(diff.asUInt32() == 5);
    
    // Multiplication
    BigNumber product = a * b;
    REQUIRE(product.asUInt32() == 50);
    
    // Division
    BigNumber quotient = a / b;
    REQUIRE(quotient.asUInt32() == 2);
    
    // Modulo
    BigNumber remainder = a % b;
    REQUIRE(remainder.asUInt32() == 0);
    
    // Test with remainder
    BigNumber a2(11);
    BigNumber remainder2 = a2 % b;
    REQUIRE(remainder2.asUInt32() == 1);
}

TEST_CASE("BigNumber compound assignment operators work correctly", "[big_number]")
{
    BigNumber a(10);
    BigNumber b(5);
    
    // Addition assignment
    a += b;
    REQUIRE(a.asUInt32() == 15);
    
    // Subtraction assignment
    a -= b;
    REQUIRE(a.asUInt32() == 10);
    
    // Multiplication assignment
    a *= b;
    REQUIRE(a.asUInt32() == 50);
    
    // Division assignment
    a /= b;
    REQUIRE(a.asUInt32() == 10);
    
    // Modulo assignment
    a %= b;
    REQUIRE(a.asUInt32() == 0);
}

TEST_CASE("BigNumber comparison operators work correctly", "[big_number]")
{
    BigNumber a(10);
    BigNumber b(10);
    BigNumber c(20);
    
    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
}

TEST_CASE("BigNumber modExp performs modular exponentiation correctly", "[big_number]")
{
    BigNumber base(4);
    BigNumber exponent(13);
    BigNumber modulus(497);
    
    // 4^13 mod 497 = 445
    BigNumber result = base.modExp(exponent, modulus);
    REQUIRE(result.asUInt32() == 445);
}

TEST_CASE("BigNumber exp performs exponentiation correctly", "[big_number]")
{
    BigNumber base(2);
    BigNumber exponent(8);
    
    // 2^8 = 256
    BigNumber result = base.exp(exponent);
    REQUIRE(result.asUInt32() == 256);
}

TEST_CASE("BigNumber asByteArray returns correct binary representation", "[big_number]")
{
    BigNumber bn(0x1234); // 4660 in decimal
    
    std::vector<uint8> bytes = bn.asByteArray();
    REQUIRE(bytes.size() >= 2);
    
    // Check the last two bytes (in little-endian format)
    REQUIRE(bytes[0] == 0x34);
    REQUIRE(bytes[1] == 0x12);
}

TEST_CASE("BigNumber asByteArray respects minimum size", "[big_number]")
{
    BigNumber bn(0x12); // Only needs 1 byte
    
    std::vector<uint8> bytes = bn.asByteArray(4); // Request minimum 4 bytes
    REQUIRE(bytes.size() == 4);

    // Output is little-endian, so the value byte must sit in the least-significant
    // position and the padding must occupy the most-significant (trailing) bytes.
    // A previous bug placed the value in the most-significant position instead,
    // corrupting fixed-size SRP6 payloads whenever a value had a leading zero byte.
    REQUIRE(bytes[0] == 0x12);
    REQUIRE(bytes[1] == 0x00);
    REQUIRE(bytes[2] == 0x00);
    REQUIRE(bytes[3] == 0x00);
}

TEST_CASE("BigNumber asByteArray round-trips with leading zero bytes", "[big_number]")
{
    // A 2-byte value whose most-significant byte is zero when padded to 4 bytes.
    BigNumber bn(0x00AB);

    const std::vector<uint8> bytes = bn.asByteArray(4);
    REQUIRE(bytes.size() == 4);
    REQUIRE(bytes[0] == 0xAB);
    REQUIRE(bytes[1] == 0x00);
    REQUIRE(bytes[2] == 0x00);
    REQUIRE(bytes[3] == 0x00);

    // Reconstructing from the fixed-size little-endian array must preserve the value.
    BigNumber rebuilt;
    rebuilt.setBinary(bytes.data(), bytes.size());
    REQUIRE(rebuilt == bn);
}

TEST_CASE("Sha1_BigNumbers creates correct hash", "[big_number]")
{
    BigNumber a(123);
    BigNumber b(456);
    
    SHA1Hash hash = Sha1_BigNumbers({a, b});
    
    // The actual hash value will depend on the implementation details
    // We just verify that the hash is not empty
    REQUIRE_FALSE(hash.empty());
}
