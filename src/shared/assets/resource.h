
#pragma once

#include "base/signal.h"
#include "base/typedefs.h"

#include <atomic>
#include <mutex>

namespace mmo
{
	class ResourceManager;
	class ManualResourceLoader;

	using ResourceHandle = uint64;

	/// @brief Abstract base class of a loadable resource, like textures, meshes or materials.
	class Resource : public std::enable_shared_from_this<Resource>
	{
	public:
		std::recursive_mutex mutex;

	public:
		signal<void(Resource*)> loadingComplete;
		signal<void(Resource*)> preparingComplete;
		signal<void(Resource*)> unloadingComplete;

		/// @brief Enumerates possible loading states of the resource.
		enum class LoadingState : uint8
		{
			/// @brief Resource is not loaded.
			Unloaded,

			/// @brief Loading of the resource is in progress.
			Loading,

			/// @brief The resource is fully loaded.
			Loaded,

			/// @brief The resource is currently unloading.
			Unloading,

			/// @brief The resource is fully prepared and ready to be used.
			Prepared,

			/// @brief The resource is currently preparing.
			Preparing
		};

	protected:
		ResourceManager* m_creator { nullptr };
		String m_name;
		String m_group;
		ResourceHandle m_handle { 0 };
		std::atomic<LoadingState> m_loadingState { LoadingState::Unloaded };
		volatile bool m_backgroundLoaded { false };
		uint64 m_size { 0 };
		bool m_manual { false };
		String m_origin;
		ManualResourceLoader* m_loader { nullptr };
		uint64 m_stateCount { 0 };

	protected:
		/// @brief Protected default constructor. Public constructors are required to
		///	       provide certain dependencies.
		Resource() = default;

	protected:
		/// @brief Internal hook to perform actions before the load process but after the resource
		///	       has been marked as loading.
		virtual void PreLoadImpl() {}

		/// @brief Internal hook to perform actions after the load process but before the resource
		///	       has been marked as fully loaded.
		virtual void PostLoadImpl() {}

		/// @brief Internal hook to perform actions before the unload process.
		virtual void PreUnloadImpl() {}

		/// @brief Internal hook to perform actions after the unload process, but before the resource
		///	       has been marked as fully unloaded.
		virtual void PostUnloadImpl() {}

		/// @brief Internal implementation to perform when the resource should be prepared.
		virtual void PrepareImpl() {}

		/// @brief Internal implementation to perform when the resource should be unprepared.
		virtual void UnprepareImpl() {}

		/// @brief Internal implementation of the load action. Has to be implemented.
		virtual void LoadImpl() = 0;

		/// @brief Internal implementation of the unload action. Has to be implemented.
		virtual void UnloadImpl() = 0;

	public:
		/// @brief Standard constructor.
		/// @param creator The resource manager that is creating the resource.
		/// @param name The unique name of the resource inside of it's manager.
		/// @param handle The resource handle. 
		/// @param group Name of the resource group.
		/// @param isManual Whether this resource is manually loaded using a manual loader instance.
		/// @param loader The manual loader instance to use when loading this resource manually.
		Resource(ResourceManager* creator, std::string_view name, ResourceHandle handle, std::string_view group, bool isManual = false, ManualResourceLoader* loader = nullptr);

		/// @brief Virtual default destructor because of inheritance.
		virtual ~Resource() = default;

	public:
		/// @brief Prepares the resource for load, if it isn't already. Note that you don't have to call
		///	       Prepare explicitly before loading, as Load will call Prepare if required.
		/// @param backgroundThread 
		virtual void Prepare(bool backgroundThread = false);

		/// @brief Loads the resource if it isn't already loaded.
		/// @param backgroundThread true if the loading thread is a special background loading thread.
		virtual void Load(bool backgroundThread = false);

		/// @brief Reloads the resource if possible. If it is currently loaded, the resource is first
		///	       unloaded before it is being loaded again.
		virtual void Reload();

		/// @brief Determines whether the resource can be loaded again after it has been unloaded.
		/// @return true if the resource can be reloaded.
		virtual bool IsReloadable() const noexcept { return !m_manual || m_loader; }

		/// @brief Gets whether the resource is manually loaded.
		/// @return true if the resource is loaded manually.
		virtual bool IsManuallyLoaded() const noexcept { return m_manual; }

		/// @brief Unloads the resource. The resource can be reloaded later if required.
		virtual void Unload();

