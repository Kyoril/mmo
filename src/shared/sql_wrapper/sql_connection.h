// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <stdexcept>
#include <variant>
#include <string>

namespace mmo
{
	namespace sql
	{
		class Statement;

		struct DatabaseEditor
		{
			virtual ~DatabaseEditor();
			virtual void Create(const std::string &name) = 0;
			virtual void Drop(const std::string &name) = 0;
			virtual void Use(const std::string &name) = 0;
		};

		/// The Connection interface can be implemented for different
		/// SQL connectors for providing flexibility.
		class Connection
		{
		public:

			virtual ~Connection();

			/// execute tries to execute a textually given SQL query.
			/// @param query The begin of the SQL string.
			/// @param size The length of the SQL string from the begin.
			/// @throws if the query fails. The exception is derived from std::runtime_error.
			virtual void Execute(const char *query, std::size_t size) = 0;

			/// createStatement compiles an SQL query into a reusable
			/// statement.
			/// @param query The begin of the SQL string.
			/// @param size The length of the SQL string from the begin.
			/// @return the compiled statement which can be used from now. Never nullptr.
			/// @throws if the compilation fails. The exception is derived from std::runtime_error.
			virtual std::unique_ptr<Statement> CreateStatement(
			    const char *query,
			    std::size_t size) = 0;

			/// beginTransaction starts a transaction. A transaction
			/// combines an arbitrary number of queries into an atomic unit.
			/// Either all of the queries in the transaction will be executed or
			/// none. Other observers will never see a state of the database in
			/// between. This Connection is the only one which does.
			virtual void BeginTransaction() = 0;

			/// commit ends a transaction and guarantees that all of the
			/// queries in the transaction have been executed successfully on
			/// the database.
			/// @throws if the transaction could not be committed. Reasons for
			/// this can be specific to the DBMS you are actually using. A
			/// possible reason for the failure of a commit is a constraint
			/// violation.
			virtual void Commit() = 0;

			/// rollback stops a transaction and reverts the changes of
			/// the queries executed so far in this transaction. In other words,
			/// this transaction will have had no effect on the state of
			/// the database for observers.
			virtual void Rollback() = 0;

			virtual bool IsTableExisting(const std::string &tableName);
			virtual const std::string &GetAutoIncrementSyntax() const = 0;
			virtual DatabaseEditor *GetDatabaseEditor() = 0;

			/// execute is a utility which forwards the query to
			/// @link Connection::execute(const char *, std::size_t)}.
			/// @param query the query to be executed.
			void Execute(const std::string &query);

			std::unique_ptr<Statement> CreateStatement(const std::string &query);
		};


		/// The Null struct represents an SQL NULL.
		struct Null {};


		/// Primitive contains a simple SQL value type. Possible values
		/// are NULL, integers up to 64 bits, floats up to 64 bit or strings
		/// of 8-bit bytes.
		typedef std::variant<Null, int64, double, std::string> Primitive;


		class ResultReader
		{
		public:

			virtual ~ResultReader();
			virtual bool IsRow() const = 0;
			virtual void NextRow() = 0;
			virtual Primitive GetPrimitive(std::size_t column) const = 0;

			virtual int64 GetInt(std::size_t column) const;
			virtual double GetFloat(std::size_t column) const;
			virtual std::string GetString(std::size_t column) const;
		};


		class Statement
		{
		public:

			virtual ~Statement();

			virtual std::size_t GetParameterCount() const = 0;
			virtual void ClearParameters() = 0;
			virtual void Execute() = 0;
			virtual ResultReader &Select() = 0;
			virtual void FreeResult() = 0;
			virtual void SetParameter(std::size_t column, const Primitive &parameter) = 0;

			virtual void SetNull(std::size_t column);
			virtual void SetInt(std::size_t column, int64 value);
			virtual void SetFloat(std::size_t column, double value);
			virtual void SetString(std::size_t column,
			                       const char *begin,
			                       std::size_t size);

			void SetString(std::size_t column, const std::string &value);
		};


		void UnpackParameter(const Primitive &parameter,
		                     std::size_t column,
		                     Statement &statement);

		template <class Int>
		Int CastInt(int64 fullValue)
		{
			const Int castValue = static_cast<Int>(fullValue);

			//this typedef is outside of the lambda because of strange compiler behaviour
			//(GCC 4.7 wants the typename, VS11 rejects it)
			typedef typename std::make_signed<Int>::type SignedInt;
			const int64 checkedValue = [ = ]() -> int64
			{
				if (std::is_unsigned<Int>::value && (fullValue < 0))
				{
					return static_cast<SignedInt>(castValue);
				}
				return static_cast<int64>(castValue);
			}();
			if (checkedValue != fullValue)
			{
				throw std::runtime_error("SQL result integer cannot be represented in the requested type");
			}
			return castValue;
		}

		class Row final
		{
		public:

			Row();
			explicit Row(ResultReader &readerWithRow);
			int64 GetInt(std::size_t column) const;
			double GetFloat(std::size_t column) const;
			std::string GetString(std::size_t column) const;

			template <class Int>
			Int GetInt(std::size_t column) const
			{
				const int64 fullValue = GetInt(column);
				return CastInt<Int>(fullValue);
			}

			template <class Int>
			void GetIntInto(std::size_t column, Int &out_value) const
			{
				out_value = GetInt<Int>(column);
			}

		private:

			ResultReader *m_reader;
		};


		class Result final
		{
		public:

			explicit Result(Statement &statement);
			~Result();
			Row *GetCurrentRow();
			void NextRow();

		private:

			Statement &m_statement;
			ResultReader &m_reader;
			Row m_currentRow;
		};


		/// The Transaction struct begins a transaction on construction
		/// and guarantees to either commit or rollback the transaction before
		/// it is destroyed.
		class Transaction final
		{
		private:

			Transaction(const Transaction &Other) = delete;
			Transaction &operator=(const Transaction &Other) = delete;

		public:

			/// Transaction Begins a transaction.
			/// @param connection The connection to begin the transaction on.
			explicit Transaction(Connection &connection);

			/// Rolls back the transaction if not already committed.
			~Transaction();

			/// commit Commits the transaction. Undefined behaviour if
			/// called more than once on the same Transaction object.
			void Commit();

		private:

			Connection &m_connection;
			bool m_isCommit;
		};


		/// The Owner template extends a Statement or connection wrapper with
		/// automatic resource clean-up.
		/// @param Statement The statement wrapper to extend. Has to provide a
		/// default constructor, a Handle constructor, a swap method and a method
		/// getHandle which returns a pointer which can be managed with Handle.
		/// @param Handle A unique_ptr-like movable type which manages the
		/// lifetime of the internal statement resource.
		template <class Base, class Handle>
		class Owner
			: public Base
		{
		private:

			Owner<Base, Handle>(const Owner<Base, Handle> &Other) = delete;
			Owner<Base, Handle> &operator=(const Owner<Base, Handle> &Other) = delete;

		public:

			Owner()
			{
			}

			Owner(Handle handle)
				: Base(*handle)
			{
				handle.release();
			}

			Owner(Owner &&other)
			{
				this->swap(other);
				Owner().swap(other);
			}

			Owner &operator = (Owner &&other)
			{
				if (this != &other)
				{
					Owner(std::move(other)).swap(*this);
				}
				return *this;
			}

			virtual ~Owner()
			{
				(Handle(this->getHandle()));
			}
		};
	}
}
