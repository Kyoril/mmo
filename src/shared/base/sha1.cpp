// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "sha1.h"

#include <iomanip>
#include <cassert>

#include <openssl/sha.h>

namespace mmo
{
	struct HashGeneratorSha1::Context : SHA_CTX
	{
	};

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

	HashGeneratorSha1::HashGeneratorSha1() noexcept
	{
	}

	void HashGeneratorSha1::update(const char * data, size_t len)
	{
		if (!m_context)
		{
			m_context = std::make_shared<Context>();
			if (!SHA1_Init(m_context.get()))
			{
				throw std::runtime_error("SHA1_Init failed");
			}
		}

		if (!(SHA1_Update(m_context.get(), data, len)))
		{
			throw std::runtime_error("SHA1_Update failed");
		}
	}

	SHA1Hash HashGeneratorSha1::finalize()
	{
		SHA1Hash digest;
		if (!SHA1_Final(digest.data(), m_context.get()))
		{
			throw std::runtime_error("SHA1_Final failed");
		}

		m_context.reset();
		return digest;
	}
}
