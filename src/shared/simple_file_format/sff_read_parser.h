// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "sff_read_scanner.h"
#include "sff_exceptions.h"
#include "sff_read_token.h"
#include "sff_read_scanguard.h"
#include "sff_datatypes.h"

namespace sff
{
	namespace read
	{
		template <class T>
		class Parser
		{
		public:

			typedef Token<T> MyToken;
			typedef typename MyToken::String String;

		public:

			Parser(T begin, T end)
				: m_scanner(begin, end)
				, m_end(end) //for the error token
			{
			}

			Scanner<T> &getScanner()
			{
				return m_scanner;
			}

			bool detectDataType(DataType &out_type)
			{
				switch (m_scanner.getCurrentToken().type)
				{
				case token_type::Plus:
				case token_type::Minus:
				case token_type::Decimal:
					out_type = TypeInteger;
					break;

				case token_type::String:
					out_type = TypeString;
					break;

				case token_type::LeftBrace:
					out_type = TypeArray;
					break;

				case token_type::LeftParenthesis:
					out_type = TypeTable;
					break;

				default:
					return false;
				}

				return true;
			}

			DataType detectDataTypeEx()
			{
				DataType type;
				if (!detectDataType(type))
				{
					throw ObjectExpectedException<T>(getErrorToken());
				}

				return type;
			}

			bool parseIntegerToken(bool isSigned, bool &out_negative, MyToken &out_digits)
			{
				MyScanGuard guard(m_scanner);
				MyToken token = guard.next();

				//allow prefix "+"
				if (token.type == token_type::Plus)
				{
					out_negative = false;
					token = guard.next();
				}

				//prefix "-" for signed integers
				else if (isSigned &&
				         (token.type == token_type::Minus))
				{
					out_negative = true;
					token = guard.next();
				}
				else
				{
					out_negative = false;
				}

				if (token.type == token_type::Decimal)
				{
					out_digits = token;
					guard.approve();
					afterObject();
					return true;
				}

				return false;
			}

			void parseIntegerTokenEx(bool isSigned, bool &out_negative, MyToken &out_digits)
			{
				if (!parseIntegerToken(isSigned, out_negative, out_digits))
				{
					throw TypeExpectedException<T, TypeInteger>(getErrorToken());
				}
			}

			template <class Integer>
			bool parseInteger(Integer &value)
			{
				const bool isSigned = std::numeric_limits<Integer>::is_signed;
				MyToken digits;
				bool isNegative;

				if (parseIntegerToken(isSigned, isNegative, digits))
				{
					value = static_cast<Integer>(std::atoll(digits.str()));

					if (isNegative)
					{
						value = -value;
					}

					return true;
				}

				return false;
			}

			template <class Integer>
			Integer parseInteger()
			{
				Integer temp = Integer();
				parseInteger(temp);
				return temp;
			}

			template <class Integer>
			Integer parseIntegerEx()
			{
				Integer temp;

				if (!parseInteger(temp))
				{
					throw TypeExpectedException<T, TypeInteger>(getErrorToken());
				}

				return temp;
			}

			bool parseStringToken(MyToken &out_content)
			{
				MyScanGuard guard(m_scanner);

				out_content = guard.next();

				if (out_content.type == token_type::String)
				{
					guard.approve();
					afterObject();
					return true;
				}

				return false;
			}

			MyToken parseStringTokenEx()
			{
				MyToken temp;

				if (!parseStringToken(temp))
				{
					throw TypeExpectedException<T, TypeString>(getErrorToken());
				}

				return temp;
			}

			String parseString()
			{
				String temp;
				parseString(temp);
				return temp;
			}

			bool parseString(String &dest)
			{
				MyScanGuard guard(m_scanner);

				const MyToken token = guard.next();

				if (token.type == token_type::String)
				{
					dest = decodeStringLiteral(token);

					guard.approve();
					afterObject();
					return true;
				}

				return false;
			}

			static String decodeStringLiteral(const MyToken &token)
			{
				String result;

				for (auto i = token.begin; i != token.end; ++i)
				{
					auto c = *i;
					if (c == '\\')
					{
						++i;

						//scanner assures this:
						assert(i != token.end);

						const char option = *i;
						switch (option)
						{
						case '\\':
						case '\'':
						case '\"':
							c = option;
							break;

						case 'n':
							c = '\n';
							break;
						case 'r':
							c = '\r';
							break;
						case 't':
							c = '\t';
							break;

						default:
							throw InvalidEscapeSequenceException<T>(token);
						}
					}

					result += c;
				}

				return std::move(result);
			}

