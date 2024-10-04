// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "tex/magic.h"
#include "magic.h"


namespace io
{
	struct ISink;
}


namespace mmo
{
	namespace tex
	{
		namespace v1_0
		{
			struct Header;

			struct HeaderSaver
			{
				explicit HeaderSaver(io::ISink &destination, const Header& header);
				~HeaderSaver();

				void finish();

			private:

				io::ISink &m_destination;
				const Header& m_header;
				std::size_t m_mipPosition;
				std::size_t m_contentPosition;
#ifdef _DEBUG
				bool m_finished;
#endif
			};
		}
	}
}
