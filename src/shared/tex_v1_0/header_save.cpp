// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "header_save.h"
#include "binary_io/writer.h"
#include "base/io_array.h"
#include "tex/pre_header_save.h"
#include "tex/pre_header.h"
#include "base/macros.h"
#include "header.h"


namespace mmo
{
	namespace tex
	{
		namespace v1_0
		{
			HeaderSaver::HeaderSaver(io::ISink &destination, const Header& header)
				: m_destination(destination)
			{
				io::Writer writer(destination);
				savePreHeader(PreHeader(Version_1_0), writer);

				writer
					<< io::write<uint8>(header.compression)
					<< io::write<uint8>(header.hasMips)
					<< io::write<uint16>(header.width)
					<< io::write<uint16>(header.height)
					<< io::write_range(header.mimapOffsets)
					<< io::write_range(header.mipmapLengths);
			}
		}
	}
}
