// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "typedefs.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"

#include <array>


namespace mmo
{
	namespace detail
	{
		template <class T, class S, std::size_t N>
		struct ReadArray
		{
			typedef std::array<T, N> Array;


			Array *dest;


			ReadArray()
				: dest(nullptr)
			{
			}

			explicit ReadArray(Array *dest)
				: dest(dest)
			{
			}
		};

		template <class T, class S, std::size_t N>
		io::Reader &operator >> (io::Reader &reader, const ReadArray<T, S, N> &arr)
		{
			for (unsigned i = 0; i < N; ++i)
			{
				reader >> io::read<S>((*arr.dest)[i]);
			}

			return reader;
		}


		template <class T, class S, std::size_t N>
		struct WriteArray
		{
			typedef std::array<T, N> Array;


			const Array *source;


			WriteArray()
				: source(nullptr)
			{
			}

			explicit WriteArray(const Array *source)
				: source(source)
			{
			}
		};

		template <class T, class S, std::size_t N>
		io::Writer &operator << (io::Writer &writer, const WriteArray<T, S, N> &arr)
		{
			for (std::size_t i = 0; i < N; ++i)
			{
				writer << io::write<S>((*arr.source)[i]);
			}

			return writer;
		}
	}


	template <class S, class T, std::size_t N>
	detail::ReadArray<T, S, N> read_array(std::array<T, N> &dest)
	{
		return detail::ReadArray<T, S, N>(&dest);
	}

	template <class S, class T, std::size_t N>
	detail::WriteArray<T, S, N> write_array(const std::array<T, N> &source)
	{
		return detail::WriteArray<T, S, N>(&source);
	}
}
