// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "header_save.h"
#include "binary_io/writer.h"
#include "base/io_array.h"
#include "mesh/pre_header_save.h"
#include "mesh/pre_header.h"
#include "base/macros.h"
#include "header.h"


namespace mmo
{
	namespace mesh
	{
		namespace v1_0
		{
			HeaderSaver::HeaderSaver(io::ISink &destination, const Header& header)
				: m_destination(destination)
				, m_header(header)
#ifdef _DEBUG
				, m_finished(false)
#endif
			{
				io::Writer writer(destination);
				savePreHeader(PreHeader(Version_1_0), writer);

				// TODO
			}

			HeaderSaver::~HeaderSaver()
			{
#ifdef _DEBUG
				ASSERT(m_finished);
#endif
			}

			void HeaderSaver::finish()
			{
				io::Writer writer(m_destination);

				// TODO

#ifdef _DEBUG
				m_finished = true;
#endif
			}
		}
	}
}
