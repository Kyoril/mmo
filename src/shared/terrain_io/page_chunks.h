// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/chunk_writer.h"

namespace mmo
{
	namespace terrain_io
	{
		/// @brief Chunk magics of the terrain page (.tile) file format.
		/// @details MakeChunkMagic stores the multi-char literal little-endian, so the
		///	         bytes on disk read reversed (e.g. 'REVM' appears as "MVER" in a hex dump).
		///	         These constants are the single source of truth shared between the
		///	         rendering terrain library (Page) and headless tools.
		inline const ChunkMagic VersionChunk = MakeChunkMagic('REVM');
		inline const ChunkMagic MaterialChunk = MakeChunkMagic('TMCM');
		inline const ChunkMagic VertexChunk = MakeChunkMagic('TVCM');
		inline const ChunkMagic NormalChunk = MakeChunkMagic('MNCM');
		inline const ChunkMagic LayerChunk = MakeChunkMagic('YLCM');
		inline const ChunkMagic AreaChunk = MakeChunkMagic('RACM');
		inline const ChunkMagic VertexShadingChunk = MakeChunkMagic('SVCM');
		inline const ChunkMagic InnerVertexChunk = MakeChunkMagic('IVCM');
		inline const ChunkMagic InnerNormalChunk = MakeChunkMagic('INCM');
		inline const ChunkMagic InnerVertexShadingChunk = MakeChunkMagic('ISCM');
		inline const ChunkMagic HoleChunk = MakeChunkMagic('LOHM');
		inline const ChunkMagic WaterChunk = MakeChunkMagic('WCLM');     // legacy v1
		inline const ChunkMagic WaterQuadChunk = MakeChunkMagic('QWCM'); // current v2
	}
}
