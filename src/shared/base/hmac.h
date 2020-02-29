// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "non_copyable.h"

#include <memory>
#include <array>
#include <istream>

#include <openssl/opensslv.h>


namespace mmo
{
	/// Represents a SHA1 hash in it's binary form
	typedef std::array<unsigned char, 20> HMACHash;

	/// 
	class HashGeneratorHmac final
		: public NonCopyable
	{
	private:

		struct Context;

	public:
		explicit HashGeneratorHmac() noexcept;

	public:
		void Update(const char* data, size_t len);

		template<typename T>
		void Update(const T& data)
		{
			Update(reinterpret_cast<const char*>(&data), sizeof(data));
		}

		HMACHash Finalize();

	private:
#if OPENSSL_VERSION_NUMBER < 0x10100000L
		Context* GetContext();
#else
		struct hmac_ctx_st* GetContext();
#endif

	private:
		std::shared_ptr<Context> m_context;
		std::array<unsigned char, 16> m_key;

	private:
		void EnsureContextCreated();
	};

	/// Generates a HMAC hash based on the contents of an std::istream.
	/// @returns Binary HMAC hash.
	HMACHash Hmac(std::istream &source);
	/// Generates a HMAC hash based on the contents of an std::istream.
	/// @returns Binary HMAC hash.
	HMACHash Hmac(const char* data, size_t len);
	/// Writes the hexadecimal string of a binary HMAC hash to an std::ostream.
	/// @param sink The output stream.
	/// @param value The binary HMAC hash.
	void HmacPrintHex(std::ostream &sink, const HMACHash &value);
	/// Parses a sha1 hex string.
	/// @param source The string to parse.
	/// @param error If not set to nullptr, true will be written if an error occurred.
	HMACHash HmacParseHex(std::string &source, bool* error = nullptr);

	HMACHash HmacParseHex(std::istream &source);
}
