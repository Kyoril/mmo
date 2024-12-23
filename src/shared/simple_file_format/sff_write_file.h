// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "sff_write_writer.h"
#include "sff_write_table.h"

namespace sff
{
	namespace write
	{
		template <class C>
		struct File : Table<C>
		{
			typedef Table<C> Super;
			typedef typename Super::MyWriter MyWriter;
			typedef typename Super::Flags Flags;


			MyWriter writer;


			File(typename MyWriter::Stream &stream, Flags flags)
				: Super(writer, flags)
				, writer(stream)
			{
			}
		};
	}
}
