// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

namespace io
{
	class Reader;
}


namespace mmo
{
	namespace mesh
	{
		namespace v1_0
		{
			struct Header;


			bool LoadHeader(Header &header, io::Reader &reader);
		}
	}
}
