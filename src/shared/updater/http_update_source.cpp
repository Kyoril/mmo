#include "http_update_source.h"
#include "virtual_dir/path.h"
#include "http_client/send_request.h"


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
			throw std::runtime_error(
			    path + ": HTTP response " +
			    std::to_string(response.status));
		}

		return UpdateSourceFile(
		           response.getInternalData(),
		           std::move(response.body),
		           response.bodySize
		       );
	}
}
