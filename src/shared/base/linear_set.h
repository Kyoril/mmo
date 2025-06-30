// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file linear_set.h
 *
 * @brief Provides a simple set implementation with linear time complexity.
 *
 * This file defines the LinearSet class which implements a set using a vector
 * as the underlying container. It provides basic set operations with linear
 * time complexity for most operations, making it efficient for small sets.
 */

#pragma once

#include <vector>
#include "macros.h"

namespace mmo
{
	/**
	 * @class LinearSet
	 * @brief A simple set implementation with linear time complexity.
	 *
	 * LinearSet is a container that stores unique elements in no particular order.
	 * Unlike std::set which uses a tree structure for O(log n) operations,
	 * LinearSet uses a vector for O(n) operations, making it more efficient
	 * for small sets due to better cache locality and less overhead.
	 *
	 * @tparam T The type of elements to store in the set.
	 */
	template <class T>
	class LinearSet
	{
	public:
		/**
		 * @typedef value_type
		 * @brief The type of elements stored in the set.
		 */
		typedef T value_type;
		
		/**
		 * @typedef Elements
		 * @brief The underlying container type.
		 */
		typedef std::vector<T> Elements;
		
		/**
		 * @typedef const_iterator
		 * @brief Constant iterator type for the set.
		 */
		typedef typename Elements::const_iterator const_iterator;
		
		/**
		 * @typedef iterator
		 * @brief Iterator type for the set.
		 */
		typedef typename Elements::iterator iterator;

	public:
		/**
		 * @brief Default constructor.
		 * 
		 * Creates an empty set.
		 */
		LinearSet()
		{
		}

		/**
		 * @brief Finds an element in the set.
		 * 
		 * Searches for the specified element in the set and returns an iterator
		 * to it if found, or end() if not found.
		 * 
		 * @param element The element to find.
		 * @return An iterator to the element if found, or end() if not found.
		 * @complexity O(n)
		 */
		const_iterator find(const T &element) const
		{
			return std::find(
			           m_elements.begin(),
			           m_elements.end(),
			           element);
		}

		/**
		 * @brief Checks if an element is in the set.
		 * 
		 * @param element The element to check for.
		 * @return true if the element is in the set, false otherwise.
		 * @complexity O(n)
		 */
		bool contains(const T &element) const
		{
			// O(n)
			return find(element) != m_elements.end();
		}

		/**
		 * @brief Adds a new element to the set.
		 * 
		 * This method adds an element to the set if it doesn't already exist.
		 * In debug builds, it asserts that the element doesn't exist before adding
		 * and that it exists after adding.
		 * 
		 * @tparam U Type convertible to T
		 * @param element The element to add.
		 * @complexity Release: O(1), Debug: O(n)
		 */
		template <class U>
		typename std::enable_if<std::is_convertible<U, T>::value, void>::type
		add(U &&element)
		{
			// Release: O(1), Debug: O(n)
			optionalAdd(std::forward<U>(element));
		}

		/**
		 * @brief Adds a new element to the set if it doesn't already exist.
		 * 
		 * This method checks if the element already exists in the set before adding it.
		 * If the element already exists, it returns false. Otherwise, it adds the element
		 * and returns true.
		 * 
		 * @tparam U Type convertible to T
		 * @param element The element to add.
		 * @return true if the element was added, false if it already existed.
		 * @complexity Release: O(1), Debug: O(n)
		 */
		template <class U>
		typename std::enable_if<std::is_convertible<U, T>::value, bool>::type
		optionalAdd(U &&element)
		{
			// Release: O(1), Debug: O(n)
			if (contains(element))
			{
				return false;
			}

			m_elements.push_back(std::forward<U>(element));
			return true;
		}

		/**
		 * @brief Removes an element from the set.
		 * 
		 * This method removes the specified element from the set.
		 * 
		 * @param element The element to remove.
		 * @complexity O(n)
		 */
		void remove(const T &element)
		{
			optionalRemove(element);
		}

		/**
		 * @brief Removes an element from the set if it exists.
		 * 
		 * This method checks if the element exists in the set before removing it.
		 * If the element exists, it removes it and returns true. Otherwise, it
		 * returns false.
		 * 
		 * @param element The element to remove.
		 * @return true if the element was removed, false if it didn't exist.
		 * @complexity O(n)
		 */
		bool optionalRemove(const T &element)
		{
			for (auto i = m_elements.begin(), e = m_elements.end();
			        i != e; ++i)
			{
				if (*i == element)
				{
					*i = std::move(m_elements.back());
					m_elements.pop_back();

					ASSERT(!contains(element));
					return true;
				}
			}

			ASSERT(!contains(element));
			return false;
		}

