// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "https_update_source.h"
#include "virtual_dir/path.h"
#include "update_errors.h"
#include "https_client/connection.h"

#include "asio/error.hpp"
#include "asio/system_error.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <optional>
#include <thread>
#include <unordered_map>


namespace mmo::updating
{
	/// Downloads announced files on background connections with HTTP pipelining.
	///
	/// Files move through Queued -> InFlight -> Done (or Failed). readFile takes Done
	/// files, waits for InFlight ones, and claims Queued ones for itself, which is what
	/// rules out a deadlock: whatever a reader waits for is already being downloaded,
	/// and the buffer limit only ever holds back files nobody is waiting for yet.
	class HTTPSUpdateSource::Prefetcher final
	{
	public:
		Prefetcher(HTTPSUpdateSource &source)
			: m_source(source)
			, m_depth(std::max<uint32>(1, source.m_options.pipelineDepth))
		{
		}

		~Prefetcher()
		{
			{
				const std::scoped_lock lock{ m_mutex };
				m_stop = true;
			}

			m_fetchersCondition.notify_all();

			for (std::thread &thread : m_threads)
			{
				thread.join();
			}
		}

		void Add(const std::vector<RemoteFile> &files)
		{
			{
				const std::scoped_lock lock{ m_mutex };

				for (const RemoteFile &file : files)
				{
					if (file.size > m_source.m_options.prefetchMaxFileSize ||
					        m_entries.count(file.path))
					{
						continue;
					}

					Entry &entry = m_entries[file.path];
					entry.size = file.size;
					m_queue.push_back(file.path);
				}

				const size_t wanted = std::min<size_t>(m_source.m_options.prefetchConnections, m_queue.size());
				while (m_threads.size() < wanted)
				{
					m_threads.emplace_back([this] { Run(); });
				}
			}

			m_fetchersCondition.notify_all();
		}

		/// Returns the prefetched response for `path`, waiting for it if it is being
		/// downloaded right now. Returns nothing if the caller should request the file
		/// itself.
		std::optional<net::https_client::Response> Take(const std::string &path)
		{
			std::unique_lock lock{ m_mutex };

			for (;;)
			{
				const auto it = m_entries.find(path);
				if (it == m_entries.end())
				{
					return std::nullopt;
				}

				Entry &entry = it->second;
				switch (entry.state)
				{
				case State::Queued:
					// Not started yet: fetching it directly is quicker than waiting for a
					// prefetch connection to get to it.
					m_entries.erase(it);
					return std::nullopt;

				case State::InFlight:
					if (m_source.m_options.cancel && m_source.m_options.cancel->load())
					{
						throw UpdateCancelled();
					}

					m_readersCondition.wait_for(lock, std::chrono::milliseconds(50));
					continue;

				case State::Done:
				{
					std::optional<net::https_client::Response> response(std::move(entry.response));
					m_buffered -= entry.size;
					m_entries.erase(it);
					m_fetchersCondition.notify_all();
					return response;
				}

				case State::Failed:
					// Requested directly once more, which goes through the usual retries and
					// reports the actual error if the file really cannot be had.
					m_entries.erase(it);
					return std::nullopt;
				}
			}
		}

	private:
		enum class State
		{
			Queued,
			InFlight,
			Done,
			Failed
		};

		struct Entry
		{
			State state = State::Queued;
			std::uintmax_t size = 0;
			net::https_client::Response response;
		};

		bool IsStopping() const
		{
			return m_stop || (m_source.m_options.cancel && m_source.m_options.cancel->load());
		}

