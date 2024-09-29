// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <array>
#include <type_traits>
#include <algorithm>
#include <sstream>

namespace sff
{
	namespace write
	{
		struct EscapeReplacement
		{
			char original;
			std::string replaced;
		};


		static const std::array<EscapeReplacement, 6> QuotedStringReplacements =
		{{
				{'\n', "\\n"},
				{'\r', "\\r"},
				{'\t', "\\t"},
				{'\"', "\\\""},
				{'\'', "\\\'"},
				{'\\', "\\\\"},
			}
		};


		template <class String, class Replacements>
		static String escapeString(const String &raw, const Replacements &replacements)
		{
			String result;
			result.reserve(raw.size());

			for (auto r = raw.begin(); r != raw.end(); ++r)
			{
				const auto replacement = std::find_if(
				                             replacements.begin(),
				                             replacements.end(),
				                             [r](const EscapeReplacement & repl)
				{
					return (repl.original == *r);
				});

				if (replacement == replacements.end())
				{
					result += *r;
				}
				else
				{
					result += replacement->replaced;
				}
			}

			return result;
		}


		template <class C>
		struct Writer
		{
			typedef C Char;
			typedef std::basic_ostream<Char> Stream;
			typedef std::basic_string<Char> String;


			explicit Writer(Stream &stream)
				: m_stream(stream)
				, m_indentation(0)
			{
			}

			template <class Key>
			void writeKey(const Key &key)
			{
				m_stream
				        << key
				        << m_stream.widen(' ')
				        << m_stream.widen('=')
				        << m_stream.widen(' ');
			}

			template <class Value>
			void writeValue(const Value &value)
			{
				if (std::is_floating_point<Value>::value)
				{
					//a super-ugly hack to get non-scientific notation without trailing zeros
					std::ostringstream buffer;
					buffer.precision(6);
					buffer << std::fixed << value;
					std::string intermediate = buffer.str();
					if (std::find(begin(intermediate), end(intermediate), '.') != end(intermediate))
					{
						while (
						    intermediate.size() >= 2 &&
						    intermediate.back() == '0')
						{
							intermediate.resize(intermediate.size() - 1);
						}
						if (intermediate.back() == '.')
						{
							intermediate.resize(intermediate.size() - 1);
						}
					}
					m_stream << intermediate;
				}
				else
				{
					m_stream << value;
				}
			}

			void writeValue(const String &value)
			{
				m_stream
				        << m_stream.widen('"')
				        << escapeString(value, QuotedStringReplacements)
				        << m_stream.widen('"')
				        ;
			}

			void writeValue(const Char *value)
			{
				writeValue(String(value));
			}

			template <class Key, class Value>
			void writeAssignment(const Key &key, const Value &value)
			{
				writeKey(key);
				writeValue(value);
			}

			template <class Key, class Value>
			void writeStringAssignment(const Key &key, const Value &value)
			{
				writeKey(key);
				writeString(value);
			}

			void enterArray()
			{
				m_stream
				        << m_stream.widen('{');
			}

			void leaveArray()
			{
				m_stream
				        << m_stream.widen('}');
			}

			void enterTable()
			{
				m_stream
				        << m_stream.widen('(');
			}

			void leaveTable()
			{
				m_stream
				        << m_stream.widen(')');
			}

			void writeComma()
			{
				m_stream
				        << m_stream.widen(',');
			}

			void newLine()
			{
				m_stream << std::endl;
			}

			void space()
			{
				m_stream
				        << m_stream.widen(' ');
			}

			void enterLevel()
			{
				++m_indentation;
			}

			void leaveLevel()
			{
				--m_indentation;
			}

			void writeIndentation()
			{
				for (std::size_t i = 0; i < m_indentation; ++i)
				{
					m_stream << m_stream.widen('\t');
				}
			}

			template <class Comment>
			void lineComment(const Comment &text)
			{
				newLine();
				writeIndentation();

				m_stream
				        << m_stream.widen('/')
				        << m_stream.widen('/')
				        << text;
			}

			template <class Comment>
			void comment(const Comment &text)
			{
				newLine();
				writeIndentation();

				m_stream
				        << m_stream.widen('/')
				        << m_stream.widen('*')
				        << text
				        << m_stream.widen('*')
				        << m_stream.widen('/');
			}

		private:

			Stream &m_stream;
			std::size_t m_indentation;
		};
	}
}
