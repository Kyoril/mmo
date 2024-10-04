// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

namespace io
{
	class Reader;
}


namespace mmo
{
	namespace hpak
	{
		namespace v1_0
		{
			struct Header;


			bool loadHeader(Header &header, io::Reader &reader);
		}
	}
}
