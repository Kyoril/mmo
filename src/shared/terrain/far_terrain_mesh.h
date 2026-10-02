// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/math_utils.h"
#include "math/vector3.h"
#include "terrain/constants.h"

#include <vector>

namespace mmo
{
	namespace terrain
	{
		/// @brief Pure geometry of a distant-terrain page: the .tlod grid plus a skirt around it.
		///
		/// @remark Deliberately free of any dependency on the terrain library itself (and on terrain_io,
		///			whose grid constants it mirrors) so it compiles into the headless terrain_tests target.
		namespace far_mesh
		{
			/// @brief LOD vertices along one page side. Must match terrain_io::LodVerticesPerPageSide.
			constexpr uint32 VerticesPerSide = 33;

			/// @brief Cells along one page side.
			constexpr uint32 CellsPerSide = VerticesPerSide - 1;

			/// @brief Number of grid vertices.
			constexpr uint32 GridVertexCount = VerticesPerSide * VerticesPerSide;

			/// @brief Number of vertices on the page border, walked once around.
			constexpr uint32 PerimeterVertexCount = CellsPerSide * 4;

			/// @brief Total vertex count: the grid followed by one lowered copy of every border vertex.
			constexpr uint32 VertexCount = GridVertexCount + PerimeterVertexCount;

			/// @brief Total index count: two triangles per grid cell and per skirt segment.
			constexpr uint32 IndexCount = (CellsPerSide * CellsPerSide + PerimeterVertexCount) * 6;

			/// @brief Distance between two grid vertices in world units.
			constexpr float CellSize = static_cast<float>(constants::PageSize / static_cast<double>(CellsPerSide));

			static_assert(VertexCount <= 65536, "Far pages use 16 bit indices");

			struct Vertex
			{
				/// @brief Page-local position (the page origin is its minimum corner).
				Vector3 position;
				Vector3 normal;
				float u;
				float v;
			};

			/// @brief Grid coordinate of the k-th border vertex, walking (0,0) -> +X -> +Z -> -X -> -Z.
			inline void PerimeterCoord(const uint32 k, uint32& i, uint32& j)
			{
				const uint32 side = k / CellsPerSide;
				const uint32 step = k % CellsPerSide;
				switch (side)
				{
				case 0:		i = step;					j = 0;						break;
				case 1:		i = CellsPerSide;			j = step;					break;
				case 2:		i = CellsPerSide - step;	j = CellsPerSide;			break;
				default:	i = 0;						j = CellsPerSide - step;	break;
				}
			}

			/// @brief Builds the vertices of one far page.
			/// @param heights VerticesPerSide² page-local heights, index i + j * VerticesPerSide.
			/// @param normals VerticesPerSide² encoded normals, same indexing.
			/// @param skirtDepth How far the skirt hangs below the border, hiding cracks to full-resolution neighbours.
			/// @param out Receives VertexCount vertices.
			inline void BuildVertices(const float* heights, const EncodedNormal8* normals, const float skirtDepth, std::vector<Vertex>& out)
			{
				out.resize(VertexCount);

				for (uint32 j = 0; j < VerticesPerSide; ++j)
				{
					for (uint32 i = 0; i < VerticesPerSide; ++i)
					{
						const uint32 index = i + j * VerticesPerSide;
						Vertex& v = out[index];
						v.position = Vector3(static_cast<float>(i) * CellSize, heights[index], static_cast<float>(j) * CellSize);
						DecodeNormalSNorm8(normals[index], v.normal.x, v.normal.y, v.normal.z);
						v.u = static_cast<float>(i) / static_cast<float>(CellsPerSide);
						v.v = static_cast<float>(j) / static_cast<float>(CellsPerSide);
					}
				}

				for (uint32 k = 0; k < PerimeterVertexCount; ++k)
				{
					uint32 i, j;
					PerimeterCoord(k, i, j);

					Vertex& skirt = out[GridVertexCount + k];
					skirt = out[i + j * VerticesPerSide];
					skirt.position.y -= skirtDepth;
				}
			}

			/// @brief Builds the (constant) index list shared by every far page.
			/// @details Triangles wind counter-clockwise seen from above (cross(b - a, c - a) points up for
			///	         the grid, outward for the skirt), like terrain tiles.
			inline void BuildIndices(std::vector<uint16>& out)
			{
				out.clear();
				out.reserve(IndexCount);

				for (uint32 j = 0; j < CellsPerSide; ++j)
				{
					for (uint32 i = 0; i < CellsPerSide; ++i)
					{
						const uint16 tl = static_cast<uint16>(i + j * VerticesPerSide);
						const uint16 tr = static_cast<uint16>(tl + 1);
						const uint16 bl = static_cast<uint16>(tl + VerticesPerSide);
						const uint16 br = static_cast<uint16>(bl + 1);

						out.insert(out.end(), { tl, bl, tr });
						out.insert(out.end(), { tr, bl, br });
					}
				}

				for (uint32 k = 0; k < PerimeterVertexCount; ++k)
				{
					const uint32 next = (k + 1) % PerimeterVertexCount;

					uint32 i0, j0, i1, j1;
					PerimeterCoord(k, i0, j0);
					PerimeterCoord(next, i1, j1);

					const uint16 p0 = static_cast<uint16>(i0 + j0 * VerticesPerSide);
					const uint16 p1 = static_cast<uint16>(i1 + j1 * VerticesPerSide);
					const uint16 s0 = static_cast<uint16>(GridVertexCount + k);
					const uint16 s1 = static_cast<uint16>(GridVertexCount + next);

					out.insert(out.end(), { p0, p1, s0 });
					out.insert(out.end(), { p1, s1, s0 });
				}
			}
		}
	}
}
