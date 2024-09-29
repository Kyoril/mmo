// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include <cassert>
#include <memory>

#include "sql_connection.h"

namespace mmo
{
	namespace sql
	{
		DatabaseEditor::~DatabaseEditor()
		{
		}


		Connection::~Connection()
		{
		}

		bool Connection::IsTableExisting(const std::string &tableName)
		{
			const auto select = CreateStatement(
			                        "SELECT EXISTS(SELECT table_name FROM INFORMATION_SCHEMA.TABLES "
			                        "WHERE table_name LIKE ? AND table_schema=DATABASE())");
			select->SetString(0, tableName);
			sql::Result results(*select);
			sql::Row *const row = results.GetCurrentRow();
			if (!row)
			{
				throw ("Result row expected");
			}
			const bool doesExist = (row->GetInt(0) != 0);
			return doesExist;
		}

		void Connection::Execute(const std::string &query)
		{
			return Execute(query.data(), query.size());
		}

		std::unique_ptr<Statement> Connection::CreateStatement(
		    const std::string &query)
		{
			return CreateStatement(query.data(), query.size());
		}


		ResultReader::~ResultReader()
		{
		}

		namespace
		{
			template <class Number>
			struct PrimitiveToNumberConverter
			{
				Number operator ()(Null) const
				{
					return 0;
				}

				Number operator ()(int64 i) const
				{
					return static_cast<Number>(i);
				}

				Number operator ()(double i) const
				{
					return static_cast<Number>(i);
				}

				Number operator ()(const std::string &) const
				{
					return 0; //TODO
				}
			};

			struct PrimitiveToStringConverter
			{
				std::string operator ()(Null) const
				{
					return std::string();
				}

				std::string operator ()(int64) const
				{
					return std::string();
				}

				std::string operator ()(double) const
				{
					return std::string();
				}

				std::string operator ()(std::string &str) const
				{
					return std::move(str);
				}
			};

			template <class Result, class Converter>
			Result ConvertColumn(const ResultReader &reader, std::size_t column)
			{
				Converter converter;
				auto value = reader.GetPrimitive(column);
				return std::visit(converter, value);
			}
		}

		int64 ResultReader::GetInt(std::size_t column) const
		{
			return ConvertColumn<int64, PrimitiveToNumberConverter<int64>>(*this, column);
		}

		double ResultReader::GetFloat(std::size_t column) const
		{
			return ConvertColumn<double, PrimitiveToNumberConverter<double>>(*this, column);
		}

		std::string ResultReader::GetString(std::size_t column) const
		{
			return ConvertColumn<std::string, PrimitiveToStringConverter>(*this, column);
		}


		Statement::~Statement()
		{
		}

		void Statement::SetNull(std::size_t column)
		{
			SetParameter(column, Null());
		}

		void Statement::SetInt(std::size_t column, int64 value)
		{
			SetParameter(column, value);
		}

		void Statement::SetFloat(std::size_t column, double value)
		{
			SetParameter(column, value);
		}

		void Statement::SetString(std::size_t column,
		                          const char *begin,
		                          std::size_t size)
		{
			SetParameter(column, std::string(begin, size));
		}

		void Statement::SetString(std::size_t column, const std::string &value)
		{
			return SetString(column, value.data(), value.size());
		}


		namespace
		{
			class ParameterUnpacker
			{
			public:

				explicit ParameterUnpacker(Statement &statement,
				                           std::size_t column)
					: m_statement(statement)
					, m_column(column)
				{
				}

				void operator ()(sql::Null) const
				{
					m_statement.SetNull(m_column);
				}

				void operator ()(int64 value) const
				{
					m_statement.SetInt(m_column, value);
				}

				void operator ()(double value) const
				{
					m_statement.SetFloat(m_column, value);
				}

				void operator ()(const std::string &value) const
				{
					m_statement.SetString(m_column, value);
				}

			private:

				Statement &m_statement;
				const size_t m_column;
			};
		}

		void UnpackParameter(const Primitive &parameter,
		                     std::size_t column,
		                     Statement &statement)
		{
			ParameterUnpacker converter(statement, column);
			return std::visit(converter, parameter);
		}


		Row::Row()
			: m_reader(nullptr)
		{
		}

		Row::Row(ResultReader &readerWithRow)
			: m_reader(&readerWithRow)
		{
			assert(m_reader->IsRow());
		}

		int64 Row::GetInt(std::size_t column) const
		{
			return m_reader->GetInt(column);
		}

		double Row::GetFloat(std::size_t column) const
		{
			return m_reader->GetFloat(column);
		}

		std::string Row::GetString(std::size_t column) const
		{
			return m_reader->GetString(column);
		}


		Result::Result(Statement &statement)
			: m_statement(statement)
			, m_reader(statement.Select())
		{
		}

		Result::~Result()
		{
			m_statement.FreeResult();
		}

		Row *Result::GetCurrentRow()
		{
			if (m_reader.IsRow())
			{
				m_currentRow = Row(m_reader);
				return &m_currentRow;
			}
			return nullptr;
		}

		void Result::NextRow()
		{
			assert(m_reader.IsRow());
			m_reader.NextRow();
		}


		Transaction::Transaction(Connection &connection)
			: m_connection(connection)
			, m_isCommit(false)
		{
			m_connection.BeginTransaction();
		}

		Transaction::~Transaction()
		{
			if (!m_isCommit)
			{
				m_connection.Rollback();
			}
		}

		void Transaction::Commit()
		{
			//TODO forbid double commit with ASSERT?
			m_connection.Commit();
			m_isCommit = true;
		}
	}
}