		/**
		 * @brief Removes elements that satisfy a condition.
		 * 
		 * This method removes all elements for which the provided predicate returns true.
		 * 
		 * @tparam RemovePredicate Type of the predicate function
		 * @param condition A predicate that takes an element and returns true if it should be removed
		 * @return true if any elements were removed, false otherwise
		 * @complexity O(n) * O(condition)
		 */
		template <class RemovePredicate>
		bool optionalRemoveIf(const RemovePredicate &condition)
		{
			bool removedAny = false;

			for (auto i = m_elements.begin(), e = m_elements.end();
			        i != e; ++i)
			{
				if (condition(*i))
				{
					*i = std::move(m_elements.back());
					--e;
					m_elements.pop_back();
					removedAny = true;
				}
			}

			return removedAny;
		}

		/**
		 * @brief Inserts an element into the set if it doesn't already exist.
		 * 
		 * This method checks if the element already exists in the set before inserting it.
		 * If the element already exists, it returns an iterator to the existing element.
		 * Otherwise, it inserts the element and returns an iterator to the new element.
		 * 
		 * @tparam U Type convertible to T
		 * @param element The element to insert
		 * @return An iterator to the element in the set
		 * @complexity O(n)
		 */
		template <class U>
		const_iterator insert(U &&element)
		{
			const auto i = find(element);
			if (i != m_elements.end())
			{
				return i;
			}
			m_elements.push_back(std::forward<U>(element));
			return m_elements.end() - 1;
		}

		/**
		 * @brief Gets the underlying container.
		 * 
		 * @return A const reference to the underlying vector
		 * @complexity O(1)
		 */
		const Elements &getElements() const
		{
			return m_elements;
		}

		/**
		 * @brief Gets the number of elements in the set.
		 * 
		 * @return The number of elements
		 * @complexity O(1)
		 */
		size_t size() const
		{
			return m_elements.size();
		}

		/**
		 * @brief Checks if the set is empty.
		 * 
		 * @return true if the set is empty, false otherwise
		 * @complexity O(1)
		 */
		bool empty() const
		{
			return m_elements.empty();
		}

		/**
		 * @brief Erases a range of elements.
		 * 
		 * @param from The index of the first element to erase
		 * @param count The number of elements to erase
		 * @complexity O(n)
		 */
		void erase(size_t from, size_t count)
		{
			const auto i = m_elements.begin() + from;

			m_elements.erase(
			    i,
			    i + count
			);
		}

		/**
		 * @brief Clears the set.
		 * 
		 * Removes all elements from the set.
		 * 
		 * @complexity O(1)
		 */
		void clear()
		{
			m_elements.clear();
		}

		/**
		 * @brief Swaps the contents with another set.
		 * 
		 * @param other The set to swap with
		 * @complexity O(1)
		 */
		void swap(LinearSet &other)
		{
			m_elements.swap(other.m_elements);
		}
		
		/**
		 * @brief Returns an iterator to the beginning of the set.
		 * 
		 * @return An iterator to the first element
		 * @complexity O(1)
		 */
		iterator begin()
		{
			return m_elements.begin();
		}

		/**
		 * @brief Returns an iterator to the end of the set.
		 * 
		 * @return An iterator to the element following the last element
		 * @complexity O(1)
		 */
		iterator end()
		{
			return m_elements.end();
		}

		/**
		 * @brief Returns a const iterator to the beginning of the set.
		 * 
		 * @return A const iterator to the first element
		 * @complexity O(1)
		 */
		const_iterator begin() const
		{
			return m_elements.begin();
		}

		/**
		 * @brief Returns a const iterator to the end of the set.
		 * 
		 * @return A const iterator to the element following the last element
		 * @complexity O(1)
		 */
		const_iterator end() const
		{
			return m_elements.end();
		}

	private:
		/**
		 * @brief The underlying container that stores the elements.
		 */
		Elements m_elements;
	};

	/**
	 * @brief Swaps the contents of two LinearSet objects.
	 * 
	 * This is a free function that provides the swap functionality for LinearSet objects.
	 * It's used by the standard library algorithms that require swapping.
	 * 
	 * @tparam T The type of elements stored in the sets
	 * @param left The first set
	 * @param right The second set
	 * @complexity O(1)
	 */
	template <class T>
	void swap(LinearSet<T> &left, LinearSet<T> &right)
	{
		left.swap(right);
	}
}
