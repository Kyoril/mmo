
#pragma once

#include "resource.h"

#include <vector>
#include <atomic>
#include <map>
#include <mutex>

namespace io
{
	class Reader;
}

namespace mmo
{
	/// @brief Class for managing resources of a certain type.
	class ResourceManager
	{
		static const String AutoDetectResourceGroup;

		/// @brief Marks the number of ref counts on a resource that has no external references anywhere.
		static const uint32 DefaultResourceUsageCount;

	public:
		std::recursive_mutex mutex;

	public:
		ResourceManager();

		virtual ~ResourceManager();

	public:
		virtual ResourcePtr CreateResource(std::string_view name, std::string_view group, bool isManual = false, ManualResourceLoader* loader = nullptr);
		
		typedef std::pair<ResourcePtr, bool> ResourceCreateOrRetrieveResult;
		virtual ResourceCreateOrRetrieveResult CreateOrRetrieve(std::string_view name, std::string_view group, bool isManual = false, ManualResourceLoader* loader = nullptr);

		virtual void SetMemoryBudget(const uint64 bytes) { m_memoryBudget = bytes; CheckUsage(); }

		virtual uint64 GetMemoryBudget() const { return m_memoryBudget; }

		virtual uint64 GetMemoryUsage() const { return m_memoryUsage.load(); }

		virtual void Unload(std::string_view name);

		virtual void Unload(ResourceHandle handle);

		virtual void UnloadAll(bool reloadableOnly = true);

		virtual void ReloadAll(bool reloadableOnly = true);

		virtual void UnloadUnreferencedResource(bool reloadableOnly = true);

		virtual void ReloadUnreferencedResource(bool reloadableOnly = true);

		virtual void Remove(ResourcePtr& resource);

		virtual void Remove(std::string_view name);

		virtual void Remove(ResourceHandle handle);

		virtual void RemoveAll();

		virtual void RemoveUnreferencedResources(bool reloadableOnly = true);

		virtual ResourcePtr GetResourceByName(std::string_view name, std::string_view group = AutoDetectResourceGroup);

		virtual ResourcePtr GetByHandle(ResourceHandle handle);

		virtual bool ResourceExists(const std::string_view name)
		{
			return !GetResourceByName(name);
		}

		virtual bool ResourceExists(const ResourceHandle handle)
		{
			return !GetByHandle(handle);
		}

		virtual void NotifyResourceTouched(Resource& resource);

		virtual void NotifyResourceLoaded(Resource& resource);

		virtual void NotifyResourceUnloaded(Resource& resource);

		virtual ResourcePtr Prepare(std::string_view name, std::string_view group, bool isManual = false, ManualResourceLoader* loader = nullptr, bool backgroundThread = false);

		virtual ResourcePtr Load(std::string_view name, std::string_view group, bool isManual = false, ManualResourceLoader* loader = nullptr, bool backgroundThread = false);

		virtual const std::vector<String>& GetFilePatterns() const { return m_filePatterns; }

		virtual void ParseFile(io::Reader& reader, std::string_view group) {};

		virtual float GetLoadingOrder() const { return m_loadOrder; }

		std::string_view GetResourceType() const { return m_resourceType; }

	protected:
		ResourceHandle GetNextHandle() { return m_nextHandle++; }

		virtual ResourcePtr CreateImpl(std::string_view name, ResourceHandle handle, std::string_view group, bool isManual, ManualResourceLoader* loader) = 0;

		virtual void AddImpl(ResourcePtr& resource);

		virtual void RemoveImpl(ResourcePtr& resource);

		virtual void CheckUsage();

	public:
		typedef std::map<String, ResourcePtr> ResourceMap;
		typedef std::map<String, ResourceMap> ResourceWithGroupMap;
		typedef std::map<ResourceHandle, ResourcePtr> ResourceHandleMap;
		
	protected:
		ResourceHandleMap m_resourcesByHandle;
		ResourceMap m_resources;
		ResourceWithGroupMap m_resourcesWithGroup;
		uint64 m_memoryBudget { 0 };
		std::atomic<ResourceHandle> m_nextHandle { 1 };
		std::atomic<uint64> m_memoryUsage { 0 };
		std::vector<String> m_filePatterns;
		float m_loadOrder { 0.0f };
		String m_resourceType;
	};
}
