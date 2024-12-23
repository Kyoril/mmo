// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_crypt.h"

#include "base/big_number.h"

#include <algorithm>


namespace mmo
{
	namespace game
	{
		const size_t Crypt::CryptedSendLength = 6;
		const size_t Crypt::CryptedReceiveLength = 6;


		Crypt::Crypt()
			: m_initialized(false)
		{
		}

		Crypt::~Crypt()
		{
		}

		void Crypt::Init()
		{
			m_send_i = m_send_j = m_recv_i = m_recv_j = 0;
			m_initialized = true;
		}

		void Crypt::DecryptReceive(uint8 *data, size_t length)
		{
			if (!m_initialized) {
				return;
			}
			if (length < CryptedReceiveLength) {
				return;
			}

			for (size_t t = 0; t < CryptedReceiveLength; ++t)
			{
				m_recv_i %= m_key.size();
				uint8 x = (data[t] - m_recv_j) ^ m_key[m_recv_i];
				++m_recv_i;
				m_recv_j = data[t];
				data[t] = x;
			}
		}

		void Crypt::EncryptSend(uint8 *data, size_t length)
		{
			if (!m_initialized) {
				return;
			}
			if (length < CryptedSendLength) {
				return;
			}

			for (size_t t = 0; t < CryptedSendLength; ++t)
			{
				m_send_i %= m_key.size();
				uint8 x = (data[t] ^ m_key[m_send_i]) + m_send_j;
				++m_send_i;
				data[t] = m_send_j = x;
			}
		}

		void Crypt::SetKey(uint8 *key, size_t length)
		{
			m_key.resize(length);
			std::copy(key, key + length, m_key.begin());
		}

		void Crypt::GenerateKey(HMACHash &out_key, const BigNumber &prime)
		{
			HashGeneratorHmac sink;

			std::vector<uint8> arr = prime.asByteArray();
			sink.Update(reinterpret_cast<const char *>(arr.data()), arr.size());
			out_key = sink.Finalize();
		}
	}
}
