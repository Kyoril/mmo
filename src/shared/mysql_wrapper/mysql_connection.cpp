// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "mysql_connection.h"
#include "mysql_exception.h"

#include <vector>
#include <cassert>

namespace mmo
{
	namespace mysql
	{
		DatabaseInfo::DatabaseInfo()
			: port(0)
		{
		}

		DatabaseInfo::DatabaseInfo(
		    const String &host,
		    uint16 port,
		    const String &user,
		    const String &password,
		    const String &database)
			: host(host)
			, port(port)
			, user(user)
			, password(password)
			, database(database)
		{
		}


		Connection::Connection()
			: m_mySQL(new MYSQL)
			, m_isConnected(false)
		{
			if (!::mysql_init(m_mySQL.get()))
			{
				throw std::bad_alloc();
			}
		}

		Connection::Connection(const DatabaseInfo &info)
			: m_mySQL(new MYSQL)
			, m_isConnected(false)
		{
			if (!::mysql_init(m_mySQL.get()))
			{
				throw std::bad_alloc();
			}
			if (!Connect(info))
			{
				throw Exception(mysql_error(GetHandle()));
			}
		}

		Connection::Connection(Connection  &&other)
			: m_isConnected(false)
		{
			Swap(other);
		}

		Connection::~Connection()
		{
			if (GetHandle())
			{
				::mysql_close(GetHandle());
			}
		}

		Connection &Connection::operator = (Connection &&other)
		{
			Swap(other);
			return *this;
		}

		void Connection::Swap(Connection &other)
		{
			using std::swap;
			swap(m_mySQL, other.m_mySQL);
			swap(m_isConnected, other.m_isConnected);
		}

		bool Connection::Connect(const DatabaseInfo &info)
		{
			assert(!m_isConnected);

			{
				my_bool reconnect = true;
				if (::mysql_options(GetHandle(), MYSQL_OPT_RECONNECT, &reconnect) != 0)
				{
					return false;
				}
			}

			MYSQL *const result = ::mysql_real_connect(GetHandle(),
			                      info.host.c_str(),
			                      info.user.c_str(),
			                      info.password.c_str(),
			                      info.database.c_str(),
			                      static_cast<uint16>(info.port),
			                      0,
			                      0);

			m_isConnected = (result != nullptr);
			return m_isConnected;
		}

		bool Connection::Execute(const String &query)
		{
			assert(m_isConnected);

			const int result = ::mysql_real_query(GetHandle(),
			                                      query.data(),
			                                      static_cast<unsigned long>(query.size()));
			return (result == 0);
		}

		void Connection::TryExecute(const String &query)
		{
			if (!Execute(query))
			{
				throw QueryFailException(GetErrorMessage());
			}
		}

		const char *Connection::GetErrorMessage()
		{
			return ::mysql_error(GetHandle());
		}

		int Connection::GetErrorCode()
		{
			return ::mysql_errno(GetHandle());
		}

		MYSQL_RES *Connection::StoreResult()
		{
			assert(m_isConnected);
			return ::mysql_store_result(GetHandle());
		}

		bool Connection::KeepAlive()
		{
			return (::mysql_ping(GetHandle()) == 0);
		}

		String Connection::EscapeString(const String &str)
		{
			const std::size_t worstCaseLength = (str.size() * 2 + 1);
			std::vector<char> temp(worstCaseLength);
			char *const begin = temp.data();
			const std::size_t length = ::mysql_real_escape_string(GetHandle(), begin, str.data(), static_cast<unsigned long>(str.size()));
			const char *const end = (begin + length);
			return String(static_cast<const char *>(begin), end);
		}

		MYSQL *Connection::GetHandle()
		{
			return m_mySQL.get();
		}

		bool Connection::IsConnected() const
		{
			return m_isConnected;
		}


		Transaction::Transaction(Connection &connection)
			: m_connection(connection)
			, m_isCommit(false)
		{
			m_connection.TryExecute("START TRANSACTION");
		}

		Transaction::~Transaction()
		{
			if (!m_isCommit)
			{
				m_connection.TryExecute("ROLLBACK");
			}
		}

		void Transaction::Commit()
		{
			assert(!m_isCommit);
			m_connection.TryExecute("COMMIT");
			m_isCommit = true;
		}
	}
}
