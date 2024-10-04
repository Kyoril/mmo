// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "source.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cassert>

namespace io
{
	class MemorySource : public ISource
	{
	public:

		MemorySource()
			: m_begin(nullptr)
			, m_end(nullptr)
			, m_pos(nullptr)
		{
		}

		MemorySource(const char *begin, const char *end)
			: m_begin(begin)
			, m_end(end)
			, m_pos(begin)
		{
			assert(begin <= end);
		}

		explicit MemorySource(const std::string &buffer)
			: m_begin(&buffer[0])
		{
			m_pos = m_begin;
			m_end = m_begin + buffer.size();
		}

		explicit MemorySource(const std::vector<char> &buffer)
			: m_begin(&buffer[0])
		{
			m_pos = m_begin;
			m_end = m_begin + buffer.size();
		}

		void rewind()
		{
			m_pos = m_begin;
		}

		std::size_t getSize() const
		{
			return static_cast<std::size_t>(m_end - m_begin);
		}

		std::size_t getRest() const
		{
			return static_cast<std::size_t>(m_end - m_pos);
		}

		std::size_t getRead() const
		{
			return static_cast<std::size_t>(m_pos - m_begin);
		}

		const char *getBegin() const
		{
			return m_begin;
		}

		const char *getEnd() const
		{
			return m_end;
		}

		const char *getPosition() const
		{
			return m_pos;
		}

		virtual bool end() const override
		{
			return (m_pos == m_end);
		}

		virtual std::size_t read(char *dest, std::size_t size) override
		{
			const std::size_t rest = getRest();
			const std::size_t copyable = std::min<std::size_t>(rest, size);
			std::memcpy(dest, m_pos, copyable);
			m_pos += copyable;
			return copyable;
		}

		virtual std::size_t skip(std::size_t size) override
		{
			const std::size_t rest = getRest();
			const std::size_t skippable = std::min<std::size_t>(rest, size);
			m_pos += skippable;
			return skippable;
		}

		virtual std::size_t size() const override
		{
			return static_cast<std::size_t>(m_end - m_begin);
		}

		virtual void seek(std::size_t pos) override
		{
			const std::size_t max = size();
			m_pos = m_begin + std::min<std::size_t>(pos, max);
		}

		virtual std::size_t position() const override
		{
			return static_cast<size_t>(m_pos - m_begin);
		}

	private:

		const char *m_begin, *m_end, *m_pos;
	};
}
