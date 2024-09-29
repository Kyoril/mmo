// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "include_mysql.h"
#include "base/typedefs.h"
#include <optional>
#include <variant>
#include <vector>

namespace mmo
{
	namespace mysql
	{
		struct StatementResult;
		struct Connection;

		struct Null
		{
		};

		struct ConstStringPtr
		{
			const std::string *ptr;
		};

		typedef std::variant<Null, int64, double, std::string, ConstStringPtr> Bind;


		struct Statement
		{
		private:

			Statement(const Statement &Other) = delete;
			Statement &operator=(const Statement &Other) = delete;

		public:

			Statement();
			Statement(Statement  &&other);
			explicit Statement(MYSQL &mysql);
			Statement(MYSQL &mysql, const char *query, size_t length);
			Statement(MYSQL &mysql, const std::string &query);
			Statement(Connection &mysql, const std::string &query);
			~Statement();
			Statement &operator = (Statement &&other);
			void Swap(Statement &other);
			void Prepare(const char *query, size_t length);
			size_t GetParameterCount() const;
			void SetParameter(std::size_t index, const Bind &argument);
			void SetString(std::size_t index, const std::string &value);
			void SetInt(std::size_t index, int64 value);
			void SetDouble(std::size_t index, double value);
			void Execute();
			StatementResult ExecuteSelect();

		private:

			MYSQL_STMT *m_handle;
			std::vector<std::optional<Bind>> m_parameters;
			std::vector<MYSQL_BIND> m_convertedParameters;
			std::vector<MYSQL_BIND> m_boundResult;

			void allocateHandle(MYSQL &mysql);
		};


		struct StatementResult
		{
		private:

			StatementResult(const StatementResult &Other) = delete;
			StatementResult &operator=(const StatementResult &Other) = delete;

		public:

			StatementResult();
			StatementResult(StatementResult &&other);
			explicit StatementResult(MYSQL_STMT &statementWithResults);
			~StatementResult();
			StatementResult &operator = (StatementResult &&other);
			void Swap(StatementResult &other);
			void StoreResult();
			size_t GetAffectedRowCount() const;
			bool FetchResultRow();
			std::string GetString(std::size_t index,
			                      std::size_t maxLengthInBytes = 1024 * 1024);
			int64 GetInt(std::size_t index);
			double GetDouble(std::size_t index);
			bool GetBoolean(std::size_t index);

		private:

			MYSQL_STMT *m_statement;
		};
	}
}
