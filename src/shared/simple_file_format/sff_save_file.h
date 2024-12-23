// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "sff_write_table.h"
#include "sff_write_writer.h"

#include <string>

namespace sff
{
	template <class C, class F>
	bool save_file(const std::basic_string<C> &fileName, F tableHandler, unsigned flags = write::MultiLine)
	{
		std::basic_ofstream<C> file(fileName.c_str());
		if (!file)
		{
			return false;
		}

		sff::write::Writer<C> writer(file);
		sff::write::Table<C> table(writer, flags);

		return tableHandler(table);
	}
}
