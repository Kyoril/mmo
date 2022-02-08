
#pragma once

#include "page.h"

namespace mmo
{
	class IPageVisibilityListener;

	size_t Distance(size_t first, size_t second);

	bool IsInRange(const PagePosition &center, size_t range, const PagePosition &other);

	/// @brief Calculates the visible pages based on the viewers position.
	/// 
	class PagePOVPartitioner
	{
	public:
		/// @brief Initializes the point of view partitioner.
		/// @param size Size of the page field.
		/// @param sight How many pages can be seen.
		/// @param centeredPage Initial position of the viewer.
		/// @param listener Page visibility listener.
		explicit PagePOVPartitioner(
			const PagePosition &size,
			std::size_t sight,
			PagePosition centeredPage,
			IPageVisibilityListener &listener);

		/// @brief Updates the position of the viewer.
		/// @param centeredPage New position of the viewer.
		void UpdateCenter(const PagePosition &centeredPage);

	private:
		const PagePosition m_size;
		const std::size_t m_sight;
		PagePosition m_previouslyCenteredPage;
		IPageVisibilityListener &m_listener;
	};
}
