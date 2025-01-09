
#pragma once

#include "page.h"

#include <array>


namespace mmo
{
	/// @brief 
	class PageNeighborhood
	{
	public:
		explicit PageNeighborhood(SerializableNavPage &mainPage);

		/// @brief 
		/// @param position
		/// @param page 
		void SetPageByRelativePosition(const PagePosition &position, SerializableNavPage *page);

		/// @brief 
		/// @param position 
		/// @returns 
		SerializableNavPage *GetPageByRelativePosition(const PagePosition &position) const;

		/// @brief Gets a reference to the main page.
		/// @returns Reference to the main page.
		SerializableNavPage &GetMainPage() const;

	private:

		/// @brief Calculates the page index based on the page coordinates.
		/// @param position The page coordinates.
		/// @returns Index of the page based on the page coordinates.
		static std::size_t ToIndex(const PagePosition &position);

	private:
		std::array<SerializableNavPage *, 4> m_pages{};
	};
}
