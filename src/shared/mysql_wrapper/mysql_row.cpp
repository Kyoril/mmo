// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mysql_row.h"
#include "mysql_select.h"
#include <cassert>

namespace mmo
{
	namespace mysql
	{
		Row::Row(Select &select)
			: m_row(nullptr)
			, m_length(0)
		{
			if (select.NextRow(m_row))
			{
				assert(m_row);
				m_length = select.GetFieldCount();
			}
		}

		Row::operator bool () const
		{
			return (m_row != nullptr);
		}

		std::size_t Row::GetLength() const
		{
			return m_length;
		}

		const char *Row::GetField(std::size_t index) const
		{
			assert(index < m_length);
			return m_row[index];
		}

		template <>
		bool Row::GetField<std::string, std::string>(std::size_t index, std::string& value) const
		{
			try
			{
				const char *const field = GetField(index);
				if (!field)
				{
					return false;
				}
                    
				// Simply assign the entire field as the string value
				value = field;
			}
			catch(...)
			{
				return false;
			}
                
			return true;
		}

		Row Row::Next(Select &select)
		{
			return Row(select);
		}
	}
}
