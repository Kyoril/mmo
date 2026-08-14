// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/matrix4.h"
#include "math/vector4.h"

namespace mmo
{
	/// @brief Per-instance data for one hardware-instanced draw of a mesh.
	/// @details This structure is uploaded to the GPU as a per-instance vertex stream. Its layout is
	///          fixed by VertexDeclarationD3D11::BindInstanced, which declares a world matrix at
	///          TEXCOORD8-11 followed by a tint at TEXCOORD12, both with
	///          D3D11_INPUT_PER_INSTANCE_DATA and a step rate of one. The generated instanced vertex
	///          shader uses the matrix as the full world transform (it ignores the per-object matWorld
	///          constant entirely) and multiplies the vertex colour by the tint, so instance matrices
	///          must always be world-space.
	///
	///          Every instanced renderable in the engine shares this layout: foliage and world model
	///          batches leave the tint white, particle mesh emitters drive it from colour-over-life.
	struct MeshInstanceData
	{
		/// @brief World transform matrix for this instance (4x4 = 64 bytes).
		Matrix4 worldMatrix;

		/// @brief Per-instance tint (RGBA). Defaults to opaque white, which renders unchanged.
		Vector4 color { 1.0f, 1.0f, 1.0f, 1.0f };
	};

	static_assert(sizeof(MeshInstanceData) == 80, "MeshInstanceData size mismatch");
}
