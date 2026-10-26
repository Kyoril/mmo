// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <istream>
#include <memory>
#include <streambuf>


namespace mmo
{
	/// A read-only, seekable stream buffer over file content shared with its owner, so no copy is made.
	class SharedMemoryStreamBuf final : public std::streambuf
	{
	public:
		/// @brief Creates a buffer reading the given content.
		explicit SharedMemoryStreamBuf(std::shared_ptr<const String> content);

	protected:
		pos_type seekoff(off_type offset, std::ios_base::seekdir direction, std::ios_base::openmode which) override;
		pos_type seekpos(pos_type position, std::ios_base::openmode which) override;

	private:
		std::shared_ptr<const String> m_content;
	};

	/// An input stream over file content held in memory.
	class SharedMemoryStream final : public std::istream
	{
	public:
		/// @brief Creates a stream reading the given content from its start.
		explicit SharedMemoryStream(std::shared_ptr<const String> content);

	private:
		SharedMemoryStreamBuf m_buffer;
	};
}
