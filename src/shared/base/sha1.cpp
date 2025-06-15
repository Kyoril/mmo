// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "sha1.h"

#include <iomanip>
#include <cassert>
#include <cctype>

#include <openssl/sha.h>

#if OPENSSL_VERSION_MAJOR >= 3
#	include <openssl/types.h>
#	include <openssl/evp.h>
#endif

namespace mmo
{
#if OPENSSL_VERSION_MAJOR < 3
	struct HashGeneratorSha1::Context : SHA_CTX
	{
	};
#else
	struct HashGeneratorSha1::Context
	{
		EVP_MD_CTX* md_ctx = nullptr;

		Context()
		{
			md_ctx = EVP_MD_CTX_new();
		}
		~Context()
		{
			if (md_ctx)
			{
				EVP_MD_CTX_free(md_ctx);
			}
		}
	};
#endif

	SHA1Hash sha1(std::istream & source)
	{
		HashGeneratorSha1 generator;

		char buffer[4096];
		size_t read = 0;
		do
		{
			source.read(buffer, 4096);
			read = source.gcount();
			if (read > 0) generator.update(buffer, read);
		} while (source && read > 0);

		return generator.finalize();
	}

	SHA1Hash sha1(const char* data, size_t len)
	{
		HashGeneratorSha1 generator;
		generator.update(data, len);
		return generator.finalize();
	}

	void sha1PrintHex(std::ostream &sink, const SHA1Hash &value)
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

	SHA1Hash sha1ParseHex(std::string &source, bool* error)
	{
		SHA1Hash result;
		
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

	SHA1Hash sha1ParseHex(std::istream &source)
	{
		SHA1Hash result;

		for(unsigned char & e : result)
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

	HashGeneratorSha1::HashGeneratorSha1()
	{
	}

	void HashGeneratorSha1::ensureContextCreated()
	{
		if (!m_context)
		{
			m_context = std::make_shared<Context>();

#if OPENSSL_VERSION_MAJOR < 3
			if (!SHA1_Init(m_context.get()))
			{
				throw std::runtime_error("SHA1_Init failed");
			}
#else
			EVP_DigestInit(m_context->md_ctx, EVP_sha1());
#endif
		}
	}

	void HashGeneratorSha1::update(const char * data, size_t len)
	{
		ensureContextCreated();

#if OPENSSL_VERSION_MAJOR < 3
		if (!(SHA1_Update(m_context.get(), data, len)))
		{
			throw std::runtime_error("SHA1_Update failed");
		}
#else
		EVP_DigestUpdate(m_context->md_ctx, data, len);
#endif
	}

	SHA1Hash HashGeneratorSha1::finalize()
	{
		ensureContextCreated();

		SHA1Hash digest;
#if OPENSSL_VERSION_MAJOR < 3
		if (!SHA1_Final(digest.data(), m_context.get()))
		{
			throw std::runtime_error("SHA1_Final failed");
		}
#else
		EVP_DigestFinal(m_context->md_ctx, digest.data(), nullptr);
#endif

		m_context.reset();
		return digest;
	}
}
