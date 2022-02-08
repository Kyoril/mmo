
#include "world_page_loader.h"

#include "paging/page_neighborhood.h"
#include "paging/page_loader_listener.h"

namespace mmo
{
	WorldPageLoader::WorldPageLoader(IPageLoaderListener& resultListener, DispatchWork dispatchWork,
		DispatchWork synchronize)
		: m_resultListener(resultListener)
		, m_dispatchWork(std::move(dispatchWork))
		, m_synchronize(std::move(synchronize))
	{
	}

	void WorldPageLoader::OnPageVisibilityChanged(const PagePosition& page, bool isVisible)
	{
		const auto existingPage = m_pages.find(page);
		if (existingPage == m_pages.end())
		{
			if (isVisible)
			{
				const auto strongPage = std::make_shared<Page>(page);
				m_pages[page] = strongPage;

				const std::weak_ptr<Page> weakPage = strongPage;
				m_dispatchWork([this, weakPage]()
				{
					this->AsyncPerformLoadOperation(weakPage);
				});
			}
		}
		else
		{
			if (!isVisible)
			{
				const PageNeighborhood pages(*existingPage->second);

				m_resultListener.OnPageAvailabilityChanged(
					pages,
					false
					);

				// Destroy page
				m_pages.erase(existingPage);
			}
		}
	}
	
	void WorldPageLoader::AsyncPerformLoadOperation(const std::weak_ptr<Page>& page)
	{
		const auto strongPage = page.lock();
		if (!strongPage)
		{
			// Request was cancelled
			return;
		}
		
		const auto notify = [this, page]()
		{
			const auto strongPage = page.lock();
			if (!strongPage)
			{
				// Request cancelled
				return;
			}

			const PageNeighborhood pages(*strongPage);
			m_resultListener.OnPageAvailabilityChanged(
				pages,
				true
				);
		};
		m_synchronize(notify);
	}
}
