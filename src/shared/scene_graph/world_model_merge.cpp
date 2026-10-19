// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_merge.h"

#include "math/matrix3.h"

namespace mmo
{
	namespace
	{
		Vector3 NormalizedOr(const Vector3& v, const Vector3& fallback)
		{
			const float length = v.GetLength();
			return length > 1e-12f ? v / length : fallback;
		}
	}

	void AppendTransformedGeometry(
		const WorldModelMergeVertex* vertices,
		const size_t vertexCount,
		const uint32* indices,
		const size_t indexCount,
		const Matrix4& transform,
		std::vector<WorldModelMergeVertex>& outVertices,
		std::vector<uint32>& outIndices)
	{
		Matrix3 linear;
		transform.Extract3x3Matrix(linear);
		const Matrix3 normalMatrix = linear.Inverse().Transpose();
		const bool mirrored = transform.HasNegativeScale();

		// No exact-size reserve here: this is called once per placement on the same buffers, and an
		// exact reserve defeats the vector's geometric growth, copying the whole buffer every call.
		const uint32 base = static_cast<uint32>(outVertices.size());
		for (size_t i = 0; i < vertexCount; ++i)
		{
			WorldModelMergeVertex v = vertices[i];
			v.pos = transform * v.pos;
			v.normal = NormalizedOr(normalMatrix * v.normal, v.normal);
			v.tangent = NormalizedOr(linear * v.tangent, v.tangent);
			v.binormal = NormalizedOr(linear * v.binormal, v.binormal);
			outVertices.push_back(v);
		}

		for (size_t i = 0; i + 2 < indexCount; i += 3)
		{
			outIndices.push_back(base + indices[i]);
			if (mirrored)
			{
				outIndices.push_back(base + indices[i + 2]);
				outIndices.push_back(base + indices[i + 1]);
			}
			else
			{
				outIndices.push_back(base + indices[i + 1]);
				outIndices.push_back(base + indices[i + 2]);
			}
		}
	}
}
