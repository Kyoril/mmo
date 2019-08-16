// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once


namespace io
{
	class Writer;
}


namespace mmo
{
	namespace mesh
	{
		struct PreHeader;


		void savePreHeader(const PreHeader &preHeader, io::Writer &writer);
	}
}
