// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file non_copyable.h
 *
 * @brief Provides a base class for creating non-copyable objects.
 *
 * This file defines the NonCopyable class which can be used as a base class
 * to prevent objects from being copied. This is useful for resources that
 * should not be duplicated, such as file handles, network connections, etc.
 */

#pragma once

namespace mmo
{
	/**
	 * @class NonCopyable
	 * @brief Base class that makes derived classes non-copyable.
	 *
	 * This class disables copy construction and copy assignment for any class
	 * that inherits from it. It's used to prevent accidental copying of objects
	 * that shouldn't be copied, such as those managing unique resources.
	 *
	 * Usage example:
	 * @code
	 * class MyResource : private NonCopyable
	 * {
	 *     // This class cannot be copied
	 * };
	 * @endcode
	 */
	struct NonCopyable
	{
		/**
		 * @brief Default constructor.
		 * 
		 * Explicitly defaulted to ensure the compiler generates the default implementation.
		 */
		NonCopyable() = default;
		
		/**
		 * @brief Virtual destructor.
		 * 
		 * Virtual to allow proper destruction of derived classes through base class pointers.
		 * Explicitly defaulted to ensure the compiler generates the default implementation.
		 */
		virtual ~NonCopyable() = default;

	private:
		/**
		 * @brief Copy constructor (deleted).
		 * 
		 * Deleted to prevent copying of NonCopyable objects.
		 * Made private to prevent derived classes from re-enabling it.
		 * 
		 * @param other The object to copy from (unused).
		 */
		NonCopyable(const NonCopyable& other) = delete;
		
		/**
		 * @brief Copy assignment operator (deleted).
		 * 
		 * Deleted to prevent copying of NonCopyable objects.
		 * Made private to prevent derived classes from re-enabling it.
		 * 
		 * @param other The object to copy from (unused).
		 * @return Reference to this object (never returns).
		 */
		NonCopyable& operator=(const NonCopyable& other) = delete;
	};
}
