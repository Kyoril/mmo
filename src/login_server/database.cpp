// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "database.h"

namespace mmo
{
	IDatabase::~IDatabase()
	{
	}

	AsyncDatabase::AsyncDatabase(IDatabase &database, ActionDispatcher asyncWorker, ActionDispatcher resultDispatcher)
		: m_database(database)
		, m_asyncWorker(std::move(asyncWorker))
		, m_resultDispatcher(std::move(resultDispatcher))
	{
	}
}
