// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "mysql_select.h"
#include "mysql_connection.h"
#include <cassert>

namespace mmo
{
	namespace mysql
	{
		Select::Select(Connection &connection, const String &query, bool throwOnError)
			: m_result(nullptr)
		{
			if (throwOnError)
			{
				connection.TryExecute(query);
			}
			else
			{
				if (!connection.Execute(query))
				{
					return;
				}
			}

			m_result = connection.StoreResult();
		}

		Select::~Select()
		{
			if (m_result)
			{
				::mysql_free_result(m_result);
			}
		}

		void Select::FreeResult()
		{
			assert(m_result);
			::mysql_free_result(m_result);
			m_result = nullptr;
		}

		bool Select::Success() const
		{
			return (m_result != nullptr);
		}

		bool Select::NextRow(MYSQL_ROW &row)
		{
			assert(m_result);
			row = ::mysql_fetch_row(m_result);
			return (row != nullptr);
		}

		std::size_t Select::GetFieldCount() const
		{
			assert(m_result);
			return ::mysql_num_fields(m_result);
		}
	}
}
