// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

namespace sff
{
	template <class T>
	class CharTraits
	{
	};

	template <>
	class CharTraits<char>
	{
	public:

		static const char LeftParenthesis = '(';
		static const char RightParenthesis = ')';
		static const char LeftBrace = '{';
		static const char RightBrace = '}';
		static const char LeftBracket = '[';
		static const char RightBracket = ']';
		static const char Assign = '=';
		static const char Comma = ',';
		static const char Dot = '.';
		static const char Plus = '+';
		static const char Minus = '-';
		static const char Underscore = '_';
		static const char Quotes = '"';
		static const char Slash = '/';
		static const char BackSlash = '\\';
		static const char EndOfLine = '\n';
		static const char Star = '*';

		static inline bool isAlpha(char c)
		{
			return
			    (c >= 'a' && c <= 'z') ||
			    (c >= 'A' && c <= 'Z');
		}

		static inline bool isDigit(char c)
		{
			return
			    (c >= '0') && (c <= '9');
		}

		static inline bool isIdentifierBegin(char c)
		{
			return isAlpha(c) || (c == Underscore);
		}

		static inline bool isIdentifierMiddle(char c)
		{
			return isAlpha(c) || (c == Underscore) || isDigit(c);
		}

		static inline bool isWhitespace(char c)
		{
			auto avoidAlwaysTrueWarning = static_cast<int>(c);
			return (avoidAlwaysTrueWarning >= '\0') && (c <= ' ');
		}
	};
}
