// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

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
		
		/// Position + color + normal + binormal + tangent + 1 texture
		PosColorNormalBinormalTangentTex1,

		/// Counter
		Last,
	};


	struct POS_VERTEX
	{
		Vector3 pos;
	};

	struct POS_COL_VERTEX
	{
		Vector3 pos;
		uint32 color;
	};

	struct POS_COL_TEX_VERTEX
	{
		Vector3 pos;
		uint32 color;
		float uv[2];
	};

	struct POS_COL_NORMAL_VERTEX
	{
		Vector3 pos;
		uint32 color;
		Vector3 normal;
	};

	struct POS_COL_NORMAL_TEX_VERTEX
	{
		Vector3 pos;
		uint32 color;
		Vector3 normal;
		float uv[2];
	};
	
	struct POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX
	{
		Vector3 pos;
		uint32 color;
		Vector3 normal;
		Vector3 binormal;
		Vector3 tangent;
		float uv[2];
	};
}
