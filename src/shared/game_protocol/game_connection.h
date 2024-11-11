// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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
			
			void startReceiving() override
			{
				m_isClosedOnParsing = false;
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
				if (m_isParsingIncomingData)
				{
					m_isClosedOnParsing = true;
				}
				else
				{
					m_isClosedOnParsing = true;
					if (m_socket->is_open())
					{
						m_socket->close();

						m_received.clear();
					}
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
			size_t m_decryptedUntil;
			bool m_isReceiving;
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

					const size_t availableSize = (m_received.size() - parsedUntil);
					
					// Check if we have received a complete header
					if (m_decryptedUntil <= parsedUntil &&
						availableSize >= game::Crypt::CryptedReceiveLength)
					{
						m_crypt.DecryptReceive(reinterpret_cast<uint8 *>(&m_received[0] + parsedUntil), game::Crypt::CryptedReceiveLength);

						// This will prevent double-decryption of the header (which would produce
						// invalid packet sizes)
						m_decryptedUntil = parsedUntil + game::Crypt::CryptedReceiveLength;
					}

					const char *const packetBegin = &m_received[0] + parsedUntil;
					const char *const streamEnd = packetBegin + availableSize;

					io::MemorySource source(packetBegin, streamEnd);

					typename Protocol::IncomingPacket packet;
					const ReceiveState state = packet.Start(packet, source);

					switch (state)
					{
					case receive_state::Incomplete:
						break;
					case receive_state::Complete:
						{
							if (m_listener)
							{
								auto result = m_listener->connectionPacketReceived(packet);
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
							const char* const expectedPacketEnd = &m_received[0] + parsedUntil + packetSize;
							const size_t expectedPacketReadSize = static_cast<std::size_t>(expectedPacketEnd - packetBegin) + sizeof(packet.GetId()) + sizeof(packetSize);
							ASSERT(expectedPacketReadSize == static_cast<size_t>(source.getPosition() - source.getBegin()));

							parsedUntil += expectedPacketReadSize;
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

					m_received.erase(
						m_received.begin(),
						m_received.begin() + parsedUntil);
					m_decryptedUntil = 0;
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

				if (m_socket->is_open())
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
