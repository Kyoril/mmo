#pragma once

#include "base/non_copyable.h"

namespace mmo
{
	struct ClientContext;
	class LoginConnector;
	class RealmConnector;

	/// @brief Orchestrates client startup and shutdown lifecycle.
	///
	/// This class encapsulates the previous global init/destroy flow and keeps
	/// application bootstrap logic separate from platform entry points.
	class ClientApplication final : public NonCopyable
	{
	public:
		ClientApplication() = default;
		~ClientApplication() = default;

	public:
		/// @brief Starts all required client systems.
		/// @return True if startup completed successfully.
		bool Start();

		/// @brief Stops and tears down all previously started client systems.
		void Stop();

		/// @brief Returns whether the application is currently started.
		/// @return True if Start succeeded and Stop has not been called.
		bool IsStarted() const { return m_started; }

	private:
		/// @brief Initializes core services and foundational runtime objects.
		/// @param context Shared client context.
		/// @return True on success.
		bool InitializeCore(ClientContext& context);

		/// @brief Initializes network/audio runtime and validates connectors.
		/// @param context Shared client context.
		/// @param outLoginConnector Receives the initialized login connector reference.
		/// @param outRealmConnector Receives the initialized realm connector reference.
		/// @return True on success.
		bool InitializeRuntimeServices(ClientContext& context, LoginConnector*& outLoginConnector, RealmConnector*& outRealmConnector);

		/// @brief Loads project data and client cache.
		/// @param context Shared client context.
		/// @param realmConnector Initialized realm connector.
		/// @return True on success.
		bool LoadProjectAndCache(ClientContext& context, RealmConnector& realmConnector);

		/// @brief Initializes gameplay-oriented client subsystems.
		/// @param context Shared client context.
		/// @param realmConnector Initialized realm connector.
		void InitializeGameplaySystems(ClientContext& context, RealmConnector& realmConnector);

		/// @brief Registers game states and script layer.
		/// @param context Shared client context.
		/// @param loginConnector Initialized login connector.
		/// @param realmConnector Initialized realm connector.
		void InitializeStatesAndScripts(ClientContext& context, LoginConnector& loginConnector, RealmConnector& realmConnector);

		/// @brief Initializes frame-ui runtime and enters login state.
		/// @param context Shared client context.
		/// @return True on success.
		bool InitializeUiAndEnterState(ClientContext& context);

		/// @brief Shuts down gameplay and network systems gracefully.
		/// @param context Shared client context.
		void ShutdownSystems(ClientContext& context);

		/// @brief Shuts down gameplay client systems and releases related instances.
		/// @param context Shared client context.
		void ShutdownGameplaySystems(ClientContext& context);

		/// @brief Shuts down UI runtime and releases UI-facing objects.
		/// @param context Shared client context.
		void ShutdownUiSystems(ClientContext& context);

		/// @brief Shuts down network/runtime services and persists cache state.
		/// @param context Shared client context.
		void ShutdownRuntimeServices(ClientContext& context);

		/// @brief Shuts down core engine services and logging/timer facilities.
		/// @param context Shared client context.
		void ShutdownCoreServices(ClientContext& context);

		/// @brief Releases context-owned pointers and core services.
		/// @param context Shared client context.
		void ResetContext(ClientContext& context);

		bool m_started = false;
	};
}
