#pragma once

#include "macros.h"

#include <vector>

namespace mmo
{
	/// Represents a two-dimensional grid of elements. This class is used for the visibility grid, for example.
	template <class T>
	class Grid
	{
	public:

		typedef T value_type;
		typedef value_type &reference;
		typedef const value_type &const_reference;
		typedef typename std::vector<value_type> Contents;
		typedef typename Contents::size_type size_type;
		typedef typename Contents::iterator iterator;
		typedef typename Contents::const_iterator const_iterator;

	public:

		Grid()
			: m_width(0)
		{
		}

		Grid(size_type width, size_type height)
			: m_contents(width * height)
			, m_width(width)
		{
		}

		Grid(size_type width, size_type height, const value_type &value)
			: m_contents(width * height, value)
			, m_width(width)
		{
		}

		Grid(Grid &&other)
			: m_contents(std::move(other.m_contents))
			, m_width(other.m_width)
		{
		}

		Grid(const Grid &other)
			: m_contents(other.m_contents)
			, m_width(other.m_width)
		{
		}

		Grid &operator = (const Grid &other)
		{
			if (this != &other)
			{
				Grid(other).swap(*this);
			}
			return *this;
		}

		Grid &operator = (Grid &&other)
		{
			other.swap(*this);
			return *this;
		}

		void swap(Grid &other)
		{
			m_contents.swap(other.m_contents);
			std::swap(m_width, other.m_width);
		}

		bool empty() const
		{
			return (m_width == 0);
		}

		size_type width() const
		{
			return m_width;
		}

		size_type height() const
		{
			return (m_width == 0 ? 0 : (m_contents.size() / m_width));
		}

		void clear()
		{
			m_width = 0;
			m_contents.clear();
		}

		iterator begin()
		{
			return m_contents.begin();
		}

		const_iterator begin() const
		{
			return m_contents.begin();
		}

		iterator end()
		{
			return m_contents.end();
		}

		const_iterator end() const
		{
			return m_contents.end();
		}

		value_type &operator ()(size_type x, size_type y)
		{
			return get(getIndex(x, y));
		}

		const value_type &operator ()(size_type x, size_type y) const
		{
			return get(getIndex(x, y));
		}

	private:

		Contents m_contents;
		size_type m_width;


		value_type &get(size_type index)
		{
			ASSERT(index < m_contents.size());
			return m_contents[index];
		}

		const value_type &get(size_type index) const
		{
			ASSERT(index < m_contents.size());
			return m_contents[index];
		}

		size_type getIndex(size_type x, size_type y) const
		{
			ASSERT(x < m_width);
			return (x + y * width());
		}
	};


	template <class T>
	void swap(Grid<T> &left, Grid<T> &right)
	{
		left.swap(right);
	}
}
