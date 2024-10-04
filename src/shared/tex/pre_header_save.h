// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once


namespace io
{
	class Writer;
}


namespace mmo
{
	namespace tex
	{
		struct PreHeader;


		void savePreHeader(const PreHeader &preHeader, io::Writer &writer);
	}
}
