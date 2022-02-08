
#include "page_pov_partitioner.h"
#include "page_visibility_listener.h"

namespace mmo
{
	template <class F>
	void ForEachPageInSquare(
		const PagePosition &terrainSize,
		const PagePosition &center,
		const std::size_t radius,
		const F &pageHandler
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

	size_t Distance(size_t first, size_t second)
	{
		if (first < second)
		{
			std::swap(first, second);
		}
		return (first - second);
	}

	bool IsInRange(const PagePosition &center, const size_t range, const PagePosition &other)
	{
		return
			(Distance(other[0], center[0]) <= range) &&
			(Distance(other[1], center[1]) <= range);
	}

	PagePOVPartitioner::PagePOVPartitioner(const PagePosition &size, const std::size_t sight, const PagePosition centeredPage, IPageVisibilityListener &listener)
		: m_size(size)
		, m_sight(sight)
		, m_previouslyCenteredPage(centeredPage)
		, m_listener(listener)
	{
		ForEachPageInSquare(
			m_size,
			m_previouslyCenteredPage,
			m_sight,
			[&listener](const PagePosition & page)
		{
			listener.OnPageVisibilityChanged(page, true);
		});
	}

	void PagePOVPartitioner::UpdateCenter(const PagePosition &centeredPage)
	{
		if (m_previouslyCenteredPage == centeredPage)
		{
			return;
		}

		ForEachPageInSquare(
			m_size,
			m_previouslyCenteredPage,
			m_sight,
			[this, centeredPage](const PagePosition & page)
		{
			if (!IsInRange(centeredPage, this->m_sight, page))
			{
				this->m_listener.OnPageVisibilityChanged(page, false);
			}
		});

		ForEachPageInSquare(
			m_size,
			centeredPage,
			m_sight,
			[this](const PagePosition & page)
		{
			if (!IsInRange(this->m_previouslyCenteredPage, this->m_sight, page))
			{
				this->m_listener.OnPageVisibilityChanged(page, true);
			}
		});

		m_previouslyCenteredPage = centeredPage;
	}
}
