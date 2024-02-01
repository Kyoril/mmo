
#pragma once

#include <vector>

namespace mmo
{
	/// Erases an element from a vector by swapping it with the last element
	/// and then calling pop_back for efficiency.
	template <class T>
	void EraseByMove(
		std::vector<T>& vector,
		typename std::vector<T>::iterator position
	)
	{
		if (position != vector.end())
		{
			*position = std::move(vector.back());
			vector.pop_back();
		}
	}
}
