// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "buffer.h"
#include "receive_state.h"
#include "base/assign_on_exit.h"
#include "binary_io/string_sink.h"
#include "binary_io/memory_source.h"

#include "asio/io_service.hpp"
#include "asio/ip/tcp.hpp"
#include "asio/write.hpp"
#include "asio/strand.hpp"

#include <functional>
#include <cassert>
#include <asio/bind_executor.hpp>
#include <asio/post.hpp>

#include "log/default_log_levels.h"

namespace mmo
{
	/// Enumerates possible packet parse results.
	enum class PacketParseResult
	{
		/// Process next packets
		Pass,
		/// Stop packet processing for this connection.
		Block,
		/// Close the connection.
		Disconnect
	};

	template<class P>
	struct IConnectionListener
	{
		typedef P Protocol;

		virtual ~IConnectionListener()
		{
		}

		virtual void connectionLost() = 0;
		virtual void connectionMalformedPacket() = 0;
		virtual PacketParseResult connectionPacketReceived(typename Protocol::IncomingPacket &packet) = 0;
		virtual void connectionDataSent(size_t size) {};
	};


	template<class P>
	class AbstractConnection
	{
	public:

		typedef P Protocol;
		typedef IConnectionListener<P> Listener;

	public:

		virtual ~AbstractConnection()
		{
		}

		virtual void setListener(Listener &Listener_) = 0;
		virtual void resetListener() = 0;
		virtual asio::ip::address getRemoteAddress() const = 0;
		virtual void startReceiving() = 0;
		virtual void resumeParsing() = 0;
		virtual void flush() = 0;
		virtual void close() = 0;
		virtual Buffer& getSendBuffer() = 0;

		/// Sets how many bytes may accumulate in the receive buffer before the peer is dropped
		/// as malformed.
		///
		/// The buffer holds whatever has arrived but not yet formed a complete packet, so it is
		/// exactly the quantity a peer controls: announce a large body, send most of it, stall.
		/// Bounding it is what makes that harmless. Set this well above the largest packet the
		/// link legitimately carries -- a value below it turns ordinary traffic into a
		/// disconnect.
		virtual void SetMaxReceiveBufferSize(std::size_t size) = 0;

		/// Runs `work` on this connection's strand, keeping the connection alive until it does.
		/// Safe to call from any thread.
		///
		/// This is how code that does not own this connection -- another connection's handler,
		/// or a web request handler on a different io thread -- reaches this connection's state
		/// without racing the handlers asio dispatches on the strand. Without it the only
		/// options are a lock around every member or an unsynchronised write, and the second is
		/// what this codebase had.
		virtual void Post(std::function<void()> work) = 0;

	public:
		template<class F>
		void sendSinglePacket(F generator, bool autoFlush = true)
		{
			io::StringSink sink(getSendBuffer());
			typename Protocol::OutgoingPacket packet(sink);
			generator(packet);

			if (autoFlush)
			{
				flush();
			}
		}
	};


