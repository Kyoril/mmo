#pragma once

#include "mesh/magic.h"


namespace io
{
	struct ISink;
}


namespace mmo
{
	namespace mesh
	{
		namespace v1_0
		{
			struct Header;


			class SubMeshChunkSaver
			{
			public:
				explicit SubMeshChunkSaver(io::ISink &destination);
				void Finish();

			private:

				io::ISink &m_destination;
				size_t m_chunkSizePos;
				size_t m_contentPos;
			};

			struct HeaderSaver
			{
				explicit HeaderSaver(io::ISink &destination, const Header& header);
				~HeaderSaver();

				void Finish();

			private:

				io::ISink &m_destination;
				const Header& m_header;
#ifdef _DEBUG
				bool m_finished;
#endif
				size_t m_vertexChunkSizePos;
				size_t m_indexChunkSizePos;
			};
		}
	}
}
