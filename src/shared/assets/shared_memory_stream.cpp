// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "shared_memory_stream.h"


namespace mmo
{
	SharedMemoryStreamBuf::SharedMemoryStreamBuf(std::shared_ptr<const String> content)
		: m_content(std::move(content))
	{
		// The buffer never writes, so handing the get area a non-const pointer is safe.
		char* begin = const_cast<char*>(m_content->data());
		setg(begin, begin, begin + m_content->size());
	}

	SharedMemoryStreamBuf::pos_type SharedMemoryStreamBuf::seekoff(const off_type offset, const std::ios_base::seekdir direction, const std::ios_base::openmode which)
	{
		if (!(which & std::ios_base::in))
		{
			return pos_type(off_type(-1));
		}

		off_type base = 0;
		if (direction == std::ios_base::cur)
		{
			base = gptr() - eback();
		}
		else if (direction == std::ios_base::end)
		{
			base = egptr() - eback();
		}

		const off_type target = base + offset;
		if (target < 0 || target > egptr() - eback())
		{
			return pos_type(off_type(-1));
		}

		setg(eback(), eback() + target, egptr());
		return pos_type(target);
	}

	SharedMemoryStreamBuf::pos_type SharedMemoryStreamBuf::seekpos(const pos_type position, const std::ios_base::openmode which)
	{
		return seekoff(off_type(position), std::ios_base::beg, which);
	}

	SharedMemoryStream::SharedMemoryStream(std::shared_ptr<const String> content)
		: std::istream(nullptr)
		, m_buffer(std::move(content))
	{
		rdbuf(&m_buffer);
	}
}
