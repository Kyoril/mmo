// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <vector>
#include <ctime>
#include <cctype>
#include <sstream>

#include "random.h"
#ifndef _MSC_VER
#	include <strings.h>
#endif

#ifndef _MSC_VER
#	define _stricmp(l,r) strcasecmp(l, r)
#endif

namespace mmo
{
	static RandomnessGenerator randomGenerator(time(nullptr));

	static inline std::string UrlDecode(const std::string& encoded)
	{
		std::string decoded;
		char ch;
		int hexValue;

		for (size_t i = 0; i < encoded.length(); ++i) 
		{
			if (encoded[i] == '%' && i + 2 < encoded.length() &&
				std::isxdigit(encoded[i + 1]) && std::isxdigit(encoded[i + 2]))
			{
				// Convert hex to character
				std::stringstream ss;
				ss << std::hex << encoded.substr(i + 1, 2);
				ss >> hexValue;
				ch = static_cast<char>(hexValue);
				decoded += ch;
				i += 2; // Skip the next two hex digits
			}
			else if (encoded[i] == '+')
			{
				// Convert '+' to space
				decoded += ' ';
			}
			else
			{
				// Copy normal character
				decoded += encoded[i];
			}
		}

		return decoded;
	}
	/// A custom compare operator used to make string keys in a hash container case insensitive.
	struct StrCaseIComp
	{
		bool operator() (const std::string& lhs, const std::string& rhs) const {
			return _stricmp(lhs.c_str(), rhs.c_str()) < 0;
		}
	};

