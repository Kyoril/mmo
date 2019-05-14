// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "non_copyable.h"
#include <memory>
#include <array>
#include <istream>

namespace mmo
{
	/// Represents a SHA1 hash in it's binary form
	typedef std::array<unsigned char, 20> SHA1Hash;

	struct HashGeneratorSha1 : public NonCopyable
	{
		explicit HashGeneratorSha1() noexcept;

		void update(const char* data, size_t len);

		SHA1Hash finalize();

	private:
		struct Context;
		std::shared_ptr<Context> m_context;

	private:
		void ensureContextCreated();
	};

	/// Generates a SHA1 hash based on the contents of an std::istream.
	/// @returns Binary SHA1 hash.
	SHA1Hash sha1(std::istream &source);
	/// Generates a SHA1 hash based on the contents of an std::istream.
	/// @returns Binary SHA1 hash.
	SHA1Hash sha1(const char* data, size_t len);
	/// Writes the hexadecimal string of a binary SHA1 hash to an std::ostream.
	/// @param sink The output stream.
	/// @param value The binary SHA1 hash.
	void sha1PrintHex(std::ostream &sink, const SHA1Hash &value);
	/// Parses a sha1 hex string.
	/// @param source The string to parse.
	/// @param error If not set to nullptr, true will be written if an error occurred.
	SHA1Hash sha1ParseHex(std::string &source, bool* error = nullptr);

	SHA1Hash sha1ParseHex(std::istream &source);
}
