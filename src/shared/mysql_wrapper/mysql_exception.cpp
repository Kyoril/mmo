// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "mysql_exception.h"


namespace mmo
{
	namespace mysql
	{
		Exception::Exception(const std::string &message)
			: std::runtime_error(message)
		{
		}


		OutOfRangeException::OutOfRangeException()
			: Exception("Index out of range")
		{
		}


		QueryFailException::QueryFailException(const std::string &message)
			: Exception(message)
		{
		}


		StatementException::StatementException(const std::string &message)
			: Exception(message)
		{
		}
	}
}