	// Function to trim whitespace from both ends of a string
	static inline std::string Trim(const std::string& str)
	{
		// Find the first non-whitespace character
		auto start = std::find_if_not(str.begin(), str.end(), [](unsigned char ch) {
			return std::isspace(ch);
			});

		// Find the last non-whitespace character
		auto end = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char ch) {
			return std::isspace(ch);
			}).base();

		// If there's no content, return an empty string
		if (start >= end) {
			return "";
		}

		return std::string(start, end);
	}

	static inline size_t FindCaseInsensitive(std::string data, std::string toSearch, size_t pos = 0)
	{
		// Convert complete given String to lower case
		std::transform(data.begin(), data.end(), data.begin(), ::tolower);
		// Convert complete given Sub String to lower case
		std::transform(toSearch.begin(), toSearch.end(), toSearch.begin(), ::tolower);
		// Find sub string in given string
		return data.find(toSearch, pos);
	}

	static inline size_t RFindCaseInsensitive(std::string data, std::string toSearch, size_t pos = std::string::npos)
	{
		// Convert complete given String to lower case
		std::transform(data.begin(), data.end(), data.begin(), ::tolower);
		// Convert complete given Sub String to lower case
		std::transform(toSearch.begin(), toSearch.end(), toSearch.begin(), ::tolower);
		// Find sub string in given string
		return data.rfind(toSearch, pos);
	}

	static inline bool StringEndsWith(const std::string& data, const std::string& compare)
	{
		return data.rfind(compare, std::string::npos) == data.size() - compare.size();
	}

	static inline bool StringEndsWithCaseInsensitive(const std::string& data, const std::string& compare)
	{
		return RFindCaseInsensitive(data, compare) == data.size() - compare.size();
	}

	static inline std::string GetFileExtension(const std::string& data)
	{
		size_t point = data.find_last_of('.');
		if (point != data.npos)
		{
			return data.substr(point);
		}

		return std::string();
	}

	template< typename T, size_t N >
	size_t countof(const T(&)[N]) { return N; }

	/// Splits a string into whitespace separated tokens.
	/// @remarks A token may be wrapped in double quotes to keep whitespace inside it, which is how
	///          values containing spaces (a data path below "C:\Program Files" for example) survive
	///          a round trip through a config file. A doubled quote inside a quoted token ("") is
	///          read as a single literal quote. Use QuoteConfigValue to produce that encoding.
	/// @param str The string to split.
	/// @param out_tokens Receives the tokens found in str. Empty tokens are only emitted for
	///                   explicitly quoted empty values ("").
	static inline void TokenizeString(const std::string& str, std::vector<std::string>& out_tokens)
	{
		std::string token;

		// Whether the current token contained a quoted section. Such a token is emitted even when
		// it is empty so that an explicit "" round trips as an empty value instead of vanishing.
		bool quoted = false;

		for (size_t i = 0; i < str.length(); i++)
		{
			const char c = str[i];

			if (c == ' ' || c == '\t')
			{
				if (!token.empty() || quoted)
				{
					out_tokens.emplace_back(std::move(token));
					token.clear();
					quoted = false;
				}
			}
			else if (c == '\"')
			{
				quoted = true;
				i++;

				while (i < str.length())
				{
					if (str[i] == '\"')
					{
						// A doubled quote inside a quoted section is a literal quote character.
						if (i + 1 < str.length() && str[i + 1] == '\"')
						{
							token.push_back('\"');
							i += 2;
							continue;
						}

						break;
					}

					token.push_back(str[i]);
					i++;
				}

				// i now refers to the closing quote, or to the end of the string if the quote was
				// never closed. Either way, the loop increment moves us past it.
			}
			else
			{
				token.push_back(c);
			}
		}

		if (!token.empty() || quoted)
		{
			out_tokens.emplace_back(std::move(token));
		}
	}

	/// Determines whether a value can be written to a line based config file at all.
	/// @remarks A line break cannot be encoded by quoting, because the reader splits the file into
	///          lines before it ever sees a quote. Everything behind the break would be read as a
	///          separate command, so such a value has to be rejected rather than mangled.
	/// @param value The raw value to test.
	/// @returns true if the value survives a write/read round trip.
	static inline bool IsWritableConfigValue(const std::string& value)
	{
		return value.find_first_of("\r\n") == std::string::npos;
	}

	/// Encodes a value so that TokenizeString reads it back as a single token.
	/// @remarks Values that contain neither whitespace nor quotes are returned unchanged to keep
	///          config files readable. Everything else is wrapped in double quotes, with embedded
	///          quotes doubled. Note that quoting cannot rescue a value containing a line break —
	///          check IsWritableConfigValue before writing one to a line based config file.
	/// @param value The raw value to encode.
	/// @returns The value, quoted and escaped if it needs to be.
	static inline std::string QuoteConfigValue(const std::string& value)
	{
		// Any whitespace has to trigger quoting, not just space and tab: the reader trims each
		// value with Trim(), which strips everything std::isspace accepts.
		const bool needsQuotes = value.empty() ||
			std::any_of(value.begin(), value.end(), [](const unsigned char c)
				{
					return c == '\"' || std::isspace(c) != 0;
				});

		if (!needsQuotes)
		{
			return value;
		}

		std::string result;
		result.reserve(value.length() + 2);

		result.push_back('\"');
		for (const char c : value)
		{
			if (c == '\"')
			{
				result.push_back('\"');
			}

			result.push_back(c);
		}
		result.push_back('\"');

		return result;
	}

	/// Splits a "<name> <value>" config assignment into its two halves.
	/// @remarks The value is everything behind the name: it is a single string no matter what it
	///          contains, so it is deliberately not tokenized. A quoted value is unquoted through
	///          TokenizeString, an unquoted one is taken verbatim so that config files written
	///          before values were quoted keep loading.
	/// @param args The argument string, without the leading command name.
	/// @param out_name Receives the name.
	/// @param out_value Receives the value.
	/// @returns true if both a name and a value were present.
	static inline bool ParseConfigAssignment(const std::string& args, std::string& out_name, std::string& out_value)
	{
		const auto nameStart = args.find_first_not_of(" \t");
		if (nameStart == std::string::npos)
		{
			return false;
		}

		const auto nameEnd = args.find_first_of(" \t", nameStart);
		if (nameEnd == std::string::npos)
		{
			return false;
		}

		const std::string rawValue = Trim(args.substr(nameEnd + 1));
		if (rawValue.empty())
		{
			return false;
		}

		out_name = args.substr(nameStart, nameEnd - nameStart);

		if (rawValue.front() == '\"')
		{
			std::vector<std::string> tokens;
			TokenizeString(rawValue, tokens);
			out_value = tokens.empty() ? std::string() : std::move(tokens.front());
		}
		else
		{
			out_value = rawValue;
		}

		return true;
	}
	
	namespace detail
	{
		template<class T>
		struct WritableHexDigit
		{
			const T& digit;

			explicit WritableHexDigit(const T& in_digit)
				: digit(in_digit)
			{
			}
		};
		
		template<class T>
		std::ostream &operator <<(std::ostream &stream, const WritableHexDigit<T> &digit)
		{
			return stream
				<< "0x" 
				<< std::hex << std::setfill('0') << std::setw(sizeof(digit.digit) * 2)
				<< digit.digit;
		}
	}
	
	template<typename T>
	static detail::WritableHexDigit<T> log_hex_digit(const T& digit)
	{
		return detail::WritableHexDigit<T>(digit);
	}
}