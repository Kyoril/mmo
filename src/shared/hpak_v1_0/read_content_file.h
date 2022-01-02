// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <istream>
#include <sstream>
#include <memory>

#include "base/macros.h"

namespace mmo
{
	namespace hpak
	{
		namespace v1_0
		{
			struct Header;
			struct FileEntry;


			struct ContentFileReader
			{
				explicit ContentFileReader(
				    const Header &header,
				    const FileEntry &file,
				    std::istream &source
				);

				/// Gets the 
				std::istream &GetContent() { ASSERT(m_stream); return *m_stream; }

			private:

				std::stringstream m_contentStream;
				std::unique_ptr<std::istream> m_stream;
			};
		}
	}
}
