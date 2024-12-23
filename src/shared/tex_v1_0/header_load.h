// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace io
{
	class Reader;
}


namespace mmo
{
	namespace tex
	{
		namespace v1_0
		{
			struct Header;


			bool loadHeader(Header &header, io::Reader &reader);
		}
	}
}
