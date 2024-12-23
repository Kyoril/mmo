// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once


namespace io
{
	class Writer;
}


namespace mmo
{
	namespace hpak
	{
		struct PreHeader;


		void savePreHeader(const PreHeader &preHeader, io::Writer &writer);
	}
}
