// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "https_update_source.h"
#include "virtual_dir/path.h"
#include "update_errors.h"
#include "https_client/send_request.h"

#include "asio/error.hpp"
#include "asio/system_error.hpp"


namespace mmo::updating
{
	HTTPSUpdateSource::HTTPSUpdateSource(
	    std::string host,
	    uint16 port,
	    std::string path,
	    SourceOptions options
	)
		: m_host(std::move(host))
		, m_port(port)
		, m_path(std::move(path))
		, m_options(options)
	{
	}

	UpdateSourceFile HTTPSUpdateSource::readFile(
	    const std::string &path
	)
	{
		net::https_client::Request request;
		request.host = m_host;
		request.document = m_path;
		virtual_dir::appendPath(request.document, path);

		net::https_client::RequestOptions requestOptions;
		requestOptions.inactivityTimeout = m_options.inactivityTimeout;
		requestOptions.cancel = m_options.cancel;

		net::https_client::Response response;
		try
		{
			response = net::https_client::sendRequest(
			               m_host,
			               m_port,
			               request,
			               requestOptions);
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
