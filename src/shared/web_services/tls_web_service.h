// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "asio/ip/tcp.hpp"
#include "asio/ssl/stream.hpp"
#include "http/http_incoming_request.h"
#include "http/http_outgoing_answer.h"

namespace mmo
{
	namespace web
	{
		class TLSWebService
		{
		public:

			typedef asio::ssl::stream<asio::ip::tcp::socket> Socket;
			typedef std::function<void(net::http::IncomingRequest &,
			                           net::http::OutgoingAnswer &)> RequestHandler;


			explicit TLSWebService(
			    std::unique_ptr<asio::ip::tcp::acceptor> acceptor,
			    std::unique_ptr<asio::ssl::context> ssl,
			    RequestHandler handleRequest
			);

		private:

			const std::unique_ptr<asio::ip::tcp::acceptor> m_acceptor;
			const std::unique_ptr<asio::ssl::context> m_ssl;
			const RequestHandler m_handleRequest;


			void beginAccept();
			void onAccept(asio::error_code error,
			              std::shared_ptr<Socket> connection);
		};


		std::unique_ptr<asio::ssl::context> createSSLContext(
		    const std::string &caFileName,
		    const std::string &privateKeyFileName,
		    const std::string &password);
	}
}
