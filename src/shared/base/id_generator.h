// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "typedefs.h"


namespace mmo
{
	/// This class is used to generate new ids by using an internal counter.
	template<typename T>
	class IdGenerator
	{
	private:
		IdGenerator<T>(const IdGenerator<T> &Other) = delete;
		IdGenerator<T> &operator=(const IdGenerator<T> &Other) = delete;

	public:

		/// Default constructor.
		/// @param idOffset The first id to use. This is needed, if sometimes, a value of 0 indicates
		///                 an invalid value, and thus, the counter should start at 1.
		IdGenerator(T idOffset = 0)
			: m_nextId(idOffset)
		{
		}

		/// Generates a new id.
		/// @returns New id.
		T GenerateId()
		{
			return m_nextId++;
		}

		/// Notifies the generator about a used id. The generator will then adjust the next generated
		/// id, so that there will be no overlaps.
		void NotifyId(T id)
		{
			if (id >= m_nextId) m_nextId = id + 1;
		}

	private:

		T m_nextId;
	};
}
