// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "http_update_source.h"
#include "virtual_dir/path.h"
#include "update_errors.h"
#include "http_client/send_request.h"

#include <sstream>


namespace mmo::updating
{
	HTTPUpdateSource::HTTPUpdateSource(
	    std::string host,
	    uint16 port,
	    std::string path
	)
		: m_host(std::move(host))
		, m_port(port)
		, m_path(std::move(path))
	{
	}

	UpdateSourceFile HTTPUpdateSource::readFile(
	    const std::string &path
	)
	{
		net::http_client::Request request;
		request.host = m_host;
		request.document = m_path;
		virtual_dir::appendPath(request.document, path);

		auto response = net::http_client::sendRequest(
		                    m_host,
		                    m_port,
		                    request);

		if (response.status != net::http_client::Response::Ok)
		{
			const std::string message = path + ": HTTP response " + std::to_string(response.status);
			if (IsTransientHttpStatus(response.status))
			{
				throw std::runtime_error(message);
			}

			throw PermanentSourceError(message);
		}

		// The response body streams straight from the socket. Read it completely here so
		// that a connection dropping mid-transfer fails this call, where a retry can catch
		// it, instead of failing later while the caller is already writing the file.
		std::string body;
		{
			std::ostringstream buffer;
			if (response.body && response.body->peek() != std::char_traits<char>::eof())
			{
				buffer << response.body->rdbuf();
			}
			body = buffer.str();
		}

		if (response.bodySize && body.size() != *response.bodySize)
		{
			throw std::runtime_error(
			    path + ": Received " + std::to_string(body.size()) +
			    " of " + std::to_string(*response.bodySize) + " bytes");
		}

		const std::uintmax_t size = body.size();
		return UpdateSourceFile(
		           std::any(),
		           std::make_unique<std::istringstream>(std::move(body)),
		           size
		       );
	}
}
