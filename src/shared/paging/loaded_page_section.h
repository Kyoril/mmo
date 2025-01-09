
#pragma once

#include "page.h"
#include "page_loader_listener.h"
#include "page_neighborhood.h"

#include <unordered_map>

namespace mmo
{
	/// @brief 
	class LoadedPageSection : public IPageLoaderListener
	{
	public:
		explicit LoadedPageSection(
			PagePosition center,
			std::size_t range,
			IPageLoaderListener &sectionListener);

		/// @brief 
		/// @param center
		void UpdateCenter(const PagePosition &center);

	private:

		typedef std::unordered_map<SerializableNavPage *, PageNeighborhood> PageMap;

		/// @copydoc IPageLoaderListener::OnPageAvailabilityChanged
		virtual void OnPageAvailabilityChanged(const PageNeighborhood &pages, bool isAvailable) override;

		/// @brief 
		/// @param map
		/// @param pages
		/// @param isVisible
		static void SetVisibility(PageMap &map, const PageNeighborhood &pages, bool isVisible);

	private:
		PagePosition m_center;
		const std::size_t m_range;
		IPageLoaderListener &m_sectionListener;
		PageMap m_insideOfSection;
		PageMap m_outOfSection;
	};
}

