// Copyright (C) 2019, Robin Klimonow. All rights reserved.

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
