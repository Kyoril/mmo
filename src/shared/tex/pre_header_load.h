// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once


namespace io
{
	class Reader;
}

namespace mmo
{
	namespace tex
	{
		struct PreHeader;


		bool loadPreHeader(PreHeader &preHeader, io::Reader &reader);
	}
}
