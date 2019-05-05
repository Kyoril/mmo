// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "sff_read_tree.h"

namespace sff
{
	namespace detail
	{
		template <class I, class L>
		bool skipLiteral(I &pos, I end, L literal, L literal_end)
		{
			while (literal != literal_end)
			{
				if ((pos == end) ||
				        (*pos != *literal))
				{
					return false;
				}

				++pos;
				++literal;
			}

			return true;
		}

		template <class I>
		bool skipUTF8BOM(I &pos, I end)
		{
			static const std::array<char, 3> UTF8BOM =
			{{
					'\xEF', '\xBB', '\xBF',
				}
			};

			return skipLiteral(pos, end, UTF8BOM.begin(), UTF8BOM.end());
		}
	}

	template <class C, class I>
	void loadTableFromMemory(
	    sff::read::tree::Table<I> &fileTable,
	    const C &content,
	    FileEncoding encoding = UTF8_guess)
	{
		auto begin = content.begin();
		auto end = content.end();

		switch (encoding)
		{
		case UTF8_BOM:
		case UTF8_guess:
			{
				auto pos = begin;

				if (detail::skipUTF8BOM(pos, end))
				{
					begin = pos;
				}
				else if (encoding == UTF8_BOM)
				{
					throw InvalidEncodingException(UTF8_BOM);
				}
				break;
			}

		case UTF8:
			break;
		}

		sff::read::Parser<I> parser(
		    (begin), end);

		fileTable.parseFile(parser);
	}

	inline void loadFileIntoMemory(
	    std::istream &source,
	    std::string &content)
	{
		content.assign(std::istreambuf_iterator<char>(source),
		               std::istreambuf_iterator<char>());
	}

	template <class I>
	void loadTableFromFile(
	    sff::read::tree::Table<I> &fileTable,
	    std::string &content,
	    std::istream &source,
	    FileEncoding encoding = UTF8_guess)
	{
		loadFileIntoMemory(source, content);
		loadTableFromMemory(fileTable, content, encoding);
	}
}
