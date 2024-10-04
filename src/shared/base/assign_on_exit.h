// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "non_copyable.h"

namespace mmo
{
	/// This class delays the assignment of a value to the moment
	/// of it's destruction.
	template<typename T>
	class AssignOnExit final : public NonCopyable
	{
	public:

		/// Initializes a new instance of this class.
		/// @param dest The destination where the value should be assigned to.
		/// @param value Value to assign.
		AssignOnExit(T &dest, const T &value)
			: m_dest(dest)
			, m_value(value)
		{
		}

		~AssignOnExit()
		{
			m_dest = m_value;
		}

	private:

		T &m_dest;
		T m_value;
	};
}
