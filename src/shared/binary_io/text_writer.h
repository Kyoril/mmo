// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "sink.h"

#include <string>

namespace io
{
	template <class C>
	class TextWriter
	{
	public:

		typedef C Char;


		explicit TextWriter(io::ISink &sink)
			: m_sink(sink)
		{
		}

		void write(const Char *str)
		{
			m_sink.Write(
			    reinterpret_cast<const char *>(str),
			    getStringLength(str) * sizeof(Char));
		}

		void write(const std::basic_string<Char> &str)
		{
			m_sink.Write(
			    reinterpret_cast<const char *>(str.data()),
			    str.size() * sizeof(Char));
		}

		static std::size_t getStringLength(const Char *str)
		{
			const Char *p = str;

			while (*p)
			{
				++p;
			}

			return (p - str);
		}

	private:

		io::ISink &m_sink;
	};
}
