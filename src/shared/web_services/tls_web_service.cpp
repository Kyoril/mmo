// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "tls_web_service.h"
#include "log/default_log_levels.h"

#include <memory>

namespace mmo
{
	namespace web
	{
		namespace
		{
			struct TLSWebClient
			{
			};
		}

		TLSWebService::TLSWebService(
		    std::unique_ptr<asio::ip::tcp::acceptor> acceptor,
		    std::unique_ptr<asio::ssl::context> ssl,
		    RequestHandler handleRequest
		)
			: m_acceptor(std::move(acceptor))
			, m_ssl(std::move(ssl))
			, m_handleRequest(std::move(handleRequest))
		{
			assert(m_acceptor);
			assert(m_ssl);
			assert(m_handleRequest);
			beginAccept();
		}

		void TLSWebService::beginAccept()
		{
			const auto connection =
			    std::make_shared<Socket>(m_acceptor->get_executor(), *m_ssl);

			auto acceptHandler = std::bind(&TLSWebService::onAccept,
			                               this,
			                               std::placeholders::_1,
			                               connection);

			m_acceptor->async_accept(connection->lowest_layer(),
			                         std::move(acceptHandler));
		}

		void TLSWebService::onAccept(asio::error_code error,
		                             std::shared_ptr<Socket> connection)
		{
			assert(connection);
			DLOG("Incoming TLS client");

			if (error)
			{
				return;
			}

			beginAccept();
		}

		std::unique_ptr<asio::ssl::context> createSSLContext(
		    const std::string &caFileName,
		    const std::string &privateKeyFileName,
		    const std::string &password)
		{
			auto context = std::make_unique<asio::ssl::context>(
			                   asio::ssl::context::sslv23);

			context->set_options(
			    asio::ssl::context::default_workarounds |
			    asio::ssl::context::no_sslv2 |
			    asio::ssl::context::single_dh_use);
			context->set_password_callback(
			    [password]
			    (std::size_t, asio::ssl::context_base::password_purpose)
			{
				return password;
			});
			context->use_certificate_chain_file(caFileName);
			context->use_private_key_file(privateKeyFileName,
			                              asio::ssl::context::pem);
			//	context->use_tmp_dh_file("dh512.pem");
			return context;
		}
	}
}
