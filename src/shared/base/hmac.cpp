// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "hmac.h"

#include "base/macros.h"

#include <iomanip>
#include <cctype>

#include <openssl/hmac.h>


namespace mmo
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
	struct HashGeneratorHmac::Context : HMAC_CTX
	{
	};

#else
	struct HashGeneratorHmac::Context
	{
		HMAC_CTX* Ctx;

		Context()
			: Ctx(nullptr)
		{
		}
		Context(const std::array<unsigned char, 16>& key)
			: Ctx(nullptr)
		{
			Ctx = HMAC_CTX_new();
			HMAC_Init_ex(Ctx, key.data(), key.size(), EVP_sha1(), nullptr);
		}
		~Context()
		{
			if (Ctx)
			{
				HMAC_CTX_free(Ctx);
				Ctx = nullptr;
			}
		}
	};
#endif

	HMACHash Hmac(std::istream & source)
	{
		HashGeneratorHmac generator;

		char buffer[4096];
		size_t read = 0;
		do
		{
			source.read(buffer, 4096);
			read = source.gcount();
			if (read > 0) generator.Update(buffer, read);
		} while (source && read > 0);

		return generator.Finalize();
	}

	HMACHash Hmac(const char* data, size_t len)
	{
		HashGeneratorHmac generator;
		generator.Update(data, len);
		return generator.Finalize();
	}

	void HmacPrintHex(std::ostream &sink, const HMACHash &value)
	{
		for (unsigned char e : value)
		{
			sink
				<< std::hex
				<< std::setfill('0')
				<< std::setw(2)
				<< (static_cast<unsigned>(e) & 0xff);
		}
	}

	namespace
	{
		static int hexDigitValue(char c)
		{
			if (c >= '0' && c <= '9')
			{
				return (c - '0');
			}

			c = static_cast<char>(std::tolower(c));

			if (c >= 'a' && c <= 'f')
			{
				return (c - 'a') + 10;
			}

			return -1;
		}
	}

	HMACHash HmacParseHex(std::string &source, bool* error)
	{
		HMACHash result;

		size_t i = 0;
		for (unsigned char & e : result)
		{
			if (i >= source.size())
			{
				if (error) *error = true;
				break;
			}

			char high = source[i++];

			if (i >= source.size())
			{
				if (error) *error = true;
				break;
			}

			char low = source[i++];

			const int left = hexDigitValue(high);
			const int right = hexDigitValue(low);

			if (left < 0 ||
				right < 0)
			{
				if (error) *error = true;
				break;
			}

			e = static_cast<unsigned char>((left * 16) + right);
		}

		if (error) *error = false;
		return result;
	}

	HMACHash HmacParseHex(std::istream &source)
	{
		HMACHash result;

		for (unsigned char & e : result)
		{
			char high, low;
			source >> high >> low;

			const int left = hexDigitValue(high);
			const int right = hexDigitValue(low);

			if (left < 0 ||
				right < 0)
			{
				source.setstate(std::ios::failbit);
				break;
			}

			e = static_cast<unsigned char>((left * 16) + right);
		}

		return result;
	}


	HashGeneratorHmac::HashGeneratorHmac() noexcept
	{
		EnsureContextCreated();
	}

	void HashGeneratorHmac::EnsureContextCreated()
	{
		if (!m_context)
		{
			// Setup key
			size_t i = 0;
			m_key[i++] = 0x38;
			m_key[i++] = 0xA7;
			m_key[i++] = 0x83;
			m_key[i++] = 0x15;
			m_key[i++] = 0xF8;
			m_key[i++] = 0x92;
			m_key[i++] = 0x25;
			m_key[i++] = 0x30;
			m_key[i++] = 0x71;
			m_key[i++] = 0x98;
			m_key[i++] = 0x67;
			m_key[i++] = 0xB1;
			m_key[i++] = 0x8C;
			m_key[i++] = 0x04;
			m_key[i++] = 0xE2;
			m_key[i++] = 0xAA;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
			m_context = std::make_shared<Context>();
			HMAC_CTX_init(m_context.get());
#ifdef __APPLE__
			HMAC_Init_ex(m_context.get(), m_key.data(), m_key.size(), EVP_sha1(), nullptr);
#else
			if (!HMAC_Init_ex(m_context.get(), m_key.data(), m_key.size(), EVP_sha1(), nullptr))
			{
				throw std::runtime_error("HMAC_Init_ex failed");
			}
#endif
#else
			m_context = std::make_shared<Context>(std::ref(m_key));
#endif
		}
	}

	void HashGeneratorHmac::Update(const char * data, size_t len)
	{
		ASSERT(data);
		ASSERT(len >= 0);

		if (!m_context)
		{
			EnsureContextCreated();
		}

#ifdef __APPLE__
		HMAC_Update(reinterpret_cast<HMAC_CTX*>(GetContext()), reinterpret_cast<const unsigned char *>(data), len);
#else
		if (!HMAC_Update(reinterpret_cast<HMAC_CTX*>(GetContext()), reinterpret_cast<const unsigned char *>(data), len))
		{
			throw std::runtime_error("HMAC_Update failed");
		}
#endif
	}

	HMACHash HashGeneratorHmac::Finalize()
	{
		HMACHash digest;
		ASSERT(digest.size() == 20);
		unsigned int len = 0;

#ifdef __APPLE__
		HMAC_Final(reinterpret_cast<HMAC_CTX*>(GetContext()), digest.data(), &len);
#else
		if (!HMAC_Final(reinterpret_cast<HMAC_CTX*>(GetContext()), digest.data(), &len))
		{
			throw std::runtime_error("HMAC_Final failed");
		}
#endif
		ASSERT(len == 20);

		m_context.reset();
		return digest;
	}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	HashGeneratorHmac::Context* HashGeneratorHmac::GetContext()
	{
		return m_context.get();
	}
#else
	hmac_ctx_st* HashGeneratorHmac::GetContext()
	{
		return reinterpret_cast<hmac_ctx_st*>(m_context->Ctx);
	}
#endif
}
