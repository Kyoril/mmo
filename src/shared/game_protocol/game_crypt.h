// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/hmac.h"

#include <vector>


namespace mmo
{
	class BigNumber;

	namespace game
	{
		/// Used for packet encryption and decryption.
		class Crypt final
		{
		public:
			const static size_t CryptedSendLength;
			const static size_t CryptedReceiveLength;

		public:
			explicit Crypt();
			~Crypt();

			void Init();
			void SetKey(uint8 *key, size_t length);
			void DecryptReceive(uint8 *data, size_t length);
			void EncryptSend(uint8 *data, size_t length);

		public:
			inline bool IsInitialized() const { return m_initialized;}

		public:
			/// @param out_key The generated key.
			/// @param prime Big number to be used for key calculation.
			static void GenerateKey(HMACHash &out_key, const BigNumber &prime);

		private:
			std::vector<uint8> m_key;
			uint8 m_send_i, m_send_j, m_recv_i, m_recv_j;
			bool m_initialized;
		};
	}
}
