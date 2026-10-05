// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "update_source.h"
#include "open_source_from_url.h"
#include "base/typedefs.h"

#include <memory>
#include <mutex>
#include <vector>

namespace asio::ssl
{
	class context;
}

namespace mmo::net::https_client
{
	class Connection;
}


namespace mmo::updating
{
	/// Reads update files over HTTPS.
	///
	/// Connections are kept alive and pooled, so a run over thousands of small files pays
	/// for the TCP connect and TLS handshake once per concurrent reader rather than once
	/// per file. readFile may be called from several threads at once; each call borrows
	/// its own connection from the pool.
	struct HTTPSUpdateSource : IUpdateSource
	{
		/// `context` may be null to trust the system's root certificates.
		explicit HTTPSUpdateSource(
		    std::string host,
		    uint16 port,
		    std::string path,
		    SourceOptions options = SourceOptions(),
		    std::shared_ptr<asio::ssl::context> context = nullptr
		);
		~HTTPSUpdateSource() override;

		virtual UpdateSourceFile readFile(
		    const std::string &path
		) override;

	private:

		const std::string m_host;
		const uint16 m_port;
		const std::string m_path;
		const SourceOptions m_options;
		const std::shared_ptr<asio::ssl::context> m_context;

		/// Open connections not in use by any reader right now.
		std::mutex m_poolMutex;
		std::vector<std::unique_ptr<net::https_client::Connection>> m_idleConnections;
	};
}
