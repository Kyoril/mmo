// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "page_data.h"

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	namespace terrain_io
	{
		/// @brief Serializes a terrain page in the v2 .tile chunk format.
		/// @details Writes the exact chunk sequence produced by terrain::Page::Save: MVER,
		///	         MCMT, MCVT, MCIV, MCNM, MCIN, MCLY, MCVS, MCIS, optionally MCHL (only if
		///	         any hole mask is set), optionally MCWQ (only if any water quad mask is set
		///	         or a water material name is present), and MCAR.
		///	@param writer The writer to serialize into.
		///	@param page View over the page data to write. Required pointers must be set.
		///	@return true on success, false if the page view is incomplete.
		bool SavePage(io::Writer &writer, const PageDataView &page);

		/// @brief Deserializes a v2 .tile terrain page.
		/// @details Only file format version 0x02 is supported; legacy v1 pages are rejected
		///	         (the editor converts them to v2 on save). Unknown chunks are skipped.
		///	@param reader The reader to deserialize from.
		///	@param out Receives the page data. Reset() is called on it first.
		///	@return true on success, false if the data is malformed or has an unsupported version.
		bool LoadPage(io::Reader &reader, PageData &out);
	}
}
