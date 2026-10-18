// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chat_text.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace mmo
{
	namespace
	{
		std::string toLower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char c)
				{
					return static_cast<char>(std::tolower(c));
				});
			return text;
		}

		std::string trim(const std::string& text)
		{
			const auto first = text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
			{
				return std::string();
			}

			const auto last = text.find_last_not_of(" \t\r\n");
			return text.substr(first, last - first + 1);
		}

		bool isWordChar(const char c)
		{
			return std::isalnum(static_cast<unsigned char>(c)) != 0;
		}

		/// Phrases that give away a language model rather than a player.
		const char* const tellTales[] = {
			"as an ai", "an ai ", "language model", "i'm an assistant", "i am an assistant",
			"ai assistant", "chatbot", "openai", "anthropic", "system prompt", "my instructions",
		};

		/// Keys next to each letter on a QWERTY keyboard, for plausible slips.
		const char* neighbours(const char c)
		{
			switch (c)
			{
			case 'a': return "qws";
			case 'b': return "vn";
			case 'c': return "xv";
			case 'd': return "sf";
			case 'e': return "wr";
			case 'f': return "dg";
			case 'g': return "fh";
			case 'h': return "gj";
			case 'i': return "uo";
			case 'j': return "hk";
			case 'k': return "jl";
			case 'l': return "k";
			case 'm': return "n";
			case 'n': return "bm";
			case 'o': return "ip";
			case 'p': return "o";
			case 'q': return "w";
			case 'r': return "et";
			case 's': return "ad";
			case 't': return "ry";
			case 'u': return "yi";
			case 'v': return "cb";
			case 'w': return "qe";
			case 'x': return "zc";
			case 'y': return "tu";
			case 'z': return "x";
			default: return nullptr;
			}
		}

		/// Applies one slip to a word: two letters swapped, one dropped, one doubled or one hit
		/// next to its key.
		std::string mistype(const std::string& word, std::mt19937& rng)
		{
			std::string result = word;
			const std::size_t position = std::uniform_int_distribution<std::size_t>(1, word.size() - 1)(rng);

			switch (std::uniform_int_distribution<int>(0, 3)(rng))
			{
			case 0:
				std::swap(result[position - 1], result[position]);
				break;
			case 1:
				result.erase(position, 1);
				break;
			case 2:
				result.insert(position, 1, result[position]);
				break;
			default:
				if (const char* keys = neighbours(static_cast<char>(std::tolower(static_cast<unsigned char>(result[position])))))
				{
					const std::size_t count = std::char_traits<char>::length(keys);
					result[position] = keys[std::uniform_int_distribution<std::size_t>(0, count - 1)(rng)];
				}
				else
				{
					std::swap(result[position - 1], result[position]);
				}
				break;
			}

			return result;
		}
	}

	std::optional<std::string> SanitizeChatReply(const std::string& raw, const std::string& speakerName)
	{
		// Only the first line: a second one is the model continuing the conversation by itself.
		std::string text = trim(raw);
		if (const auto lineEnd = text.find_first_of("\r\n"); lineEnd != std::string::npos)
		{
			text = trim(text.substr(0, lineEnd));
		}

		// "Name: hello" and "[Name]: hello" are the model writing a transcript line instead of
		// speaking.
		const std::vector<std::string> prefixes = { speakerName + ":", "[" + speakerName + "]:", "you:" };
		for (const std::string& prefix : prefixes)
		{
			if (prefix.size() > 1 && toLower(text).rfind(toLower(prefix), 0) == 0)
			{
				text = trim(text.substr(prefix.size()));
				break;
			}
		}

		// Matching quotes around the whole line.
		while (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front())
		{
			text = trim(text.substr(1, text.size() - 2));
		}

		std::string printable;
		printable.reserve(text.size());
		for (const char c : text)
		{
			const auto u = static_cast<unsigned char>(c);
			if (u >= 0x20 && u < 0x7F)
			{
				printable.push_back(c);
			}
		}
		text = trim(printable);

		if (text.empty())
		{
			return std::nullopt;
		}

		const std::string lower = toLower(text);
		if (lower.rfind("skip", 0) == 0 && lower.size() <= 6)
		{
			return std::nullopt;
		}

		if (lower.find("http") != std::string::npos || lower.find("www.") != std::string::npos)
		{
			return std::nullopt;
		}

		for (const char* tell : tellTales)
		{
			if (lower.find(tell) != std::string::npos)
			{
				return std::nullopt;
			}
		}

		if (text.size() > MaxBotChatLength)
		{
			auto cut = text.rfind(' ', MaxBotChatLength);
			if (cut == std::string::npos || cut < MaxBotChatLength / 2)
			{
				cut = MaxBotChatLength;
			}

			text = trim(text.substr(0, cut));
		}

		return text;
	}

	HumanizedChat HumanizeChat(const std::string& text, const TypingStyle& style, std::mt19937& rng)
	{
		HumanizedChat result;
		std::string line = text;

		if (style.lowercase)
		{
			line = toLower(line);
		}

		if (style.dropApostrophes)
		{
			line.erase(std::remove(line.begin(), line.end(), '\''), line.end());
		}

		if (style.dropTrailingPeriod && !line.empty() && line.back() == '.'
			&& (line.size() < 2 || line[line.size() - 2] != '.'))
		{
			line.pop_back();
		}

		// Collect word spans and pick at most one to mistype.
		std::vector<std::pair<std::size_t, std::size_t>> words;
		for (std::size_t i = 0; i < line.size();)
		{
			if (!std::isalpha(static_cast<unsigned char>(line[i])))
			{
				++i;
				continue;
			}

			std::size_t end = i;
			while (end < line.size() && std::isalpha(static_cast<unsigned char>(line[end])))
			{
				++end;
			}

			if (end - i >= 3)
			{
				words.emplace_back(i, end - i);
			}

			i = end;
		}

		std::uniform_real_distribution<float> chance(0.0f, 1.0f);
		for (const auto& [start, length] : words)
		{
			if (chance(rng) >= style.typoChancePerWord)
			{
				continue;
			}

			const std::string original = line.substr(start, length);
			const std::string typo = mistype(original, rng);
			if (typo == original)
			{
				continue;
			}

			line.replace(start, length, typo);
			if (chance(rng) < style.correctionChance)
			{
				result.correction = original;
			}

			break;
		}

		result.text = line;
		return result;
	}

	bool MentionsName(const std::string& message, const std::string& name)
	{
		if (name.empty())
		{
			return false;
		}

		const std::string haystack = toLower(message);
		const std::string needle = toLower(name);

		for (auto position = haystack.find(needle); position != std::string::npos; position = haystack.find(needle, position + 1))
		{
			const bool startOk = position == 0 || !isWordChar(haystack[position - 1]);
			const std::size_t end = position + needle.size();
			const bool endOk = end >= haystack.size() || !isWordChar(haystack[end]);
			if (startOk && endOk)
			{
				return true;
			}
		}

		return false;
	}
}
