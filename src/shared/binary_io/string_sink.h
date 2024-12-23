// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "sink.h"

#include <string>
#include <cassert>

namespace io
{
	class StringSink : public ISink
	{
	public:

		typedef std::string String;


		explicit StringSink(String &buffer)
			: m_buffer(buffer)
		{
		}

		String &buffer()
		{
			return m_buffer;
		}

		const String &buffer() const
		{
			return m_buffer;
		}

		std::size_t Write(const char *src, std::size_t size) override
		{
			m_buffer.append(src, size);
			return size;
		}

		std::size_t Overwrite(std::size_t position, const char *src, std::size_t size) override
		{
			assert((position + size) <= m_buffer.size());

			const auto dest = &m_buffer[0] + position;
			std::memcpy(dest, src, size);
			return size;
		}

		std::size_t Position() override
		{
			return m_buffer.size();
		}

		void Flush() override
		{
		}

	private:

		String &m_buffer;
	};
}
