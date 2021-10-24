// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once


namespace io
{
	class Reader;
}

namespace mmo
{
	namespace hpak
	{
		struct PreHeader;


		bool loadPreHeader(PreHeader &preHeader, io::Reader &reader);
	}
}
