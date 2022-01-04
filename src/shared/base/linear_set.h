#pragma once

#include <vector>

namespace mmo
{
	/// Efficient implementation of a set container.
	template <class T>
	class LinearSet
	{
	public:

		typedef T value_type;
		typedef std::vector<T> Elements;
		typedef typename Elements::const_iterator const_iterator;

	public:

		/// Default constructor.
		LinearSet()
		{
		}

		const_iterator find(const T &element) const
		{
			return std::find(
			           m_elements.begin(),
			           m_elements.end(),
			           element);
		}

		/// Determines whether an element is contained. Linear execution time.
		/// @param element The searched element.
		/// @returns true if the element is contained within the set, false otherwise.
		bool contains(const T &element) const
		{
			// O(n)
			return find(element) != m_elements.end();
		}

		/// Adds a new element to the set. Debug assertions included.
		/// @param element The element to add.
		template <class U>
		typename std::enable_if<std::is_convertible<U, T>::value, void>::type
		add(U &&element)
		{
			// Release: O(1), Debug: O(n)
			ASSERT(!contains(element));
			optionalAdd(std::forward<U>(element));
			ASSERT(contains(element));
		}

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

		//O(n)
		void remove(const T &element)
		{
			ASSERT(contains(element));
			optionalRemove(element);
			ASSERT(!contains(element));
		}

		//O(n)
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

		//O(n) * O(condition)
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

		const Elements &getElements() const
		{
			return m_elements;
		}

		size_t size() const
		{
			return m_elements.size();
		}

		bool empty() const
		{
			return m_elements.empty();
		}

		void erase(size_t from, size_t count)
		{
			const auto i = m_elements.begin() + from;

			m_elements.erase(
			    i,
			    i + count
			);
		}

		void clear()
		{
			m_elements.clear();
		}

		void swap(LinearSet &other)
		{
			m_elements.swap(other.m_elements);
		}

		const_iterator begin() const
		{
			return m_elements.begin();
		}

		const_iterator end() const
		{
			return m_elements.end();
		}

	private:

		Elements m_elements;
	};


	template <class T>
	void swap(LinearSet<T> &left, LinearSet<T> &right)
	{
		left.swap(right);
	}
}
