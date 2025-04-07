// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file big_number.h
 *
 * @brief Provides arbitrary-precision integer arithmetic.
 *
 * This file defines the BigNumber class which provides arbitrary-precision
 * integer arithmetic operations. It's used for cryptographic operations,
 * particularly in the authentication system.
 */

#pragma once

#include "typedefs.h"
#include <vector>
#include "sha1.h"

// Forward declaration of OpenSSL's BIGNUM structure
struct bignum_st;

namespace mmo
{
	/**
	 * @class BigNumber
	 * @brief Provides arbitrary-precision integer arithmetic.
	 *
	 * This class wraps OpenSSL's BIGNUM functionality to provide arbitrary-precision
	 * integer arithmetic. It's primarily used for cryptographic operations in the
	 * authentication system, such as SRP6 protocol implementation.
	 *
	 * The class supports various arithmetic operations, conversion between different
	 * formats, and cryptographic functions like modular exponentiation.
	 */
	class BigNumber final
	{
	public:
		/**
		 * @brief Default constructor.
		 * 
		 * Initializes a new BigNumber with a value of zero.
		 */
		BigNumber();
		
		/**
		 * @brief Copy constructor.
		 * 
		 * Creates a new BigNumber as a copy of an existing one.
		 * 
		 * @param n The BigNumber to copy.
		 */
		BigNumber(const BigNumber &n);
		
		/**
		 * @brief Constructs a BigNumber from a 32-bit unsigned integer.
		 * 
		 * @param number The 32-bit unsigned integer value.
		 */
		BigNumber(uint32 number);
		
		/**
		 * @brief Constructs a BigNumber from a hexadecimal string.
		 * 
		 * @param Hex The hexadecimal string representation.
		 */
		BigNumber(const String &Hex);
		
		/**
		 * @brief Constructs a BigNumber from a binary data array.
		 * 
		 * @param data Pointer to the binary data.
		 * @param length Length of the binary data in bytes.
		 */
		BigNumber(const uint8 *data, size_t length);
		
		/**
		 * @brief Destructor.
		 * 
		 * Frees all resources associated with this BigNumber.
		 */
		~BigNumber();

		/**
		 * @brief Sets the value to a 32-bit unsigned integer.
		 * 
		 * @param value The 32-bit unsigned integer value to set.
		 */
		void setUInt32(uint32 value);
		
		/**
		 * @brief Sets the value to a 64-bit unsigned integer.
		 * 
		 * @param value The 64-bit unsigned integer value to set.
		 */
		void setUInt64(uint64 value);
		
		/**
		 * @brief Sets the value from binary data.
		 * 
		 * @param data Pointer to the binary data.
		 * @param length Length of the binary data in bytes.
		 */
		void setBinary(const uint8 *data, size_t length);
		
		/**
		 * @brief Sets the value from a vector of binary data.
		 * 
		 * @param data Vector containing the binary data.
		 */
		void setBinary(const std::vector<uint8> &data);
		
		/**
		 * @brief Sets the value from a hexadecimal string.
		 * 
		 * @param string The hexadecimal string representation.
		 */
		void setHexStr(const String &string);
		
		/**
		 * @brief Sets the value to a random number with the specified number of bits.
		 * 
		 * @param numBits The number of bits for the random number.
		 */
		void setRand(int numBits);
		
		/**
		 * @brief Checks if the value is zero.
		 * 
		 * @return true if the value is zero, false otherwise.
		 */
		bool isZero() const;
		
		/**
		 * @brief Performs modular exponentiation: (this ^ bn1) % bn2.
		 * 
		 * @param bn1 The exponent.
		 * @param bn2 The modulus.
		 * @return A new BigNumber containing the result.
		 */
		BigNumber modExp(const BigNumber &bn1, const BigNumber &bn2) const;
		
		/**
		 * @brief Performs exponentiation: this ^ Other.
		 * 
		 * @param Other The exponent.
		 * @return A new BigNumber containing the result.
		 */
		BigNumber exp(const BigNumber &Other) const;
		
		/**
		 * @brief Gets the number of bytes needed to represent this number.
		 * 
		 * @return The number of bytes.
		 */
		uint32 getNumBytes() const;
		
		/**
		 * @brief Converts the value to a 32-bit unsigned integer.
		 * 
		 * @return The value as a 32-bit unsigned integer.
		 * @note This will truncate the value if it's larger than 32 bits.
		 */
		uint32 asUInt32() const;
		
