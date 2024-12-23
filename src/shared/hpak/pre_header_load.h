// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