		/// @brief Gets the size of the resource in bytes.
		/// @return Size of the resource in bytes.
		virtual uint64 GetSize() const noexcept { return m_size; }

		/// @brief Touches the resource to indicate it has been used.
		virtual void Touch();

		/// @brief Gets the unique name of the resource.
		/// @return The unique name of the resource.
		virtual std::string_view GetName() const noexcept { return m_name; }

		/// @brief Gets the resource handle.
		/// @return The resource handle.
		virtual ResourceHandle GetHandle() const noexcept { return m_handle; }

		/// @brief Gets whether the resource is currently in the prepared state.
		/// @return true if the resource is currently in the prepared state.
		virtual bool IsPrepared() const noexcept { return m_loadingState.load() == LoadingState::Prepared; }

		/// @brief Gets whether the resource is currently in the loaded state.
		/// @return true if the resource is currently in the loaded state.
		virtual bool IsLoaded() const noexcept { return m_loadingState.load() == LoadingState::Loaded; }

		/// @brief Gets whether the resource is currently in the loading state.
		/// @return true if the resource is currently in the loading state.
		virtual bool IsLoading() const noexcept { return m_loadingState.load() == LoadingState::Loading; }

		/// @brief Gets the current loading state of the resource.
		/// @return The current loading state of the resource.
		virtual LoadingState GetLoadingState() const noexcept { return m_loadingState.load(); }

		/// @brief Gets whether this resource is using background loading.
		/// @return true if this resource is using background loading.
		virtual bool IsBackgroundLoaded() const noexcept { return m_backgroundLoaded; }

		/// @brief Sets whether this resource is using background loading.
		/// @param backgroundLoading true to enable background loading.
		virtual void SetBackgroundLoading(const bool backgroundLoading) noexcept { m_backgroundLoaded = backgroundLoading; }

		/// @brief Escalate the loading of a background loaded resource, locking the calling thread
		///	       until the resource is loaded.
		virtual void EscalateLoading();

		/// @brief Gets the group name of this resource.
		/// @return The group name of this resource.
		virtual std::string_view GetGroup() const noexcept { return m_group; }

		/// @brief Changes the owning group of the resource.
		/// @param newGroup Name of the new resource group to own the resource.
		virtual void ChangeGroupOwnership(std::string_view newGroup);

		/// @brief Gets the creator of this resource, if any.
		/// @return Pointer to the creator of this resource or nullptr if not available.
		virtual ResourceManager* GetCreator() const noexcept { return m_creator; }

		/// @brief Gets an optional string which indicates the origin of this resource, if available.
		///	       This can be things like a file name.
		/// @return String indicating the origin of the resource.
		virtual std::string_view GetOrigin() const noexcept { return m_origin; }

		/// @brief Tells the resource where it originated from.
		/// @param origin The origin of the resource.
		virtual void NotifyOrigin(std::string origin) { m_origin = std::move(origin); }

		/// @brief Gets the number of loading state changes of the resource.
		/// @return The number of loading state changes of the resource.
		virtual uint64 GetStateCount() const noexcept { return m_stateCount; }

		/// @brief Manually marks the state of this resource as having been changed.
		///	       This will increase the number returned by GetStateCount.
		virtual void InvalidateState() noexcept { m_stateCount++; }

		/// @brief Fires the loadingComplete signal.
		/// @param wasBackgroundLoaded Flag indicating whether the resource was loaded by a background loading thread.
		virtual void FireLoadingComplete(bool wasBackgroundLoaded);
		
		/// @brief Fires the preparingComplete signal.
		/// @param wasBackgroundLoaded Flag indicating whether the resource was loaded by a background loading thread.
		virtual void FirePreparingComplete(bool wasBackgroundLoaded);

		/// @brief Fires the unloadingComplete signal.
		virtual void FireUnloadingComplete();

		/// @brief Calculates the size of a resource. This will be called after the resource has been loaded.
		/// @return The size of the resource.
		virtual uint64 CalculateSize() const;
	};

	/// @brief Typedef for a shared resource pointer.
	typedef std::shared_ptr<Resource> ResourcePtr;

	/// @brief Interface describing a manual resource loader.
	class ManualResourceLoader
	{
	public:
		ManualResourceLoader() = default;
		virtual ~ManualResourceLoader() = default;

	public:
		virtual void PrepareResource(Resource& resource) {}

		virtual void LoadResource(Resource& resource) = 0;
	};
}