		void Run()
		{
			net::https_client::RequestOptions requestOptions;
			requestOptions.inactivityTimeout = m_source.m_options.inactivityTimeout;
			requestOptions.cancel = &m_stop;

			net::https_client::Connection connection(m_source.m_host, m_source.m_port, requestOptions, m_source.m_context);

			uint32 failuresInARow = 0;

			for (;;)
			{
				std::vector<std::string> batch;
				{
					std::unique_lock lock{ m_mutex };
					m_fetchersCondition.wait_for(lock, std::chrono::milliseconds(100), [this]
					{
						return IsStopping() || ((!m_source.m_options.pause || !m_source.m_options.pause->load()) &&
							!m_queue.empty() && m_buffered < m_source.m_options.prefetchBufferLimit);
					});

					if (IsStopping())
					{
						return;
					}

					while ((!m_source.m_options.pause || !m_source.m_options.pause->load()) &&
						batch.size() < m_depth && !m_queue.empty() &&
					        m_buffered < m_source.m_options.prefetchBufferLimit)
					{
						std::string path = std::move(m_queue.front());
						m_queue.pop_front();

						// Readers may have claimed it in the meantime.
						const auto it = m_entries.find(path);
						if (it == m_entries.end() || it->second.state != State::Queued)
						{
							continue;
						}

						it->second.state = State::InFlight;
						m_buffered += it->second.size;
						batch.push_back(std::move(path));
					}
				}

				if (batch.empty())
				{
					continue;
				}

				std::vector<net::https_client::Request> requests(batch.size());
				for (size_t i = 0; i < batch.size(); ++i)
				{
					requests[i].host = m_source.m_host;
					requests[i].document = m_source.MakeDocument(batch[i]);
					requests[i].keepAlive = true;
				}

				size_t received = 0;
				try
				{
					received = connection.SendPipelined(std::move(requests), [this, &batch](const size_t index, net::https_client::Response response)
					{
						Complete(batch[index], std::move(response));
					});
				}
				catch (const std::exception &)
				{
					// Nothing came back. The files are marked failed below, and readers
					// fetch them directly with their usual retries.
				}

				{
					const std::scoped_lock lock{ m_mutex };

					// Put back what the server did not answer, at the front and in the
					// original order. If it answered nothing, the files go to the readers
					// instead, so that a broken connection cannot loop forever.
					for (size_t i = batch.size(); i-- > received; )
					{
						const auto it = m_entries.find(batch[i]);
						if (it == m_entries.end() || it->second.state != State::InFlight)
						{
							continue;
						}

						m_buffered -= it->second.size;
						if (received > 0)
						{
							it->second.state = State::Queued;
							m_queue.push_front(batch[i]);
						}
						else
						{
							it->second.state = State::Failed;
						}
					}
				}

				m_readersCondition.notify_all();
				m_fetchersCondition.notify_all();

				// Some proxies and servers break on pipelined requests. If whole batches
				// keep failing, fall back to one request at a time, which behaves exactly
				// like an ordinary keep-alive connection.
				if (received == 0)
				{
					if (++failuresInARow >= 2 && batch.size() > 1)
					{
						m_depth = 1;
					}
				}
				else
				{
					failuresInARow = 0;
				}
			}
		}

		void Complete(const std::string &path, net::https_client::Response response)
		{
			{
				const std::scoped_lock lock{ m_mutex };

				const auto it = m_entries.find(path);
				if (it == m_entries.end() || it->second.state != State::InFlight)
				{
					return;
				}

				if (response.status == net::https_client::Response::Ok)
				{
					it->second.state = State::Done;
					it->second.response = std::move(response);
				}
				else
				{
					// Error statuses are left to readFile, which classifies them.
					it->second.state = State::Failed;
					m_buffered -= it->second.size;
				}
			}

			m_readersCondition.notify_all();
		}

		HTTPSUpdateSource &m_source;

		std::mutex m_mutex;
		std::condition_variable m_fetchersCondition;
		std::condition_variable m_readersCondition;

		std::unordered_map<std::string, Entry> m_entries;
		std::deque<std::string> m_queue;

		/// Bytes of files in flight or done but not yet read.
		std::uintmax_t m_buffered = 0;

		std::atomic<uint32> m_depth;

		/// Also the cancel flag of the prefetch connections, so stopping aborts their
		/// transfers.
		std::atomic<bool> m_stop{ false };

		std::vector<std::thread> m_threads;
	};


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
		// Created up front, so that readFile never races its creation.
		if (m_options.prefetchConnections > 0)
		{
			m_prefetcher = std::make_unique<Prefetcher>(*this);
		}
	}

	HTTPSUpdateSource::~HTTPSUpdateSource()
	{
		// Joins the prefetch threads, which use the members below.
		m_prefetcher.reset();
	}

	std::string HTTPSUpdateSource::MakeDocument(const std::string &path) const
	{
		std::string document = m_path;
		virtual_dir::appendPath(document, path);
		return document;
	}

	void HTTPSUpdateSource::prefetch(const std::vector<RemoteFile> &files)
	{
		if (m_prefetcher)
		{
			m_prefetcher->Add(files);
		}
	}

	UpdateSourceFile HTTPSUpdateSource::readFile(
	    const std::string &path
	)
	{
		if (m_prefetcher)
		{
			if (auto response = m_prefetcher->Take(path))
			{
				return ToFile(path, std::move(*response));
			}
		}

		return ToFile(path, FetchDirect(path));
	}

	net::https_client::Response HTTPSUpdateSource::FetchDirect(const std::string &path)
	{
		net::https_client::Request request;
		request.host = m_host;
		request.document = MakeDocument(path);
		request.keepAlive = true;

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

		return response;
	}

	UpdateSourceFile HTTPSUpdateSource::ToFile(const std::string &path, net::https_client::Response response)
	{
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
