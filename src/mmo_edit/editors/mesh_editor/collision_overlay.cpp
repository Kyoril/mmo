// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "collision_overlay.h"

#include "math/collision_shape.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/render_queue.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	namespace
	{
		// ARGB, translucent like the other editor overlays.
		constexpr uint32 WalkableColor = 0x5040D040u;       // green
		constexpr uint32 SteepColor = 0x50FF8C1Au;          // orange: too steep to walk on
		constexpr uint32 ShapeWalkableColor = 0x504090FFu;  // blue: walkable face of an authored shape
		constexpr uint32 CutColor = 0x50FF3030u;            // red: render face removed by a Cut shape
		constexpr uint32 SelectedColor = 0x70FFE030u;       // yellow: selected shape
		constexpr uint32 EdgeColor = 0xA0FFFFFFu;

		/// Darkens a colour by how far the face turns away from a fixed light, so that the faces of
		/// adjacent boxes stay apart without edge lines.
		uint32 ShadeByNormal(const uint32 color, const Vector3& a, const Vector3& b, const Vector3& c)
		{
			Vector3 normal = (b - a).Cross(c - a);
			const float length = normal.GetLength();
			if (length <= 0.0f)
			{
				return color;
			}

			normal /= length;
			const Vector3 light = Vector3(0.4f, 0.8f, 0.3f).NormalizedCopy();
			const float factor = 0.55f + 0.45f * std::abs(normal.Dot(light));
			const auto channel = [&](const uint32 shift)
			{
				return static_cast<uint32>(static_cast<float>((color >> shift) & 0xFFu) * factor) << shift;
			};
			return (color & 0xFF000000u) | channel(16) | channel(8) | channel(0);
		}

		/// ARGB to ABGR, for materials that read vertex colours the way Recast/Detour debug drawing packs them.
		uint32 ToAbgr(const uint32 color)
		{
			return (color & 0xFF00FF00u) | ((color >> 16) & 0xFFu) | ((color & 0xFFu) << 16);
		}
	}

	CollisionOverlay::CollisionOverlay(Scene& scene, SceneNode& parent)
		: m_scene(scene)
	{
		m_node = parent.CreateChildSceneNode();
		m_faces = m_scene.CreateManualRenderObject("CollisionOverlay");
		m_faces->SetCastShadows(false);
		m_faces->SetQueryFlags(0);
		m_faces->SetRenderQueueGroup(Overlay);
		m_node->AttachObject(*m_faces);
	}

	CollisionOverlay::~CollisionOverlay()
	{
		if (m_faces)
		{
			m_scene.DestroyManualRenderObject(*m_faces);
		}

		if (m_node)
		{
			m_scene.DestroySceneNode(*m_node);
		}
	}

	void CollisionOverlay::Rebuild(const CollisionOverlayData& data)
	{
		m_faces->Clear();
		if (!data.vertices || !data.indices)
		{
			return;
		}

		const MaterialPtr faceMaterial = MaterialManager::Get().Load(data.faceMaterial);
		const MaterialPtr edgeMaterial = MaterialManager::Get().Load("Editor/Wireframe.hmat");
		const auto& vertices = *data.vertices;
		const auto& indices = *data.indices;

		if (faceMaterial)
		{
			auto triangles = m_faces->AddTriangleListOperation(faceMaterial);
			for (size_t f = 0; f + 2 < indices.size(); f += 3)
			{
				const Vector3& a = vertices[indices[f]];
				const Vector3& b = vertices[indices[f + 1]];
				const Vector3& c = vertices[indices[f + 2]];
				const int32 shape = data.faceShape && f / 3 < data.faceShape->size() ? (*data.faceShape)[f / 3] : -1;
				const bool walkable = IsCollisionFaceWalkable(a, b, c);

				uint32 color = walkable ? (shape >= 0 ? ShapeWalkableColor : WalkableColor) : SteepColor;
				if (shape >= 0 && shape == data.selectedShape)
				{
					color = SelectedColor;
				}

				if (data.shadeByNormal)
				{
					color = ShadeByNormal(color, a, b, c);
				}
				if (data.abgrColors)
				{
					color = ToAbgr(color);
				}

				// Both windings: collision is two-sided for the player, and the camera is often inside it.
				if (data.surfaceOffset > 0.0f)
				{
					// Lift each winding off the face along its own normal, so depth-tested faces that lie
					// exactly on render geometry (floors, walls) are not lost to z-fighting.
					Vector3 normal = (b - a).Cross(c - a);
					const float length = normal.GetLength();
					const Vector3 offset = length > 0.0f ? normal * (data.surfaceOffset / length) : Vector3::Zero;
					triangles->AddTriangle(a + offset, b + offset, c + offset).SetColor(color);
					triangles->AddTriangle(a - offset, c - offset, b - offset).SetColor(color);
				}
				else
				{
					triangles->AddTriangle(a, b, c).SetColor(color);
					triangles->AddTriangle(a, c, b).SetColor(color);
				}
			}

			for (size_t i = 0; i + 2 < data.cutTriangles.size(); i += 3)
			{
				triangles->AddTriangle(data.cutTriangles[i], data.cutTriangles[i + 1], data.cutTriangles[i + 2]).SetColor(CutColor);
				triangles->AddTriangle(data.cutTriangles[i], data.cutTriangles[i + 2], data.cutTriangles[i + 1]).SetColor(CutColor);
			}
		}

		if (data.wireframe && edgeMaterial)
		{
			auto lines = m_faces->AddLineListOperation(edgeMaterial);
			for (size_t f = 0; f + 2 < indices.size(); f += 3)
			{
				const Vector3& a = vertices[indices[f]];
				const Vector3& b = vertices[indices[f + 1]];
				const Vector3& c = vertices[indices[f + 2]];
				lines->AddLine(a, b).SetColor(EdgeColor);
				lines->AddLine(b, c).SetColor(EdgeColor);
				lines->AddLine(c, a).SetColor(EdgeColor);
			}
		}
	}

	void CollisionOverlay::SetVisible(const bool visible)
	{
		m_faces->SetVisible(visible);
	}
}
