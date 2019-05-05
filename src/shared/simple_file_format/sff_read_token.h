// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include <cassert>

namespace sff
{
	namespace read
	{
		struct token_type
		{
			enum Enum
			{
				Unknown,
				LeftParenthesis,
				RightParenthesis,
				LeftBrace,
				RightBrace,
				LeftBracket,
				RightBracket,
				Assign,
				Comma,
				Plus,
				Minus,
				Identifier,
				Decimal,
				String
			};
		};

		typedef token_type::Enum TokenType;


		template <class T>
		struct Token
		{
			typedef typename std::iterator_traits<T>::value_type Char;
			typedef typename std::basic_string<Char> String;


			TokenType type;
			T begin, end;


			Token()
				: type(token_type::Unknown)
			{
			}

			explicit Token(TokenType type, T begin, T end)
				: type(type)
				, begin(begin)
				, end(end)
			{
			}

			bool isEndOfFile() const
			{
				return
				    (type == token_type::Unknown) &&
				    (begin == end);
			}

			String str() const
			{
				assert(type != token_type::Unknown);
				return String(begin, end);
			}

			std::size_t size() const
			{
				return std::distance(begin, end);
			}

			template <class TValue>
			TValue toInteger() const
			{
				return static_cast<TValue>(std::atoll(str().c_str()));
			}
		};
	}
}
