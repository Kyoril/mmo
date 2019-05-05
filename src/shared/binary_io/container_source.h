// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "source.h"

namespace io
{
	template <class C>
	class ContainerSource : public ISource
	{
	public:

		typedef C Container;


		explicit ContainerSource(const Container &container)
			: m_container(container)
			, m_position(0)
		{
		}

		std::size_t getSizeInBytes() const
		{
			return m_container.size() * sizeof(m_container[0]);
		}

		virtual bool end() const
		{
			return (m_position == m_container.size());
		}

		virtual std::size_t read(char *dest, std::size_t size) override
		{
			const std::size_t rest = getSizeInBytes() - m_position;
			const std::size_t copyable = std::min(size, rest);
			const char *const begin = &m_container[0] + m_position;
			std::memcpy(dest, begin, copyable);
			m_position += copyable;
			return copyable;
		}

		virtual std::size_t skip(std::size_t size) override
		{
			const std::size_t rest = getSizeInBytes() - m_position;
			const std::size_t skippable = std::min(size, rest);
			m_position += skippable;
			return skippable;
		}

		virtual void seek(std::size_t pos) override
		{
			const std::size_t max = m_container.size();
			m_position = std::min(pos, max);
		}

		virtual std::size_t size() const override
		{
			return m_container.size();
		}

		virtual std::size_t position() const override
		{
			return m_position;
		}

	private:

		const Container &m_container;
		std::size_t m_position;
	};
}
