// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_protocol.h"
#include "game_crypt.h"

#include "base/non_copyable.h"
#include "base/macros.h"
#include "network/connection.h"
#include "network/send_sink.h"

#include "asio.hpp"


namespace mmo
{
	namespace game
	{
		///
		template<class P, class MySocket = asio::ip::tcp::socket>
		class EncryptedConnection
			: public AbstractConnection<P>
			, public std::enable_shared_from_this<EncryptedConnection<P, MySocket> >
			, public NonCopyable
		{
		public:
			typedef MySocket Socket;
			typedef P Protocol;
			typedef mmo::IConnectionListener<P> Listener;

		public:

			explicit EncryptedConnection(std::unique_ptr<Socket> Socket_, Listener *Listener_)
				: m_socket(std::move(Socket_))
				, m_listener(Listener_)
				, m_isParsingIncomingData(false)
				, m_isClosedOnParsing(false)
				, m_isClosedOnSend(false)
				, m_decryptedUntil(0)
				, m_isReceiving(false)
				, m_strand(m_socket->get_executor())
			{
			}
			virtual ~EncryptedConnection() = default;

		public:
			template<class F>
			void sendSinglePacket(F generator)
			{
				{
					io::StringSink sink(getSendBuffer());

					const size_t bufferPos = sink.Position();

					typename Protocol::OutgoingPacket packet(sink);
					generator(packet);

					m_crypt.EncryptSend(reinterpret_cast<uint8*>(&m_sendBuffer[bufferPos]), game::Crypt::CryptedSendLength);
				}
				
				flush();
			}
			
			inline bool IsConnected() const 
			{
				return m_socket && m_socket->is_open();
			}

		public:
			void setListener(Listener &Listener_) override
			{
				m_listener = &Listener_;
			}
			
			Listener* getListener() const
			{
				return m_listener;
			}
			
			void resetListener() override
			{
				m_listener = nullptr;
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
				m_isClosedOnParsing = false;
				m_isClosedOnSend = false;
				m_isReceiving = false;
				m_isParsingIncomingData = false;
				m_received.clear();

				asio::ip::tcp::no_delay Option(true);
				m_socket->lowest_layer().set_option(Option);
				BeginReceive();
			}
			
			void resumeParsing() override
			{
				ParsePackets();
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

				m_sending = std::move(m_sendBuffer);

				ASSERT(m_sendBuffer.empty());
				ASSERT(!m_sending.empty());

				BeginSend();
			}
			
			void close() override
			{
				// A write already handed to asio must be allowed to finish. Closing the socket out
				// from under it drops whatever had not yet reached the kernel buffer, which is
				// exactly the case for anything sent immediately before a teardown -- a session
				// kick reason, above all. The send completion closes for us instead.
				//
				// This mirrors mmo::Connection::close(); the two implement the same contract and
				// callers cannot tell which one they hold.
				if (!m_sending.empty())
				{
					m_isClosedOnSend = true;
				}

				if (m_isParsingIncomingData)
				{
					m_isClosedOnParsing = true;
				}

				if (m_isClosedOnSend || m_isClosedOnParsing)
				{
					return;
				}

				m_isClosedOnParsing = true;
				if (m_socket->is_open())
				{
					m_socket->close();

					m_received.clear();
				}
			}

		public:
			inline MySocket &getSocket() { return *m_socket; }
			inline game::Crypt &GetCrypt() { return m_crypt; }
			inline Listener *GetListener() const { return m_listener; }

			void SendBuffer(const char *data, std::size_t size)
			{
				m_sendBuffer.append(data, data + size);
			}

			void SendBuffer(const Buffer &data)
			{
				m_sendBuffer.append(data.data(), data.size());
			}

		public:
			static std::shared_ptr<EncryptedConnection> Create(asio::io_service &service, Listener *listener)
			{
				return std::make_shared<EncryptedConnection<P, MySocket> >(std::unique_ptr<MySocket>(new MySocket(service)), listener);
			}

		private:
			typedef std::array<char, 4096> ReceiveBuffer;

			std::unique_ptr<Socket> m_socket;
			Listener *m_listener;
			Buffer m_sending;
			Buffer m_sendBuffer;
			Buffer m_received;
			game::Crypt m_crypt;
			ReceiveBuffer m_receiving;
			bool m_isParsingIncomingData;
			bool m_isClosedOnParsing;
			/// Set when close() was called while a write was still in flight. The send completion
			/// performs the teardown instead, so the last packet is not cut off.
			bool m_isClosedOnSend;
			size_t m_decryptedUntil;
			bool m_isReceiving;
			/// Defaults to the protocol ceiling (game::MaxIncomingPacketSize, 16 MiB). Named as a
			/// literal rather than by including the protocol header, matching Connection.
			std::size_t m_maxReceiveBufferSize = 16 * 1024 * 1024;

			asio::strand<asio::any_io_executor> m_strand;

		private:
			void BeginSend()
			{
				ASSERT(!m_sending.empty());

				if (!m_socket)
					return;

				asio::async_write(
					*m_socket,
					asio::buffer(m_sending),
					asio::bind_executor(
						m_strand,
						std::bind(&EncryptedConnection<P, Socket>::Sent, this->shared_from_this(), std::placeholders::_1))
				);
			}

			void Sent(const asio::system_error &error)
			{
				if (error.code())
				{
					Disconnected();
					return;
				}

				m_sending.clear();
				flush();

				// A close() that arrived while this write was in flight was deferred to here, so
				// that the bytes reached the peer first.
				if (m_isClosedOnSend && m_sending.empty())
				{
					Disconnected();
					m_sendBuffer.clear();
				}
			}

			void BeginReceive()
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
						std::bind(&EncryptedConnection<P, Socket>::Received, this->shared_from_this(), std::placeholders::_2))
				);
			}

			void Received(std::size_t size)
			{
				m_isReceiving = false;

				ASSERT(size <= m_receiving.size());
				if (size == 0)
				{
					Disconnected();
					return;
				}

				m_received.append(
					m_receiving.begin(),
					m_receiving.begin() + size);

				ParsePackets();
			}

			void ParsePackets()
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
						m_isClosedOnParsing = false;
						Disconnected();
						return;
					}

					nextPacket = false;

					// Check if we have received a complete header which we didn't decrypt yet
					const size_t availableSize = m_received.size() - parsedUntil;
					if (m_decryptedUntil <= parsedUntil && availableSize >= game::Crypt::CryptedReceiveLength)
					{
						m_crypt.DecryptReceive(reinterpret_cast<uint8 *>(m_received.data() + parsedUntil), game::Crypt::CryptedReceiveLength);
						m_decryptedUntil = parsedUntil + game::Crypt::CryptedReceiveLength;
					}

					// Create a new memory source which uses the received packet data relative to the position we already parsed
					const char *const packetBegin = m_received.data() + parsedUntil;
					const char *const streamEnd = packetBegin + availableSize;
					io::MemorySource source(packetBegin, streamEnd);

					typename Protocol::IncomingPacket packet;
					const ReceiveState state = packet.Start(packet, source);

					switch (state)
					{
					case receive_state::Incomplete:
						// Do nothing here as we need to receive more data first
						break;
					case receive_state::Complete:
						{
							if (m_listener)
							{
								const PacketParseResult result = m_listener->connectionPacketReceived(packet);
								switch (result)
								{
								case PacketParseResult::Pass:
									nextPacket = true;
									break;
								case PacketParseResult::Block:
									break;
								case PacketParseResult::Disconnect:
									m_socket.reset();
									if (m_listener)
									{
										m_listener->connectionMalformedPacket();
										m_listener = nullptr;
									}
									break;
								}
							}

							const auto packetSize = packet.GetSize();
							const auto packetHeaderSize = sizeof(packet.GetId()) + sizeof(packetSize);

							const char* const expectedPacketEnd = packetBegin + packetHeaderSize + packetSize;
							const size_t expectedPacketReadSize = static_cast<std::size_t>(expectedPacketEnd - packetBegin);
							ASSERT(expectedPacketReadSize == static_cast<size_t>(source.getPosition() - source.getBegin()));

							// Ensure we have parsed the whole packet
							parsedUntil += expectedPacketReadSize;
							ASSERT(parsedUntil <= m_received.size());
						}
						
						break;
					case receive_state::Malformed:
						m_socket.reset();
						if (m_listener)
						{
							m_listener->connectionMalformedPacket();
							m_listener = nullptr;
							m_decryptedUntil = 0;
						}
						return;
					}
				} while (nextPacket);

				if (parsedUntil)
				{
					ASSERT(parsedUntil <= m_received.size());
					m_received.erase(m_received.begin(), m_received.begin() + parsedUntil);
					if (parsedUntil > m_decryptedUntil)
					{
						m_decryptedUntil = 0;
					}
					else
					{
						m_decryptedUntil -= parsedUntil;
					}
				}

				// Whatever is left is a single incomplete packet. If that alone is over the cap
				// it can only grow further, so there is nothing to wait for. See
				// Connection::parsePackets for the attack this closes.
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
					m_decryptedUntil = 0;

					// close(), not reset(): releasing the socket while a write is still in flight
					// leaves that write's completion handler to route into Disconnected(), which
					// would then be looking at a socket that no longer exists. Closing keeps the
					// pointer valid for whatever handlers are still queued. (Disconnected() also
					// null-checks now, but that is the backstop, not the design.)
					if (m_socket && m_socket->is_open())
					{
						asio::error_code error;
						m_socket->close(error);
					}

					return;
				}

				BeginReceive();
			}

			void Disconnected()
			{
				if (m_listener)
				{
					m_listener->connectionLost();
					m_listener = nullptr;
				}

				// Null-checked, matching IsConnected(). Several teardown paths in this class
				// release m_socket outright, and any handler still in flight when that happens --
				// a write completing with an error, above all -- lands here afterwards. Without
				// the check that is a null dereference on a path a peer can provoke.
				if (m_socket && m_socket->is_open())
				{
					asio::error_code error;
					m_socket->shutdown(asio::ip::tcp::socket::shutdown_both, error);
					if (!error.value())
					{
						m_socket->close(error);
					}
				}

				m_decryptedUntil = 0;
				m_received.clear();
			}
		};



		typedef mmo::game::EncryptedConnection<Protocol> Connection;
		typedef mmo::IConnectionListener<Protocol> IConnectionListener;
		typedef mmo::SendSink<Protocol> SendSink;
	}
}