	///
	template<class P, class MySocket = asio::ip::tcp::socket>
	class Connection
		: public AbstractConnection<P>
		, public std::enable_shared_from_this<Connection<P, MySocket> >
	{
	private:

		Connection(const Connection &Other) = delete;
		Connection &operator=(const Connection &Other) = delete;

	public:

		typedef MySocket Socket;
		typedef P Protocol;
		typedef IConnectionListener<P> Listener;

	public:

		explicit Connection(std::unique_ptr<Socket> Socket_, Listener *Listener_)
			: m_socket(std::move(Socket_))
			, m_listener(Listener_)
			, m_isParsingIncomingData(false)
			, m_isClosedOnParsing(false)
			, m_isClosedOnSend(false)
			, m_isReceiving(false)
			, m_strand(m_socket->get_executor())
		{
		}

		virtual ~Connection()
		{
		}

		void setListener(Listener &Listener_) override
		{
			m_listener = &Listener_;
		}

		void resetListener() override
		{
			m_listener = nullptr;
		}

		Listener *getListener() const
		{
			return m_listener;
		}

		asio::ip::address getRemoteAddress() const override
		{
			return m_socket->lowest_layer().remote_endpoint().address();
		}

		Buffer &getSendBuffer() override
		{
			return m_sendBuffer;
		}

		void SetMaxReceiveBufferSize(std::size_t size) override
		{
			m_maxReceiveBufferSize = size;
		}

		void Post(std::function<void()> work) override
		{
			auto self = this->shared_from_this();
			asio::post(m_strand, [self, work = std::move(work)]() { work(); });
		}

		void startReceiving() override
		{
			m_isClosedOnSend = false;
			m_isClosedOnParsing = false;
			m_isParsingIncomingData = false;
			m_isReceiving = false;

			m_received.clear();

			asio::ip::tcp::no_delay Option(true);
			m_socket->lowest_layer().set_option(Option);
			beginReceive();
		}

		void resumeParsing() override
		{
			parsePackets();
		}

		void flush() override
		{
			if (m_sendBuffer.empty())
			{
				return;
			}

			if (!m_sending.empty())
			{
				return;
			}

			m_sending = m_sendBuffer;
			m_sendBuffer.clear();

			assert(m_sendBuffer.empty());
			assert(!m_sending.empty());

			beginSend();
		}

		void close() override
		{
			if (!m_sending.empty())
			{
				m_isClosedOnSend = true;
			}

			if (m_isParsingIncomingData)
			{
				m_isClosedOnParsing = true;
			}

			if (m_isClosedOnSend ||
				m_isClosedOnParsing)
			{
				return;
			}

			m_isClosedOnParsing = true;
			m_isClosedOnSend = true;

			if (m_socket)
			{
				if (m_socket->is_open())
				{
					asio::error_code error;
					m_socket->shutdown(asio::ip::tcp::socket::shutdown_both, error);
					if (!error.value())
					{
						m_socket->close(error);
					}
				}
			}
		}

		void sendBuffer(const char *data, std::size_t size)
		{
			m_sendBuffer.append(data, data + size);
		}

		void sendBuffer(const Buffer &data)
		{
			m_sendBuffer.append(data.data(), data.size());
		}

		MySocket &getSocket() 
		{
			return *m_socket;
		}

		inline bool IsConnected() const 
		{
			return m_socket && m_socket->is_open();
		}

		static std::shared_ptr<Connection> create(asio::io_service &service, Listener *listener)
		{
			return std::make_shared<Connection<P, MySocket> >(std::unique_ptr<MySocket>(new MySocket(service)), listener);
		}

	private:

		typedef std::array<char, 4096> ReceiveBuffer;

		std::unique_ptr<Socket> m_socket;
		Listener *m_listener;
		Buffer m_sending;
		Buffer m_sendBuffer;
		Buffer m_received;
		ReceiveBuffer m_receiving;
		bool m_isParsingIncomingData;
		bool m_isClosedOnParsing;
		bool m_isClosedOnSend;
		bool m_isReceiving;

		/// Defaults to the protocol ceiling (see auth::MaxIncomingPacketSize /
		/// game::MaxIncomingPacketSize, both 16 MiB). Named here as a literal rather than by
		/// including a protocol header, which would invert the dependency: the protocols
		/// include this file, not the other way round.
		std::size_t m_maxReceiveBufferSize = 16 * 1024 * 1024;

		asio::strand<asio::any_io_executor> m_strand;

		void beginSend()
		{
			assert(!m_sending.empty());

			if (!m_socket)
				return;

			asio::async_write(
			    *m_socket,
			    asio::buffer(m_sending),
				asio::bind_executor(
					m_strand,
					std::bind(&Connection<P, Socket>::sent, this->shared_from_this(), std::placeholders::_1))
			);
		}

		void sent(const asio::system_error &error)
		{
			if (error.code())
			{
				disconnected();
				return;
			}

			if (m_listener)
			{
				m_listener->connectionDataSent(m_sending.size());
			}

			m_sending.clear();
			flush();

			if (m_isClosedOnSend && m_sending.empty())
			{
				disconnected();
				m_sendBuffer.clear();
				return;
			}
		}

		void beginReceive()
		{
			if (m_isReceiving)
				return;

			if (!m_socket)
				return;

			m_isReceiving = true;
			
			m_socket->async_read_some(
			    asio::buffer(m_receiving.data(), m_receiving.size()),
				asio::bind_executor(
					m_strand,
					std::bind(&Connection<P, Socket>::received, this->shared_from_this(), std::placeholders::_2))
			);
		}

		void received(std::size_t size)
		{
			m_isReceiving = false;
			
			assert(size <= m_receiving.size());
			if (size == 0)
			{
				disconnected();
				return;
			}

			m_received.append(
			    m_receiving.begin(),
			    m_receiving.begin() + size);

			parsePackets();
		}

		void parsePackets()
		{
			m_isParsingIncomingData = true;
			AssignOnExit<bool> isParsingIncomingDataResetter(
				m_isParsingIncomingData, false);

			bool nextPacket;
			std::size_t parsedUntil = 0;
			do
			{
				if (m_isClosedOnParsing)
				{
					WLOG("Disconnected - close on parsing");
					m_isClosedOnParsing = false;
					disconnected();

					m_received.clear();

					return;
				}

				nextPacket = false;

				const size_t availableSize = (m_received.size() - parsedUntil);
				const char *const packetBegin = &m_received[0] + parsedUntil;
				const char *const streamEnd = packetBegin + availableSize;

				io::MemorySource source(packetBegin, streamEnd);

				typename Protocol::IncomingPacket packet;
				const ReceiveState state = packet.Start(packet, source);
				const auto id = packet.GetId();

				switch (state)
				{
					case receive_state::Incomplete:
						break;
					case receive_state::Complete:
						if (m_listener)
						{
							auto result = m_listener->connectionPacketReceived(packet);
							switch (result)
							{
							case PacketParseResult::Pass:
								nextPacket = true;
								break;
							case PacketParseResult::Block:
								nextPacket = false;
								WLOG("Block request by packet handler");
								break;
							case PacketParseResult::Disconnect:
								m_isClosedOnParsing = true;
								WLOG("Disconnect request by packet handler");
								break;
							}
						}

						parsedUntil += static_cast<std::size_t>(source.getPosition() - source.getBegin());
						break;
					case receive_state::Malformed:
						WLOG("Disconnected - malformed packet");
						if (m_listener)
						{
							m_listener->connectionMalformedPacket();
							m_listener = nullptr;
						}

						m_socket->close();
						m_received.clear();

						return;
				}
			} while (nextPacket);

			// A handler may have requested a disconnect (PacketParseResult::Disconnect or
			// close()) for the last parsed packet, which leaves the loop without passing the
			// check at its top. Honor it now instead of waiting for more incoming data, so
			// the peer sees a closed connection instead of a silently hanging one.
			if (m_isClosedOnParsing)
			{
				m_isClosedOnParsing = false;

				// If a response (e.g. an error result) is still being flushed, defer the
				// close to the send completion handler so the peer receives it first.
				if (!m_sending.empty() || !m_sendBuffer.empty())
				{
					m_isClosedOnSend = true;
					flush();
					return;
				}

				disconnected();

				m_received.clear();

				return;
			}

			if (parsedUntil)
			{
				assert(parsedUntil <= m_received.size());

				m_received.erase(
					m_received.begin(),
					m_received.begin() + static_cast<std::ptrdiff_t>(parsedUntil));
			}

			// Whatever is left is a single incomplete packet. If that alone is over the cap it
			// can only grow further, so there is nothing to wait for. Without this a peer can
			// announce a legal body size, send most of it and stall, holding the buffer open --
			// repeated across connections that is memory the server never gets back.
			if (m_received.size() > m_maxReceiveBufferSize)
			{
				ELOG("Peer exceeded the maximum receive buffer size (" << m_received.size()
					<< " > " << m_maxReceiveBufferSize << " bytes) - dropping connection");

				if (m_listener)
				{
					m_listener->connectionMalformedPacket();
					m_listener = nullptr;
				}

				m_received.clear();

				if (m_socket && m_socket->is_open())
				{
					asio::error_code error;
					m_socket->close(error);
				}

				return;
			}

			beginReceive();
		}

		void disconnected()
		{
			if (m_listener)
			{
				m_listener->connectionLost();
				m_listener = nullptr;
			}

			if (m_socket->is_open())
			{
				asio::error_code error;
				m_socket->shutdown(asio::ip::tcp::socket::shutdown_both, error);
				if (!error.value())
				{
					m_socket->close(error);
				}
			}
			m_received.clear();
		}
	};
}
