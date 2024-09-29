// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "big_number.h"
#include <openssl/ssl.h>
#include <openssl/bn.h>
#include <algorithm>

namespace mmo
{
	BigNumber::BigNumber()
	{
		m_bn = BN_new();
	}

	BigNumber::BigNumber(const uint8 *data, size_t length)
	{
		m_bn = BN_new();
		setBinary(data, length);
	}

	BigNumber::BigNumber(const BigNumber &Other)
	{
		m_bn = BN_dup(Other.m_bn);
	}

	BigNumber::BigNumber(uint32 Value)
	{
		m_bn = BN_new();
		BN_set_word(m_bn, Value);
	}

	BigNumber::BigNumber(const String &Hex)
	{
		m_bn = BN_new();
		setHexStr(Hex);
	}

	BigNumber::~BigNumber()
	{
		BN_free(m_bn);
	}

	void BigNumber::setUInt32(uint32 value)
	{
		BN_set_word(m_bn, value);
	}

	void BigNumber::setUInt64(uint64 value)
	{
		BN_add_word(m_bn, static_cast<uint32>(value >> 32));
		BN_lshift(m_bn, m_bn, 32);
		BN_add_word(m_bn, static_cast<uint32>(value & 0xFFFFFFFF));
	}

	void BigNumber::setBinary(const uint8 *data, size_t length)
	{
		std::vector<uint8> vector(data, data + length);
		setBinary(vector);
	}

	void BigNumber::setBinary(const std::vector<uint8> &data)
	{
		// Copy and reverse
		std::vector<uint8> buffer(data);
		std::reverse(buffer.begin(), buffer.end());
		BN_bin2bn(buffer.data(), buffer.size(), m_bn);
	}

	void BigNumber::setHexStr(const String &Hex)
	{
		BN_hex2bn(&m_bn, Hex.c_str());
	}

	void BigNumber::setRand(int numBits)
	{
		BN_rand(m_bn, numBits, 0, 1);
	}

	bool BigNumber::isZero() const
	{
		return BN_is_zero(m_bn) != 0;
	}

	BigNumber BigNumber::modExp(const BigNumber &bn1, const BigNumber &bn2) const
	{
		BigNumber ret;

		BN_CTX *bnctx = BN_CTX_new();
		BN_mod_exp(ret.m_bn, m_bn, bn1.m_bn, bn2.m_bn, bnctx);
		BN_CTX_free(bnctx);

		return ret;
	}

	BigNumber BigNumber::exp(const BigNumber &Other) const
	{
		BigNumber ret;

		BN_CTX *bnctx = BN_CTX_new();
		BN_exp(ret.m_bn, m_bn, Other.m_bn, bnctx);
		BN_CTX_free(bnctx);

		return ret;
	}

	uint32 BigNumber::getNumBytes() const
	{
		return BN_num_bytes(m_bn);
	}

	uint32 BigNumber::asUInt32() const
	{
		return static_cast<uint32>(BN_get_word(m_bn));
	}

	std::vector<uint8> BigNumber::asByteArray(int minSize /*= 0*/) const
	{
		const int numBytes = getNumBytes();
		const int len = (minSize >= numBytes) ? minSize : numBytes;

		// Reserver enough memory
		std::vector<uint8> ret;
		ret.resize(len);

		// Clear array if needed
		if (len > numBytes)
		{
			std::fill(ret.begin(), ret.end(), 0);
		}

		BN_bn2bin(m_bn, ret.data());
		std::reverse(ret.begin(), ret.end());

		return ret;
	}

	String BigNumber::asHexStr() const
	{
		char *buffer = BN_bn2hex(m_bn);
		String ret(buffer);

		// Free this since we copied it to the string already. If we don't do this,
		// it will result in a memory leak (which is bad!)
		OPENSSL_free(buffer);

		return ret;
	}

	String BigNumber::asDecStr() const
	{
		char *buffer = BN_bn2dec(m_bn);
		String ret(buffer);

		// Free this since we copied it to the string already. If we don't do this,
		// it will result in a memory leak (which is bad!)
		OPENSSL_free(buffer);

		return ret;
	}

	BigNumber BigNumber::operator=(const BigNumber &Other)
	{
		BN_copy(m_bn, Other.m_bn);
		return *this;
	}

	BigNumber BigNumber::operator+=(const BigNumber &Other)
	{
		BN_add(m_bn, m_bn, Other.m_bn);
		return *this;
	}

	BigNumber BigNumber::operator-=(const BigNumber &Other)
	{
		BN_sub(m_bn, m_bn, Other.m_bn);
		return *this;
	}

	BigNumber BigNumber::operator*=(const BigNumber &Other)
	{
		BN_CTX *bnctx = BN_CTX_new();
		BN_mul(m_bn, m_bn, Other.m_bn, bnctx);
		BN_CTX_free(bnctx);

		return *this;
	}

	BigNumber BigNumber::operator/=(const BigNumber &Other)
	{
		BN_CTX *bnctx = BN_CTX_new();
		BN_div(m_bn, nullptr, m_bn, Other.m_bn, bnctx);
		BN_CTX_free(bnctx);

		return *this;
	}

	BigNumber BigNumber::operator%=(const BigNumber &Other)
	{
		BN_CTX *bnctx = BN_CTX_new();
		BN_mod(m_bn, m_bn, Other.m_bn, bnctx);
		BN_CTX_free(bnctx);

		return *this;
	}

	bool BigNumber::operator==(const BigNumber& Other) const
	{
		return BN_cmp(m_bn, Other.m_bn) == 0;
	}

	SHA1Hash Sha1_BigNumbers(std::initializer_list<BigNumber> args)
	{
		HashGeneratorSha1 gen;

		Sha1_Add_BigNumbers(gen, args);

		return gen.finalize();
	}

	void Sha1_Add_BigNumbers(HashGeneratorSha1& generator, std::initializer_list<BigNumber> args)
	{
		for (auto& num : args)
		{
			auto arr = num.asByteArray();
			generator.update(reinterpret_cast<const char*>(arr.data()), arr.size());
		}
	}
}
