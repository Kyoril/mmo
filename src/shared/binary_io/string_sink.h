// Copyright (C) 2019, Robin Klimonow. All rights reserved.

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

		virtual std::size_t write(const char *src, std::size_t size) override
		{
			m_buffer.append(src, size);
			return size;
		}

		virtual std::size_t overwrite(std::size_t position, const char *src, std::size_t size) override
		{
			assert((position + size) <= m_buffer.size());

			char *const dest = &m_buffer[0] + position;
			std::memcpy(dest, src, size);
			return size;
		}

		virtual std::size_t position() override
		{
			return m_buffer.size();
		}

		virtual void flush() override
		{
		}

	private:

		String &m_buffer;
	};
}
