// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "source.h"
#include <string>
#include <cstddef>
#include <cmath>

namespace io
{
	class Reader
	{
	private:

		Reader(const Reader &other) = delete;
		Reader &operator=(const Reader &other) = delete;

	public:

		Reader()
			: m_source(nullptr)
			, m_success(true)
		{
		}

		explicit Reader(ISource &source)
			: m_source(&source)
			, m_success(true)
		{
		}

		ISource *getSource() const
		{
			return m_source;
		}

		void setSource(ISource *source)
		{
			m_source = source;
			m_success = true;
		}

		void skip(std::size_t size)
		{
			m_source->skip(size);
		}

		template <class T>
		void readPOD(T &pod)
		{
			if (!m_success)
			{
				return;
			}

			const size_t size = sizeof(pod);
			const size_t read = m_source->read(
			                        reinterpret_cast<char *>(&pod),
			                        size);

			m_success = (size == read);
		}

		void readPOD(float &pod)
		{
			if (!m_success)
			{
				return;
			}

			const size_t size = sizeof(pod);
			const size_t read = m_source->read(
				reinterpret_cast<char *>(&pod),
				size);

			if (std::isnan(pod) || !std::isfinite(pod))
				m_success = false;
			else
				m_success = (size == read);
		}

		void readPOD(double &pod)
		{
			if (!m_success)
			{
				return;
			}

			const size_t size = sizeof(pod);
			const size_t read = m_source->read(
				reinterpret_cast<char *>(&pod),
				size);

			if (std::isnan(pod) || !std::isfinite(pod))
				m_success = false;
			else
				m_success = (size == read);
		}

		operator bool () const
		{
			return m_success;
		}

		void setSuccess()
		{
			m_success = true;
		}

		void setFailure()
		{
			m_success = false;
		}

	private:

		ISource *m_source;
		bool m_success;
	};


#define BINARY_IO_READER_OPERATOR(type) \
	inline Reader &operator >> (Reader &r, type &value) \
	{ \
		r.readPOD(value); \
		return r; \
	}

	BINARY_IO_READER_OPERATOR(signed char)
	BINARY_IO_READER_OPERATOR(unsigned char)
	BINARY_IO_READER_OPERATOR(char)
	BINARY_IO_READER_OPERATOR(signed short)
	BINARY_IO_READER_OPERATOR(unsigned short)
	BINARY_IO_READER_OPERATOR(signed int)
	BINARY_IO_READER_OPERATOR(unsigned int)
	BINARY_IO_READER_OPERATOR(signed long)
	BINARY_IO_READER_OPERATOR(unsigned long)
	BINARY_IO_READER_OPERATOR(signed long long)
	BINARY_IO_READER_OPERATOR(unsigned long long)

#undef BINARY_IO_READER_OPERATOR

#define BINARY_IO_READER_FLOAT_OPERATOR(type) \
	inline Reader &operator >> (Reader &r, type &value) \
	{ \
		r.readPOD(value); \
		return r; \
	}

	BINARY_IO_READER_FLOAT_OPERATOR(float)
	BINARY_IO_READER_FLOAT_OPERATOR(double)
	BINARY_IO_READER_FLOAT_OPERATOR(long double)

#undef BINARY_IO_READER_FLOAT_OPERATOR

	//use read<T> instead
	Reader &operator >> (Reader &r, bool &value);

	namespace detail
	{
		template <class F, class T>
		struct ReadConverted
		{
			T &value;


			ReadConverted(T &value)
				: value(value)
			{
			}
		};

		template <class F, class T>
		Reader &operator >> (Reader &r, const ReadConverted<F, T> &surr)
		{
			F original;

			if (r >> original)
			{
				surr.value = static_cast<T>(original);
			}

			return r;
		}

		template <class F>
		Reader &operator >> (Reader &r, const ReadConverted<F, bool> &surr)
		{
			F original;

			if (r >> original)
			{
				surr.value = (original != 0);
			}

			return r;
		}
	}


	template <class F, class T>
	detail::ReadConverted<F, T> read(T &value)
	{
		return detail::ReadConverted<F, T>(value);
	}


	namespace detail
	{
		template <class I>
		struct ReadRange
		{
			I begin, end;


			ReadRange(I begin, I end)
				: begin(begin)
				, end(end)
			{
			}
		};

		template <class I>
		Reader &operator >> (Reader &r, const ReadRange<I> &range)
		{
			for (I i = range.begin; i != range.end; ++i)
			{
				r >> *i;
			}

			return r;
		}
	}


	template <class I>
	detail::ReadRange<I> read_range(I begin, I end)
	{
		return detail::ReadRange<I>(begin, end);
	}

	template <class R>
	detail::ReadRange<typename R::iterator> read_range(R &range)
	{
		return detail::ReadRange<typename R::iterator>(range.begin(), range.end());
	}


	namespace detail
	{
		template <class I, class E>
		struct ReadRangeWithConversion
		{
			I begin, end;


			ReadRangeWithConversion(I begin, I end)
				: begin(begin)
				, end(end)
			{
			}
		};

