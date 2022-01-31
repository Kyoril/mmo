
#include "resource.h"
#include "resource_manager.h"

#include <mutex>

#include "log/default_log_levels.h"

namespace mmo
{
	Resource::Resource(ResourceManager* creator, const std::string_view name, const ResourceHandle handle, const std::string_view group,
	                   const bool isManual, ManualResourceLoader* loader)
		: m_creator(creator)
		, m_name(name)
		, m_group(group)
		, m_handle(handle)
		, m_manual(isManual)
		, m_loader(loader)
	{
	}

	void Resource::Prepare(bool backgroundThread)
	{
		const auto oldState = GetLoadingState();

		if (oldState != LoadingState::Unloaded && oldState != LoadingState::Preparing) 
		{
			return;
		}

		auto expected = LoadingState::Unloaded;
		if (!m_loadingState.compare_exchange_strong(expected, LoadingState::Preparing))
		{
			while(GetLoadingState() == LoadingState::Preparing)
			{
				std::unique_lock lock { mutex };
			}

			const auto state = GetLoadingState();
			if (state != LoadingState::Prepared && state != LoadingState::Loading && state != LoadingState::Loaded)
			{
				ELOG("Another thread failed in resource operation");
			}

			return;
		}
		
		{
			std::unique_lock lock { mutex };
			if (m_manual)
			{
				if (m_loader)
				{
					m_loader->PrepareResource(*this);
				}
				else
				{
					WLOG(m_creator->GetResourceType() << " instance '" << m_name << "' was defined as manually loaded, but no manual loader was provided. This resource will be lost if it has to be reloaded.");
				}
			}
			else
			{
				PrepareImpl();
			}
		}

		m_loadingState = LoadingState::Prepared;

		if (!backgroundThread)
		{
			FirePreparingComplete(false);
		}
	}

	void Resource::Load(const bool backgroundThread)
	{
		if (m_backgroundLoaded && !backgroundThread)
		{
			return;
		}

		bool keepChecking = true;
		auto oldState = LoadingState::Unloaded;

		while(keepChecking)
		{
			oldState = GetLoadingState();

			if (oldState == LoadingState::Preparing)
			{
				while(GetLoadingState() == LoadingState::Preparing)
				{
					std::unique_lock lock { mutex };
				}

				oldState = GetLoadingState();
			}

			if (oldState != LoadingState::Unloaded && oldState != LoadingState::Prepared && oldState != LoadingState::Loading)
			{
				return;
			}

			if (oldState == LoadingState::Loading || !m_loadingState.compare_exchange_strong(oldState, LoadingState::Loading))
			{
				while(GetLoadingState() == LoadingState::Loading)
				{
					std::unique_lock lock { mutex };
				}

				auto state = GetLoadingState();
				if (state == LoadingState::Prepared || state == LoadingState::Preparing)
				{
					// Another thread is preparing, loop around
					continue;
				}

				if (state != LoadingState::Loaded)
				{
					ELOG("Another thread failed in resource operation");
				}

				return;
			}

			keepChecking = false;
		}

		{
			std::unique_lock lock { mutex };

			if (m_manual)
			{
				PreLoadImpl();

				if (m_loader)
				{
					m_loader->LoadResource(*this);
				}
				else
				{
					WLOG(m_creator->GetResourceType() << " instance '" << m_name << "' was defined as manually loaded, but no manual loader was provided. This resource will be lost if it has to be reloaded.");
				}

				PostLoadImpl();
			}
			else
			{
				if (oldState == LoadingState::Unloaded)
				{
					PrepareImpl();
				}

				PreLoadImpl();

				LoadImpl();

				PostLoadImpl();
			}
		}

		m_loadingState = LoadingState::Loaded;
		InvalidateState();

		if (m_creator)
		{
			m_creator->NotifyResourceLoaded(*this);
		}

		if (!backgroundThread)
		{
			FireLoadingComplete(false);
		}
	}

	void Resource::Reload()
	{
		std::unique_lock lock { mutex };

		if (GetLoadingState() == LoadingState::Loaded)
		{
			Unload();
			Load();
		}
	}

	void Resource::Unload()
	{
		auto oldState = GetLoadingState();
		if (oldState != LoadingState::Loaded && oldState != LoadingState::Prepared)
		{
			return;
		}

		if (!m_loadingState.compare_exchange_strong(oldState, LoadingState::Unloading))
		{
			return;
		}

		{
			std::unique_lock lock { mutex };
			if (oldState == LoadingState::Prepared)
			{
				UnprepareImpl();
			}
			else
			{
				PreUnloadImpl();
				UnloadImpl();
				PostUnloadImpl();
			}
		}

		m_loadingState = LoadingState::Unloaded;

		if (oldState == LoadingState::Loaded && m_creator)
		{
			m_creator->NotifyResourceUnloaded(*this);
		}

		FireUnloadingComplete();
	}

	void Resource::Touch()
	{
		Load();

		if (m_creator)
		{
			m_creator->NotifyResourceTouched(*this);
		}
	}

	void Resource::EscalateLoading()
	{
		Load(true);
		FireLoadingComplete(true);
	}

	void Resource::ChangeGroupOwnership(const std::string_view newGroup)
	{
		if (m_group != newGroup)
		{
			auto oldGroup = m_group;
			m_group = newGroup;

			// TODO: Notify resource group manager
		}
	}

	void Resource::FireLoadingComplete(bool wasBackgroundLoaded)
	{
		std::unique_lock lock { mutex };
		loadingComplete(this);
	}

	void Resource::FirePreparingComplete(bool wasBackgroundLoaded)
	{
		std::unique_lock lock { mutex };
		preparingComplete(this);
	}

	void Resource::FireUnloadingComplete()
	{
		std::unique_lock lock { mutex };
		unloadingComplete(this);
	}

	uint64 Resource::CalculateSize() const
	{
		uint64 memSize = 0;

		memSize += sizeof *this;

		return memSize;
	}
}
