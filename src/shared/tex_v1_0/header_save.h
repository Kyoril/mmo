// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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

			/// @brief Writes a v1.0 header, leaving the mip tables to be patched by finish().
			/// @note Retains a reference to the header rather than copying it: finish() reads
			///       the mip offsets and lengths again, after the caller has filled them in
			///       from the content it wrote in between. The header must therefore outlive
			///       the saver -- never construct one from a temporary.
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
