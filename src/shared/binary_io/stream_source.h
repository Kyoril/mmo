// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "source.h"

#include <istream>

namespace io
{
	class StreamSource
		: public ISource
	{
	public:

		explicit StreamSource(std::istream &stream)
			: m_stream(stream)
		{
			const std::streampos old = m_stream.tellg();
			m_stream.seekg(static_cast<std::streamoff>(0), std::ios::end);
			m_streamSize = static_cast<size_t>(m_stream.tellg());
			m_stream.seekg(old, std::ios::beg);
		}

		[[nodiscard]] bool end() const override
		{
			return m_stream.eof();
		}

		std::size_t read(char *dest, std::size_t size) override
		{
			m_stream.read(dest, static_cast<std::streamsize>(size));
			return static_cast<std::size_t>(m_stream.gcount());
		}

		std::size_t skip(std::size_t size) override
		{
			const auto old = m_stream.tellg();
			m_stream.seekg(
			    old + static_cast<std::streamoff>(size),
			    std::ios::beg);
			const std::streampos new_ = m_stream.tellg();
			return static_cast<std::size_t>(new_ - old);
		}

		void seek(std::size_t pos) override
		{
			m_stream.seekg(
			    pos,
			    std::ios::beg);
		}

		std::size_t size() const override
		{
			return m_streamSize;
		}

		std::size_t position() const override
		{
			return static_cast<std::size_t>(m_stream.tellg());
		}

	private:

		std::istream &m_stream;
		std::size_t m_streamSize;
	};
}
