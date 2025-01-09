#pragma once

#include "paging/page_visibility_listener.h"

#include <functional>
#include <map>
#include <memory>

namespace mmo
{
	class IPageLoaderListener;

	class WorldPageLoader : public IPageVisibilityListener
	{
	public:
		typedef std::function<void()> Work;
		typedef std::function<void(Work)> DispatchWork;

	public:
		explicit WorldPageLoader(IPageLoaderListener& resultListener, DispatchWork dispatchWork, DispatchWork synchronize);

	protected:
		virtual void OnPageVisibilityChanged(const PagePosition& page, bool isVisible) override;

	protected:
		void AsyncPerformLoadOperation(const std::weak_ptr<SerializableNavPage>& page);

	protected:
		IPageLoaderListener& m_resultListener;
		std::map<PagePosition, std::shared_ptr<SerializableNavPage>> m_pages;
		const DispatchWork m_dispatchWork;
		const DispatchWork m_synchronize;
	};
}