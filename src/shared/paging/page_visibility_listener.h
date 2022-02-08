
#pragma once

#include "page.h"

namespace mmo
{
	/// @brief Basic interface for a page visibility listener.
	class IPageVisibilityListener
	{
	public:
		virtual ~IPageVisibilityListener();

		/// @brief This method is called if the visibility of a page changed.
		/// @param page The page whose visibility changed.
		/// @param isVisible Indicates whether the page should be visible now or not.
		virtual void OnPageVisibilityChanged(const PagePosition &page, bool isVisible) = 0;
	};
}
