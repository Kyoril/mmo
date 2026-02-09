#include "client_runtime.h"

#include "base/macros.h"
#include "net/login_connector.h"
#include "net/realm_connector.h"

namespace mmo
{
	void ClientRuntime::Initialize()
	{
		ASSERT(!IsInitialized());

		m_networkIo = std::make_unique<asio::io_service>();
		m_networkWork = std::make_unique<asio::io_service::work>(*m_networkIo);
		m_loginConnector = std::make_shared<LoginConnector>(*m_networkIo);
		m_realmConnector = std::make_shared<RealmConnector>(*m_networkIo);
	}

	void ClientRuntime::Shutdown()
	{
		if (m_realmConnector)
		{
			m_realmConnector->resetListener();
			m_realmConnector->close();
		}

		if (m_loginConnector)
		{
			m_loginConnector->resetListener();
			m_loginConnector->close();
		}

		m_networkWork.reset();
		if (m_networkIo)
		{
			m_networkIo->stop();
			m_networkIo->reset();
		}

		m_realmConnector.reset();
		m_loginConnector.reset();
		m_networkIo.reset();
	}

	void ClientRuntime::PollNetwork()
	{
		if (!m_networkIo)
		{
			return;
		}

		m_networkIo->poll_one();
	}

	bool ClientRuntime::IsInitialized() const
	{
		return m_loginConnector != nullptr && m_realmConnector != nullptr;
	}

	LoginConnector& ClientRuntime::GetLoginConnector() const
	{
		ASSERT(m_loginConnector);
		return *m_loginConnector;
	}

	RealmConnector& ClientRuntime::GetRealmConnector() const
	{
		ASSERT(m_realmConnector);
		return *m_realmConnector;
	}
}