		template <class I, class E>
		Reader &operator >> (Reader &r, const ReadRangeWithConversion<I, E> &range)
		{
			for (I i = range.begin; i != range.end; ++i)
			{
				r >> read<E>(*i);
			}

			return r;
		}
	}


	template <class E, class I>
	detail::ReadRangeWithConversion<I, E> read_converted_range(I begin, I end)
	{
		return detail::ReadRangeWithConversion<I, E>(begin, end);
	}

	template <class E, class R>
	detail::ReadRangeWithConversion<typename R::iterator, E> read_converted_range(R &range)
	{
		return detail::ReadRangeWithConversion<typename R::iterator, E>(range.begin(), range.end());
	}


	namespace detail
	{
		struct Skip
		{
			std::size_t size;


			Skip(std::size_t size)
				: size(size)
			{
			}
		};


		inline Reader &operator >> (Reader &r, const Skip &skip)
		{
			r.skip(skip.size);
			return r;
		}
	}


	inline detail::Skip skip(std::size_t size)
	{
		return detail::Skip(size);
	}

	template <class T>
	detail::Skip skip()
	{
		return detail::Skip(sizeof(T));
	}


	namespace detail
	{
		template <class L, class C, class ReadElement>
		void readContainer(Reader &r, C &destination, L maxLength, const ReadElement &readElement)
		{
			typedef typename C::value_type Element;

			L length;

			if (r >> length)
			{
				const size_t readSize = std::min(length, maxLength);
				const size_t skipSize = length - readSize;

				destination.resize(readSize);
				for (size_t i = 0; i < readSize; ++i)
				{
					readElement(destination[i]);
				}

				//TODO: optimize for primitives
				Element dummy;
				for (size_t i = 0; i < skipSize; ++i)
				{
					r >> dummy;
				}
			}
		}


		template <class L, class C>
		struct ReadContainerWithLength
		{
			C &dest;
			L maxLength;


			ReadContainerWithLength(C &dest, L maxLength)
				: dest(dest)
				, maxLength(maxLength)
			{
			}
		};

		template <class L, class C>
		Reader &operator >> (Reader &r, const ReadContainerWithLength<L, C> &surr)
		{
			typedef typename C::value_type Element;

			readContainer(r,
			              surr.dest,
			              surr.maxLength,
			              [&r](Element & e)
			{
				r >> e;
			});
			return r;
		}


		template <class L, class E, class C>
		struct ReadContainerWithLengthAndConversion
		{
			C &dest;
			L maxLength;


			ReadContainerWithLengthAndConversion(C &dest, L maxLength)
				: dest(dest)
				, maxLength(maxLength)
			{
			}
		};

		template <class L, class E, class C>
		Reader &operator >> (Reader &r, const ReadContainerWithLengthAndConversion<L, E, C> &surr)
		{
			typedef typename C::value_type Element;

			readContainer(r,
			              surr.dest,
			              surr.maxLength,
			              [&r](Element & e)
			{
				r >> read<E>(e);
			});

			return r;
		}
	}


	template <class L, class C>
	detail::ReadContainerWithLength<L, C> read_container(C &container, L maxLength = (std::numeric_limits<L>::max)())
	{
		return detail::ReadContainerWithLength<L, C>(container, maxLength);
	}


	template <class L, class E, class C>
	detail::ReadContainerWithLengthAndConversion<L, E, C> read_converted_container(C &container, L maxLength = (std::numeric_limits<L>::max)())
	{
		return detail::ReadContainerWithLengthAndConversion<L, E, C>(container, maxLength);
	}

	namespace detail
	{
		struct ReadString
		{
			std::string &value;

			ReadString(std::string &value)
				: value(value)
			{
				value.clear();
			}
		};

		inline Reader &operator >> (Reader &r, const ReadString &surr)
		{
			char c = 0x00;
			do
			{
				if (!(r >> c))
				{
					return r;
				}
				if (c != 0)
				{
					surr.value.push_back(c);
				}
			} while (c != 0);

			return r;
		}
	}

	inline detail::ReadString read_string(std::string &value)
	{
		return detail::ReadString(value);
	}
	
	namespace detail
	{
		struct ReadablePackedGuid
		{
			std::uint64_t& guid;

			ReadablePackedGuid(std::uint64_t& guid_)
				: guid(guid_)
			{
			}
		};
		
		inline Reader &operator >> (Reader &r, const ReadablePackedGuid &surr)
		{
			std::uint8_t bitMask = 0;
			r >> io::read<std::uint8_t>(bitMask);
			if (!r) return r;
			
			surr.guid = 0;
			
			std::uint8_t value = 0;
			for (size_t i = 0; i < sizeof(std::uint8_t); ++i)
			{
				if (bitMask & (1 << i))
				{
					r >> io::read<std::uint8_t>(value);
					surr.guid |= static_cast<std::uint64_t>(value) << i;
				}
			}
			
			return r;
		}
		
	}
	
	inline detail::ReadablePackedGuid read_packed_guid(std::uint64_t& plain_guid)
	{
		return detail::ReadablePackedGuid(plain_guid);
	}
}
