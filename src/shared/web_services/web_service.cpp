// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "web_service.h"
#include "web_client.h"
#include "base/constants.h"
#include "log/default_log_levels.h"

namespace mmo
{
	namespace web
	{
		WebService::WebService(
		    asio::io_service &service,
		    uint16 port
		)
			: m_ioService(service)
			, m_server(service, port, std::bind(Client::create, std::placeholders::_1, nullptr))
		{
			m_server.connected().connect([this](std::shared_ptr<Client> connection)
			{
				connection->startReceiving();
				auto client = createClient(std::move(connection));
				m_clients.push_back(std::move(client));
			});
			m_server.startAccept();
		}

		WebService::~WebService()
		{
		}

		void WebService::clientDisconnected(WebClient &client)
		{
			for (Clients::iterator i = m_clients.begin(); i != m_clients.end(); ++i)
			{
				if (i->get() == &client)
				{
					m_clients.erase(i);
					return;
				}
			}
		}

		asio::io_service &WebService::getIOService() const
		{
			return m_ioService;
		}
	}
}
