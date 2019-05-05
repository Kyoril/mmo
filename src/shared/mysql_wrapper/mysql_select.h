// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "include_mysql.h"
#include "base/typedefs.h"

namespace mmo
{
	namespace mysql
	{
		struct Connection;


		struct Select
		{
		private:

			Select(const Select &Other) = delete;
			Select &operator=(const Select &Other) = delete;

		public:

			explicit Select(Connection &connection, const String &query, bool throwOnError = false);
			~Select();
			void FreeResult();
			bool Success() const;
			bool NextRow(MYSQL_ROW &row);
			std::size_t GetFieldCount() const;

		private:

			MYSQL_RES *m_result;
		};
	}
}
