
#pragma once

#include <vector>

namespace mmo
{
	/// Erases an element from a vector by swapping it with the last element
	/// and then calling pop_back for efficiency.
	template <class T>
	std::vector<T>::iterator EraseByMove(
		std::vector<T>& vector,
		typename std::vector<T>::iterator position
	)
	{
		if (position == vector.end())
		{
			return vector.end(); // Return end() as there are no more elements to process
		}

		if (position + 1 == vector.end())
		{
			// If the element to remove is the last element, just pop it
			vector.pop_back();
			return vector.end(); // Return end() as there are no more elements to process
		}
		else
		{
			*position = std::move(vector.back());
			vector.pop_back();
			return position; // Return the same position, now holding the moved-in last element
		}
	}
}
