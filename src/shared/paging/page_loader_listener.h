
#pragma once

namespace mmo
{
	class PageNeighborhood;


	/// @brief Basic interface for a page loader listener.
	class IPageLoaderListener
	{
	public:
		virtual ~IPageLoaderListener();

	public:
		/// @brief This method is called if the visibility of a page changed.
		/// @param page The page whose availability changed.
		/// @param isAvailable Indicates whether the page data is now available or will not.
		///			If not, the page will probably be deleted shortly after this call.
		virtual void OnPageAvailabilityChanged(const PageNeighborhood &page, bool isAvailable) = 0;
	};
}
