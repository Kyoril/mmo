// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "typedefs.h"
#include <vector>
#include "sha1.h"

struct bignum_st;

namespace mmo
{
	class BigNumber final
	{
	public:

		/// Initializes an empty number (zero).
		BigNumber();
		/// Copies the value of another big number.
		BigNumber(const BigNumber &n);
		/// Initializes a number based on a 32 bit unsigned integer value.
		BigNumber(uint32 number);
		/// Initializes a number based on a hex string.
		BigNumber(const String &Hex);

		BigNumber(const uint8 *data, size_t length);
		~BigNumber();

		/// Sets the value of this number to the value of a 32 bit unsigned integer.
		void setUInt32(uint32 value);
		/// Sets the value of this number to the value of a 64 bit unsigned integer.
		void setUInt64(uint64 value);
		/// Sets the value of this number based on a binary data block.
		void setBinary(const uint8 *data, size_t length);
		/// Sets the value of this number based on a binary data block.
		void setBinary(const std::vector<uint8> &data);
		/// Sets the value of this number based on a hex string.
		void setHexStr(const String &string);
		/// Randomizes the value.
		void setRand(int numBits);
		/// Determines whether the value of this number is equal to zero.
		bool isZero() const;
		/// Modular exponentiation.
		BigNumber modExp(const BigNumber &bn1, const BigNumber &bn2) const;
		/// Exponential function.
		BigNumber exp(const BigNumber &Other) const;
		/// Gets the number of bytes this number needs.
		uint32 getNumBytes() const;
		/// Converts the value of this number to a 32 bit unsigned integer.
		uint32 asUInt32() const;
		/// Returns an array of bytes which represent the value of this number.
		std::vector<uint8> asByteArray(int minSize = 0) const;
		/// Returns a string of this number in it's hexadecimal form.
		String asHexStr() const;
		/// Returns a string of this number in it's decimal form.
		String asDecStr() const;

	public:

		BigNumber operator=(const BigNumber &Other);
		BigNumber operator+=(const BigNumber &Other);
		BigNumber operator+(const BigNumber &Other)
		{
			BigNumber t(*this);
			return t += Other;
		}
		BigNumber operator-=(const BigNumber &Other);
		BigNumber operator-(const BigNumber &Other)
		{
			BigNumber t(*this);
			return t -= Other;
		}
		BigNumber operator*=(const BigNumber &Other);
		BigNumber operator*(const BigNumber &Other)
		{
			BigNumber t(*this);
			return t *= Other;
		}
		BigNumber operator/=(const BigNumber &Other);
		BigNumber operator/(const BigNumber &Other)
		{
			BigNumber t(*this);
			return t /= Other;
		}
		BigNumber operator%=(const BigNumber &Other);
		BigNumber operator%(const BigNumber &Other)
		{
			BigNumber t(*this);
			return t %= Other;
		}

		bool operator==(const BigNumber& Other) const;

	private:

		bignum_st *m_bn;
	};

	/// Helper method to build a sha1 hash out of a list of BigNumber objects.
	SHA1Hash Sha1_BigNumbers(std::initializer_list<BigNumber> args);

	/// Helper method to update a sha1 hash generator with a list of BigNumber objects.
	void Sha1_Add_BigNumbers(HashGeneratorSha1& generator, std::initializer_list<BigNumber> args);
}
