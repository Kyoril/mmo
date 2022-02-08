
#pragma once

#include "base/vector.h"

namespace mmo
{
	typedef Vector<std::size_t, 2> PagePosition;

	/// @brief Base class of a page.
	class Page
	{
	public:
		explicit Page(const PagePosition &position);

		/// @brief 
		/// @returns
		const PagePosition &GetPosition() const;

	private:

		const PagePosition m_position;
	};
}
