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

			private:

				io::ISink &m_destination;
			};
		}
	}
}