		/**
		 * @brief Converts the value to a byte array.
		 * 
		 * @param minSize Minimum size of the resulting array in bytes (0 for auto-size).
		 * @return Vector containing the binary representation.
		 */
		std::vector<uint8> asByteArray(int minSize = 0) const;
		
		/**
		 * @brief Converts the value to a hexadecimal string.
		 * 
		 * @return The hexadecimal string representation.
		 */
		String asHexStr() const;
		
		/**
		 * @brief Converts the value to a decimal string.
		 * 
		 * @return The decimal string representation.
		 */
		String asDecStr() const;

	public:
		/**
		 * @brief Assignment operator.
		 * 
		 * @param Other The BigNumber to copy from.
		 * @return Reference to this BigNumber after assignment.
		 */
		BigNumber operator=(const BigNumber &Other);
		
		/**
		 * @brief Addition assignment operator.
		 * 
		 * @param Other The BigNumber to add.
		 * @return Reference to this BigNumber after addition.
		 */
		BigNumber operator+=(const BigNumber &Other);
		
		/**
		 * @brief Addition operator.
		 * 
		 * @param Other The BigNumber to add.
		 * @return A new BigNumber containing the sum.
		 */
		BigNumber operator+(const BigNumber &Other)
		{
			BigNumber t(*this);
			return t += Other;
		}
		
		/**
		 * @brief Subtraction assignment operator.
		 * 
		 * @param Other The BigNumber to subtract.
		 * @return Reference to this BigNumber after subtraction.
		 */
		BigNumber operator-=(const BigNumber &Other);
		
		/**
		 * @brief Subtraction operator.
		 * 
		 * @param Other The BigNumber to subtract.
		 * @return A new BigNumber containing the difference.
		 */
		BigNumber operator-(const BigNumber &Other)
		{
			BigNumber t(*this);
			return t -= Other;
		}
		
		/**
		 * @brief Multiplication assignment operator.
		 * 
		 * @param Other The BigNumber to multiply by.
		 * @return Reference to this BigNumber after multiplication.
		 */
		BigNumber operator*=(const BigNumber &Other);
		
		/**
		 * @brief Multiplication operator.
		 * 
		 * @param Other The BigNumber to multiply by.
		 * @return A new BigNumber containing the product.
		 */
		BigNumber operator*(const BigNumber &Other)
		{
			BigNumber t(*this);
			return t *= Other;
		}
		
		/**
		 * @brief Division assignment operator.
		 * 
		 * @param Other The BigNumber to divide by.
		 * @return Reference to this BigNumber after division.
		 */
		BigNumber operator/=(const BigNumber &Other);
		
		/**
		 * @brief Division operator.
		 * 
		 * @param Other The BigNumber to divide by.
		 * @return A new BigNumber containing the quotient.
		 */
		BigNumber operator/(const BigNumber &Other)
		{
			BigNumber t(*this);
			return t /= Other;
		}
		
		/**
		 * @brief Modulo assignment operator.
		 * 
		 * @param Other The BigNumber to use as modulus.
		 * @return Reference to this BigNumber after modulo operation.
		 */
		BigNumber operator%=(const BigNumber &Other);
		
		/**
		 * @brief Modulo operator.
		 * 
		 * @param Other The BigNumber to use as modulus.
		 * @return A new BigNumber containing the remainder.
		 */
		BigNumber operator%(const BigNumber &Other)
		{
			BigNumber t(*this);
			return t %= Other;
		}
		
		/**
		 * @brief Equality comparison operator.
		 * 
		 * @param Other The BigNumber to compare with.
		 * @return true if the numbers are equal, false otherwise.
		 */
		bool operator==(const BigNumber& Other) const;

	private:
		/**
		 * @brief Pointer to the OpenSSL BIGNUM structure.
		 * 
		 * This is the underlying representation of the arbitrary-precision integer.
		 */
		bignum_st *m_bn;
	};

	/**
	 * @brief Creates a SHA1 hash from a list of BigNumber objects.
	 * 
	 * @param args Initializer list of BigNumber objects to hash.
	 * @return The resulting SHA1 hash.
	 */
	SHA1Hash Sha1_BigNumbers(std::initializer_list<BigNumber> args);

	/**
	 * @brief Updates a SHA1 hash generator with a list of BigNumber objects.
	 * 
	 * @param generator The SHA1 hash generator to update.
	 * @param args Initializer list of BigNumber objects to add to the hash.
	 */
	void Sha1_Add_BigNumbers(HashGeneratorSha1& generator, std::initializer_list<BigNumber> args);
}
