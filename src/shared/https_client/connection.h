// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "request.h"
#include "response.h"
#include "send_request.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <memory>
#include <string>

namespace asio::ssl
{
	class context;
}

namespace mmo
{
	namespace net
	{
		namespace https_client
		{
			/// Returns the TLS client context shared by all connections that were not given
			/// their own. It trusts the system's root certificates. Loading those is costly
			/// (on Windows it walks the whole certificate store), so it happens only once.
			std::shared_ptr<asio::ssl::context> GetDefaultContext();

			/// A reusable HTTPS connection to one host.
			///
			/// Requests are sent as HTTP/1.1 with keep-alive, so that a series of requests
			/// pays for the TCP connect and the TLS handshake only once instead of once per
			/// request. The connection is opened on the first Send and kept open as long as
			/// the server allows it.
			///
			/// Not thread-safe: use one connection per thread, e.g. through a pool.
			class Connection final : public NonCopyable
			{
			public:
				/// `context` may be null to use GetDefaultContext().
				Connection(
					std::string host,
					uint16 port,
					RequestOptions options = RequestOptions(),
					std::shared_ptr<asio::ssl::context> context = nullptr);
				~Connection() override;

				/// Sends `request` and reads the whole response into memory. `request.host`
				/// defaults to the connection's host when empty.
				///
				/// If a reused connection turns out to have been closed by the server while
				/// it was idle, the request is sent once more on a fresh connection; that is
				/// normal for keep-alive and not an error. Any other failure throws as
				/// sendRequest does, and leaves the connection closed.
				Response Send(Request request);

				/// True if the connection is open and may be used for the next request.
				bool IsOpen() const;

				/// Closes the connection. The next Send reopens it.
				void Close();

				/// Number of times a connection was established, for diagnostics and tests.
				uint32 GetConnectCount() const;

			private:
				struct Impl;
				std::unique_ptr<Impl> m_impl;
			};
		}
	}
}
