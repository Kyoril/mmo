
#include "loaded_page_section.h"
#include "page_neighborhood.h"
#include "page_pov_partitioner.h"

#include <vector>

namespace mmo
{
	LoadedPageSection::LoadedPageSection(const PagePosition center, const std::size_t range, IPageLoaderListener &sectionListener)
		: m_center(center)
		, m_range(range)
		, m_sectionListener(sectionListener)
	{
	}

	void LoadedPageSection::UpdateCenter(const PagePosition &center)
	{
		if (m_center == center)
		{
			return;
		}

		std::vector<PageNeighborhood> becomingVisible;

		for (auto i = m_outOfSection.begin(); i != m_outOfSection.end();)
		{
			if (IsInRange(center, m_range, (*i).first->GetPosition()))
			{
				becomingVisible.push_back(i->second);
				m_sectionListener.OnPageAvailabilityChanged(i->second, true);
				i = m_outOfSection.erase(i);
			}
			else
			{
				++i;
			}
		}

		for (auto i = m_insideOfSection.begin(); i != m_insideOfSection.end();)
		{
			if (IsInRange(center, m_range, (*i).first->GetPosition()))
			{
				++i;
			}
			else
			{
				m_outOfSection.insert(std::make_pair(&i->second.GetMainPage(), i->second));
				m_sectionListener.OnPageAvailabilityChanged(i->second, false);
				i = m_insideOfSection.erase(i);
			}
		}

		for(const auto & pages : becomingVisible)
		{
			m_insideOfSection.insert(std::make_pair(&pages.GetMainPage(), pages));
		}

		m_center = center;
	}

	void LoadedPageSection::OnPageAvailabilityChanged(const PageNeighborhood &pages, const bool isAvailable)
	{
		const auto &mainPage = pages.GetMainPage();
		if (IsInRange(m_center, m_range, mainPage.GetPosition()))
		{
			SetVisibility(m_insideOfSection, pages, isAvailable);
			m_sectionListener.OnPageAvailabilityChanged(pages, isAvailable);
		}
		else
		{
			SetVisibility(m_outOfSection, pages, isAvailable);
		}
	}

	void LoadedPageSection::SetVisibility(PageMap &map, const PageNeighborhood &pages, const bool isVisible)
	{
		if (isVisible)
		{
			map.insert(std::make_pair(&pages.GetMainPage(), pages));
		}
		else
		{
			map.erase(&pages.GetMainPage());
		}
	}
}
