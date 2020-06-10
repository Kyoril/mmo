// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "include_mysql.h"
#include "base/typedefs.h"
#include "mysql_exception.h"
#include <sstream>

namespace mmo
{
	namespace mysql
	{
		struct Select;


		struct Row
		{
			explicit Row(Select &select);
			operator bool () const;
			std::size_t GetLength() const;
			const char *GetField(std::size_t index) const;

			template <class T, class F = T>
			bool GetField(std::size_t index, T &value) const
			{
				try
				{
					const char *const field = GetField(index);
					if (!field)
					{
						return false;
					}

					std::istringstream iss;
					iss.str(field);

					F tmp;
					iss >> tmp;

					value = static_cast<T>(tmp);
				}
				catch(...)
				{
					return false;
				}

				return true;
			}

			template <class T>
			void TryGetField(std::size_t index, T &value) const
			{
				if (index >= GetLength())
				{
					throw OutOfRangeException();
				}

				if (!GetField(index, value))
				{
					throw InvalidTypeException(value);
				}
			}

			template <class T>
			T TryGetField(std::size_t index) const
			{
				T temp;
				TryGetField(index, temp);
				return temp;
			}

			static Row Next(Select &select);

		private:

			MYSQL_ROW m_row;
			std::size_t m_length;
		};
	}
}