			String parseStringEx()
			{
				String temp;
				if (!parseString(temp))
				{
					throw TypeExpectedException<T, TypeString>(getErrorToken());
				}

				return temp;
			}

			bool parseType(String &dest)
			{
				return parseString(dest);
			}

			bool parseType(signed int &dest) {
				return parseInteger(dest);
			}
			bool parseType(unsigned int &dest) {
				return parseInteger(dest);
			}
			bool parseType(signed long &dest) {
				return parseInteger(dest);
			}
			bool parseType(unsigned long &dest) {
				return parseInteger(dest);
			}
			bool parseType(signed long long &dest) {
				return parseInteger(dest);
			}
			bool parseType(unsigned long long &dest) {
				return parseInteger(dest);
			}

			bool enterArray()
			{
				MyScanGuard guard(m_scanner);

				const MyToken token = guard.next();

				if (token.type != token_type::LeftBrace)
				{
					return false;
				}

				guard.approve();
				return true;
			}

			void enterArrayEx()
			{
				if (!enterArray())
				{
					throw TypeExpectedException<T, TypeArray>(getErrorToken());
				}
			}

			bool leaveArray()
			{
				MyScanGuard guard(m_scanner);

				const MyToken token = guard.next();

				if (token.type != token_type::RightBrace)
				{
					return false;
				}

				guard.approve();
				afterObject();
				return true;
			}

			void leaveArrayEx()
			{
				if (!leaveArray())
				{
					throw UnexpectedTokenException<T>(getErrorToken(), "Operator '}' expected");
				}
			}

			template <class Value, class Inserter>
			bool parseArray(Inserter inserter)
			{
				MyScanGuard guard(m_scanner);

				const MyToken token = guard.next();

				if (token.type != token_type::LeftBrace)
				{
					return false;
				}

				Value temp;

				while (true)
				{
					const MyToken right = guard.next();

					if (right.type == token_type::RightBrace)
					{
						guard.approve();
						afterObject();
						return true;
					}

					guard.back();

					if (parseType(temp))
					{
						inserter(temp);
					}
					else
					{
						return false;
					}
				}
			}

			template <class Value, class Inserter>
			void parseArrayEx(Inserter inserter)
			{
				if (!parseArray(inserter))
				{
					throw TypeExpectedException<T, TypeArray>(getErrorToken());
				}
			}

			bool parseAssignment(MyToken &out_name)
			{
				MyScanGuard guard(m_scanner);

				MyToken name = guard.next();

				//Komma bei Aufzählung überspringen.
				//Nicht ganz korrekt, denn das erlaubt ein Komma vor dem ersten Element einer Map.
				if (name.type == token_type::Comma)
				{
					name = guard.next();
				}

				if (name.type != token_type::Identifier)
				{
					return false;
				}

				const MyToken assign = guard.next();

				if (assign.type != token_type::Assign)
				{
					throw UnexpectedTokenException<T>(assign, "Operator '=' expected");
				}

				out_name = name;

				guard.approve();
				return true;
			}

			bool parseAssignment(String &out_name)
			{
				MyToken temp;

				if (parseAssignment(temp))
				{
					out_name.assign(temp.begin, temp.end);
					return true;
				}

				return false;
			}

			bool enterTable()
			{
				MyScanGuard guard(m_scanner);
				const MyToken leftParenthesis = guard.next();

				if (leftParenthesis.type != token_type::LeftParenthesis)
				{
					return false;
				}

				guard.approve();
				return true;
			}

			void enterTableEx()
			{
				if (!enterTable())
				{
					throw TypeExpectedException<T, TypeTable>(getErrorToken());
				}
			}

			bool leaveTable()
			{
				MyScanGuard guard(m_scanner);
				const MyToken rightParenthesis = guard.next();

				if (rightParenthesis.type != token_type::RightParenthesis)
				{
					return false;
				}

				guard.approve();
				afterObject();
				return true;
			}

