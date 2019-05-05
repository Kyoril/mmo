// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"


namespace mmo
{
	/// Supported vertex formats of the engine.
	enum class VertexFormat
	{
		/// Only position
		Pos,
		/// Position + color
		PosColor,
		/// Position + color + 1 texture
		PosColorTex1,
		/// Position + color + normal
		PosColorNormal,
		/// Position + color + normal + 1 texture
		PosColorNormalTex1,

		/// Counter
		Last,
	};


	struct POS_VERTEX
	{
		float pos[3];
	};

	struct POS_COL_VERTEX
	{
		float pos[3];
		uint32 color;
	};

	struct POS_COL_TEX_VERTEX
	{
		float pos[3];
		uint32 color;
		float uv[2];
	};

	struct POS_COL_NORMAL_VERTEX
	{
		float pos[3];
		uint32 color;
		float normal[3];
	};

	struct POS_COL_NORMAL_TEX_VERTEX
	{
		float pos[3];
		uint32 color;
		float normal[3];
		float uv[2];
	};
}
