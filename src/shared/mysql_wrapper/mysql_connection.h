// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "include_mysql.h"

#include "base/typedefs.h"

#include <memory>

namespace mmo
{
	namespace mysql
	{
		struct DatabaseInfo
		{
			String host;
			uint16 port;
			String user;
			String password;
			String database;
			String updatePath;


			DatabaseInfo();
			explicit DatabaseInfo(
			    const String &host,
				uint16 port,
			    const String &user,
			    const String &password,
			    const String &database,
				const String &updatePath);
		};


		struct Connection
		{
		private:

			Connection(const Connection &Other) = delete;
			Connection &operator=(const Connection &Other) = delete;

		public:

			Connection();
			Connection(Connection  &&other);
			explicit Connection(const DatabaseInfo &info, bool allowMultiQuery);
			~Connection();
			Connection &operator = (Connection &&other);
			void Swap(Connection &other);
			bool Connect(const DatabaseInfo &info, bool allowMultiQuery);
			void Disconnect();
			bool Execute(const String &query);
			void TryExecute(const String &query);
			const char *GetErrorMessage();
			int GetErrorCode();
			MYSQL_RES *StoreResult();
			bool KeepAlive();
			String EscapeString(const String &str);
			MYSQL *GetHandle();
			bool IsConnected() const;
			uint64 GetLastInsertId() const;

		private:

			std::unique_ptr<MYSQL> m_mySQL;
			bool m_isConnected;
		};


		struct Transaction
		{
		private:

			Transaction(const Transaction &Other) = delete;
			Transaction &operator=(const Transaction &Other) = delete;

		public:

			explicit Transaction(Connection &connection);
			~Transaction();
			void Commit();

		private:

			Connection &m_connection;
			bool m_isCommit;
		};
	}
}
