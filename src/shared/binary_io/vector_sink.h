// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "sink.h"

#include <cassert>
#include <cstring>
#include <vector>

namespace io
{
	template<typename VectorType = char>
	class VectorSink final
		: public ISink
	{
	public:
		typedef std::vector<VectorType> Buffer;


		explicit VectorSink(Buffer &buffer)
			: m_buffer(buffer)
		{
		}
		

		Buffer &buffer() { return m_buffer; }

		[[nodiscard]] const Buffer &buffer() const { return m_buffer; }

		std::size_t Write(const char *src, std::size_t size) override
		{
			m_buffer.insert(m_buffer.end(), src, src + size);
			return size;
		}

		std::size_t Overwrite(std::size_t position, const char *src, std::size_t size) override
		{
			assert((position + size) <= m_buffer.size());

			char *const dest = &m_buffer[0] + position;
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

		Buffer &m_buffer;
	};
}
