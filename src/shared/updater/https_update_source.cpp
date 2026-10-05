// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "https_update_source.h"
#include "virtual_dir/path.h"
#include "update_errors.h"
#include "https_client/connection.h"

#include "asio/error.hpp"
#include "asio/system_error.hpp"


namespace mmo::updating
{
	HTTPSUpdateSource::HTTPSUpdateSource(
	    std::string host,
	    uint16 port,
	    std::string path,
	    SourceOptions options,
	    std::shared_ptr<asio::ssl::context> context
	)
		: m_host(std::move(host))
		, m_port(port)
		, m_path(std::move(path))
		, m_options(options)
		, m_context(std::move(context))
	{
	}

	HTTPSUpdateSource::~HTTPSUpdateSource() = default;

	UpdateSourceFile HTTPSUpdateSource::readFile(
	    const std::string &path
	)
	{
		net::https_client::Request request;
		request.host = m_host;
		request.document = m_path;
		request.keepAlive = true;
		virtual_dir::appendPath(request.document, path);

		std::unique_ptr<net::https_client::Connection> connection;
		{
			const std::scoped_lock lock{ m_poolMutex };
			if (!m_idleConnections.empty())
			{
				connection = std::move(m_idleConnections.back());
				m_idleConnections.pop_back();
			}
		}

		if (!connection)
		{
			net::https_client::RequestOptions requestOptions;
			requestOptions.inactivityTimeout = m_options.inactivityTimeout;
			requestOptions.cancel = m_options.cancel;

			connection = std::make_unique<net::https_client::Connection>(m_host, m_port, requestOptions, m_context);
		}

		net::https_client::Response response;
		try
		{
			// A failed request leaves the connection closed; it is simply dropped below
			// rather than returned to the pool.
			response = connection->Send(request);
		}
		catch (const asio::system_error &ex)
		{
			if (ex.code() == asio::error::operation_aborted &&
			        m_options.cancel && m_options.cancel->load())
			{
				throw UpdateCancelled();
			}

			if (ex.code() == asio::error::timed_out)
			{
				throw std::runtime_error(path + ": The server stopped responding");
			}

			throw std::runtime_error(path + ": " + ex.what());
		}

		if (connection->IsOpen())
		{
			const std::scoped_lock lock{ m_poolMutex };
			m_idleConnections.push_back(std::move(connection));
		}

		if (response.status != net::https_client::Response::Ok)
		{
			const std::string message = path + ": HTTP response " + std::to_string(response.status);
			if (IsTransientHttpStatus(response.status))
			{
				throw std::runtime_error(message);
			}

			throw PermanentSourceError(message);
		}

		return UpdateSourceFile(
		           response.getInternalData(),
		           std::move(response.body),
		           response.bodySize
		       );
	}
}
