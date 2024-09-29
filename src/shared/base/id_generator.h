// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "non_copyable.h"
#include <memory>

namespace mmo
{
	/// This class is used to generate new ids by using an internal counter.
	template<typename T>
	class IdGenerator : public NonCopyable
	{
	public:

		/// Default constructor.
		/// @param idOffset The first id to use. This is needed, if sometimes, a value of 0 indicates
		///                 an invalid value, and thus, the counter should start at 1.
		IdGenerator(T idOffset = 0)
			: m_nextId(idOffset)
			, m_initial(idOffset)
		{
		}

		IdGenerator(IdGenerator&& other) noexcept
			: m_nextId(std::move(other.m_nextId))
		{
		}

		IdGenerator& operator=(IdGenerator&& other) noexcept
		{
			if (this == &other)
			{
				return *this;
			}

			m_nextId = std::move(other.m_nextId);
		    return *this;
		}

		/// Generates a new id.
		/// @returns New id.
		T GenerateId()
		{
			return m_nextId++;
		}

		[[nodiscard]] T GetCurrentId() const noexcept { return m_nextId; }

		/// Notifies the generator about a used id. The generator will then adjust the next generated
		/// id, so that there will be no overlaps.
		void NotifyId(T id)
		{
			if (id >= m_nextId) m_nextId = id + 1;
		}

		void Reset()
		{
			m_nextId = m_initial;
		}

	private:

		T m_initial;
		T m_nextId;
	};
}
