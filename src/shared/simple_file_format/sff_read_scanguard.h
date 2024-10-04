// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "sff_read_token.h"
#include "sff_read_scanner.h"

namespace sff
{
	namespace read
	{
		template <class T>
		struct ScanGuard
		{
			typedef Scanner<T> MyScanner;
			typedef Token<T> MyToken;


			MyScanner &scanner;


			explicit ScanGuard(MyScanner &scanner)
				: scanner(scanner)
				, m_index(scanner.getTokenIndexReference())
				, m_offset(0)
				, m_approved(false)
			{
			}

			~ScanGuard()
			{
				if (!m_approved)
				{
					m_index -= m_offset;
				}
			}

			void back()
			{
				--m_index;
				--m_offset;
			}

			MyToken next()
			{
				const MyToken temp = scanner.getToken(m_index);
				++m_index;
				++m_offset;
				return temp;
			}

			void approve()
			{
				m_approved = true;
			}

		private:

			std::size_t &m_index;
			std::size_t m_offset;
			bool m_approved;
		};
	}
}
