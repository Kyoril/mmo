
#include "resource_manager.h"

#include <limits>

namespace mmo
{
	const String ResourceManager::AutoDetectResourceGroup = "Autodetect";
	const uint32 ResourceManager::DefaultResourceUsageCount = 3;

	ResourceManager::ResourceManager()
		: m_memoryBudget(std::numeric_limits<uint64>::max())
	{
	}

	ResourceManager::~ResourceManager()
	{
		RemoveAll();
	}

	ResourcePtr ResourceManager::CreateResource(const std::string_view name, const std::string_view group, const bool isManual,
		ManualResourceLoader* loader)
	{
		ResourcePtr result = CreateImpl(name, GetNextHandle(), group, isManual, loader);
		AddImpl(result);

		// TODO: Tell resource group manager

		return result;
	}

	ResourceManager::ResourceCreateOrRetrieveResult ResourceManager::CreateOrRetrieve(const std::string_view name,
		const std::string_view group, const bool isManual, ManualResourceLoader* loader)
	{
		std::unique_lock lock { mutex };

		bool wasCreated = false;

		ResourcePtr res = GetResourceByName(name, group);
		if (!res)
		{
			res = CreateResource(name, group, isManual, loader);
			wasCreated = true;
		}

		return { res, wasCreated };
	}

	void ResourceManager::Unload(const std::string_view name)
	{
		const ResourcePtr res = GetResourceByName(name);
		if (!res)
		{
			res->Unload();
		}
	}

	void ResourceManager::Unload(const ResourceHandle handle)
	{
		const ResourcePtr res = GetByHandle(handle);
		if (!res)
		{
			res->Unload();
		}
	}

	void ResourceManager::UnloadAll(const bool reloadableOnly)
	{
		std::unique_lock lock { mutex };

		for (const auto& [name, resource] : m_resources)
		{
			if (!reloadableOnly || resource->IsReloadable())
			{
				resource->Unload();
			}
		}
	}

	void ResourceManager::ReloadAll(const bool reloadableOnly)
	{
		std::unique_lock lock { mutex };

		for (const auto& [name, resource] : m_resources)
		{
			if (!reloadableOnly || resource->IsReloadable())
			{
				resource->Reload();
			}
		}
	}

	void ResourceManager::UnloadUnreferencedResource(bool reloadableOnly)
	{
		std::unique_lock lock { mutex };
		
		for (const auto& [name, resource] : m_resources)
		{
			if (resource.use_count() > DefaultResourceUsageCount)
			{
				continue;
			}

			if (!reloadableOnly || resource->IsReloadable())
			{
				resource->Unload();
			}
		}
	}

	void ResourceManager::ReloadUnreferencedResource(const bool reloadableOnly)
	{
		std::unique_lock lock { mutex };
		
		for (const auto& [name, resource] : m_resources)
		{
			if (resource.use_count() > DefaultResourceUsageCount)
			{
				continue;
			}

			if (!reloadableOnly || resource->IsReloadable())
			{
				resource->Reload();
			}
		}
	}

	void ResourceManager::Remove(ResourcePtr& resource)
	{
		RemoveImpl(resource);
	}

	void ResourceManager::Remove(const std::string_view name)
	{
		ResourcePtr res = GetResourceByName(name);
		if (res)
		{
			RemoveImpl(res);
		}
	}

	void ResourceManager::Remove(const ResourceHandle handle)
	{
		ResourcePtr res = GetByHandle(handle);
		if (res)
		{
			RemoveImpl(res);
		}
	}

	void ResourceManager::RemoveAll()
	{
		std::unique_lock lock { mutex };

		m_resources.clear();
		m_resourcesWithGroup.clear();
		m_resourcesByHandle.clear();

		// TODO: Notify resource group manager
	}

	void ResourceManager::RemoveUnreferencedResources(const bool reloadableOnly)
	{
		std::unique_lock lock { mutex };

		for (auto it = m_resources.begin(); it != m_resources.end();)
		{
			auto& resource = it->second;

			if (resource.use_count() == DefaultResourceUsageCount)
			{
				if (!reloadableOnly || resource->IsReloadable())
				{
					++it;

					Remove(resource->GetHandle());
				}
			}
			else
			{
				++it;
			}
		}
	}

	ResourcePtr ResourceManager::GetResourceByName(std::string_view name, std::string_view group)
	{
		std::unique_lock lock { mutex };

		const auto it = m_resources.find(String(name.begin(), name.end()));
		if (it != m_resources.end())
		{
			return it->second;
		}

		return nullptr;
	}

	ResourcePtr ResourceManager::GetByHandle(const ResourceHandle handle)
	{
		std::unique_lock lock { mutex };

		const auto it = m_resourcesByHandle.find(handle);
		if (it != m_resourcesByHandle.end())
		{
			return it->second;
		}

		return nullptr;
	}

	void ResourceManager::NotifyResourceTouched(Resource& resource)
	{
		// TODO
	}

	void ResourceManager::NotifyResourceLoaded(Resource& resource)
	{
		m_memoryUsage += resource.GetSize();
		CheckUsage();
	}

	void ResourceManager::NotifyResourceUnloaded(Resource& resource)
	{
		m_memoryUsage -= resource.GetSize();
	}

	ResourcePtr ResourceManager::Prepare(const std::string_view name, const std::string_view group, const bool isManual,
		ManualResourceLoader* loader, const bool backgroundThread)
	{
        ResourcePtr r = CreateOrRetrieve(name,group,isManual,loader).first;
        r->Prepare(backgroundThread);
        return r;
	}

	ResourcePtr ResourceManager::Load(const std::string_view name, const std::string_view group, const bool isManual,
	                                  ManualResourceLoader* loader, const bool backgroundThread)
	{
        ResourcePtr r = CreateOrRetrieve(name,group,isManual,loader).first;
        r->Load(backgroundThread);
        return r;
	}

	void ResourceManager::AddImpl(ResourcePtr& resource)
	{
		std::unique_lock lock { mutex };

		m_resources.emplace(resource->GetName(), resource);

		auto it = m_resourcesWithGroup.find(String(resource->GetGroup()));
		if (it == m_resourcesWithGroup.end())
		{
			it = m_resourcesWithGroup.emplace(resource->GetGroup(), ResourceMap{}).first;
		}

		it->second.emplace(resource->GetName(), resource);
		
		m_resourcesByHandle.emplace(resource->GetHandle(), resource);
	}

	void ResourceManager::RemoveImpl(ResourcePtr& resource)
	{
		std::unique_lock lock { mutex };
		
		m_resources.erase(String(resource->GetName()));

		const auto groupName = String(resource->GetGroup());
		const auto it = m_resourcesWithGroup.find(groupName);
		if (it != m_resourcesWithGroup.end())
		{
			it->second.erase(String(groupName));
		}

		m_resourcesByHandle.erase(resource->GetHandle());
	}

	void ResourceManager::CheckUsage()
	{
		if (GetMemoryUsage() > m_memoryBudget)
		{
			std::unique_lock lock { mutex };
			
			constexpr bool reloadableOnly = true;

			for (auto it = m_resources.begin(); it != m_resources.end() && GetMemoryUsage() > m_memoryBudget; ++it)
			{
				if (it->second.use_count() == DefaultResourceUsageCount)
				{
					if (!reloadableOnly || it->second->IsReloadable())
					{
						it->second->Unload();
					}
				}
			}
		}
	}
}
