// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file assign_on_exit.h
 *
 * @brief Provides a utility for delayed assignment.
 *
 * This file defines the AssignOnExit class which allows for assigning a value
 * to a variable when the AssignOnExit object goes out of scope. This is useful
 * for ensuring cleanup or state restoration at the end of a scope.
 */

#pragma once

#include "non_copyable.h"

namespace mmo
{
	/**
	 * @class AssignOnExit
	 * @brief Delays the assignment of a value until destruction.
	 *
	 * This class stores a reference to a variable and a value, and assigns
	 * the value to the variable when the AssignOnExit object is destroyed.
	 * This is useful for ensuring that a variable is set to a specific value
	 * when exiting a scope, regardless of how the scope is exited (normal flow,
	 * exception, early return, etc.).
	 *
	 * @tparam T The type of the variable and value.
	 */
	template<typename T>
	class AssignOnExit final : public NonCopyable
	{
	public:
		/**
		 * @brief Constructor.
		 * 
		 * Stores references to the destination variable and the value to be assigned.
		 * The assignment will occur when this object is destroyed.
		 * 
		 * @param dest Reference to the destination variable.
		 * @param value The value to assign to the destination when this object is destroyed.
		 */
		AssignOnExit(T &dest, const T &value)
			: m_dest(dest)
			, m_value(value)
		{
		}

		/**
		 * @brief Destructor.
		 * 
		 * Assigns the stored value to the destination variable.
		 * This ensures the assignment happens when the object goes out of scope,
		 * regardless of how the scope is exited.
		 */
		~AssignOnExit()
		{
			m_dest = m_value;
		}

	private:
		/**
		 * @brief Reference to the destination variable.
		 */
		T &m_dest;
		
		/**
		 * @brief The value to assign to the destination.
		 */
		T m_value;
	};
}
