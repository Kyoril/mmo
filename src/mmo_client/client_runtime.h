#pragma once

#include <memory>

#include <asio/io_service.hpp>

namespace mmo
{
	class LoginConnector;
	class RealmConnector;

	/// @brief Owns and manages the client networking runtime.
	///
	/// This class encapsulates the network io service, worker lifetime, and the
	/// login/realm connector instances used by the client.
	class ClientRuntime
	{
	public:
		ClientRuntime() = default;
		~ClientRuntime() = default;

		ClientRuntime(const ClientRuntime&) = delete;
		ClientRuntime& operator=(const ClientRuntime&) = delete;

	public:
		/// @brief Initializes the networking runtime and connector instances.
		void Initialize();

		/// @brief Shuts down the networking runtime and disconnects connectors.
		void Shutdown();

		/// @brief Processes a single pending network event (if available).
		void PollNetwork();

		/// @brief Returns whether the runtime is initialized and connectors exist.
		/// @return True when both login and realm connectors are ready.
		bool IsInitialized() const;

		/// @brief Accesses the login connector.
		/// @return Reference to the initialized login connector.
		LoginConnector& GetLoginConnector() const;

		/// @brief Accesses the realm connector.
		/// @return Reference to the initialized realm connector.
		RealmConnector& GetRealmConnector() const;

	private:
		std::unique_ptr<asio::io_service> m_networkIo;
		std::unique_ptr<asio::io_service::work> m_networkWork;
		std::shared_ptr<LoginConnector> m_loginConnector;
		std::shared_ptr<RealmConnector> m_realmConnector;
	};
}