			void leaveTableEx()
			{
				if (!leaveTable())
				{
					throw UnexpectedTokenException<T>(getErrorToken(), "Operator ')' expected");
				}
			}

			bool skipAssignment()
			{
				String unused;
				return parseAssignment(unused); //inefficient
			}

			bool skipInteger()
			{
				MyScanGuard guard(m_scanner);
				MyToken token = guard.next();

				if (token.type == token_type::Minus ||
				        token.type == token_type::Plus)
				{
					token = guard.next();
				}

				if (token.type == token_type::Decimal)
				{
					guard.approve();
					afterObject();
					return true;
				}

				return false;
			}

			void skipIntegerEx()
			{
				if (!skipInteger())
				{
					throw TypeExpectedException<T, TypeInteger>(getErrorToken());
				}
			}

			bool skipString()
			{
				MyScanGuard guard(m_scanner);
				const MyToken token = guard.next();

				if (token.type == token_type::String)
				{
					guard.approve();
					afterObject();
					return true;
				}

				return false;
			}

			void skipStringEx()
			{
				if (!skipString())
				{
					throw TypeExpectedException<T, TypeString>(getErrorToken());
				}
			}

			bool skipArray()
			{
				MyScanGuard guard(m_scanner);
				const MyToken leftBrace = guard.next();

				if (leftBrace.type != token_type::LeftBrace)
				{
					return false;
				}

				while (true)
				{
					const MyToken rightBrace = guard.next();

					if (rightBrace.type == token_type::RightBrace)
					{
						guard.approve();
						afterObject();
						return true;
					}

					guard.back();

					if (!skipObject())
					{
						return false;
					}

					const MyToken comma = guard.next();
					if (comma.type != token_type::Comma)
					{
						guard.back();
					}
				}
			}

			void skipArrayEx()
			{
				if (!skipArray())
				{
					throw TypeExpectedException<T, TypeArray>(getErrorToken());
				}
			}

			bool skipTable()
			{
				MyScanGuard guard(m_scanner);
				const MyToken leftParenthesis = guard.next();

				if (leftParenthesis.type != token_type::LeftParenthesis)
				{
					return false;
				}

				while (true)
				{
					const MyToken rightParenthesis = guard.next();

					if (rightParenthesis.type == token_type::RightParenthesis)
					{
						guard.approve();
						afterObject();
						return true;
					}

					guard.back();

					if (!skipAssignment())
					{
						return false;
					}

					if (!skipObject())
					{
						return false;
					}

					const MyToken comma = guard.next();
					if (comma.type != token_type::Comma)
					{
						guard.back();
					}
				}
			}

			void skipTableEx()
			{
				if (!skipTable())
				{
					throw TypeExpectedException<T, TypeTable>(getErrorToken());
				}
			}

			bool skipPrimitive()
			{
				return
				    skipInteger() ||
				    skipString();
			}

			bool skipObject()
			{
				return
				    skipPrimitive() ||
				    skipTable() ||
				    skipArray();
			}

			void skipObjectEx()
			{
				if (!skipObject())
				{
					throw ObjectExpectedException<T>(getErrorToken());
				}
			}

			size_t getTokenLine(const MyToken &token) const
			{
				return m_scanner.getTokenLine(token);
			}

			const MyToken &getCurrentToken()
			{
				return m_scanner.getCurrentToken();
			}

			void popToken()
			{
				if (!isEndOfFile())
				{
					m_scanner.getTokenIndexReference()++;
				}
			}

			bool isEndOfFile()
			{
				return getCurrentToken().isEndOfFile();
			}

			void expectEndOfFile()
			{
				if (!isEndOfFile())
				{
					throw UnexpectedTokenException<T>(getCurrentToken(), "End of file expected");
				}
			}

			void skipOptionalComma()
			{
				MyScanGuard guard(m_scanner);

				const MyToken comma = guard.next();

				if (comma.type == token_type::Comma)
				{
					guard.approve();
				}
			}

		private:

			typedef Scanner<T> MyScanner;
			typedef ScanGuard<T> MyScanGuard;
			typedef typename MyScanner::MyCharTraits MyCharTraits;


			MyScanner m_scanner;
			T m_end;


			MyToken getErrorToken()
			{
				return m_scanner.getCurrentToken();
			}

			void afterObject()
			{
				//				skipOptionalComma();
			}
		};
	}
}
