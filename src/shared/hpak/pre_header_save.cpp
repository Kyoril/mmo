// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pre_header_save.h"
#include "pre_header.h"
#include "magic.h"
#include "binary_io/writer.h"
#include "base/io_array.h"


namespace mmo
{
	namespace hpak
	{
		void savePreHeader(const PreHeader &preHeader, io::Writer &writer)
		{
			static_assert((std::is_same<decltype(FileBeginMagic), const std::array<char, 4> >::value));

			writer
			        << write_array<char>(FileBeginMagic)
			        << io::write<uint32>(preHeader.version)
			        ;
		}
	}
}
