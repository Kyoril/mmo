
#include "page_neighborhood.h"

#include "base/macros.h"


namespace mmo
{
	PageNeighborhood::PageNeighborhood(SerializableNavPage &mainPage)
	{
		m_pages.fill(nullptr);
		SetPageByRelativePosition(PagePosition(0, 0), &mainPage);
	}

	void PageNeighborhood::SetPageByRelativePosition(const PagePosition &position, SerializableNavPage *page)
	{
		const std::size_t index = ToIndex(position);
		ASSERT(index < m_pages.size() && "Index out of range");

		m_pages[ToIndex(position)] = page;
	}

	SerializableNavPage * PageNeighborhood::GetPageByRelativePosition(const PagePosition &position) const
	{
		return m_pages[ToIndex(position)];
	}

	SerializableNavPage & PageNeighborhood::GetMainPage() const
	{
		return *GetPageByRelativePosition(PagePosition(0, 0));
	}

	std::size_t PageNeighborhood::ToIndex(const PagePosition &position)
	{
		return (position[1] * 2 + position[0]);
	}
}
