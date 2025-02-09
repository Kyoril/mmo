
#pragma once

#include "page.h"

namespace mmo
{
	class IPageVisibilityListener;

	size_t Distance(size_t first, size_t second);

	bool IsInRange(const PagePosition &center, size_t range, const PagePosition &other);

	template <class F>
	void ForEachPageInSquare(
		const PagePosition& terrainSize,
		const PagePosition& center,
		const std::size_t radius,
		const F& pageHandler
	)
	{
		const PagePosition begin(
			std::max(center[0], radius) - radius,
			std::max(center[1], radius) - radius
		);
		const PagePosition end(
			std::min(center[0] + radius + 1, terrainSize[0]),
			std::min(center[1] + radius + 1, terrainSize[1])
		);

		for (size_t y = begin[1]; y < end[1]; ++y)
		{
			for (size_t x = begin[0]; x < end[0]; ++x)
			{
				pageHandler(PagePosition(x, y));
			}
		}
	}

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
