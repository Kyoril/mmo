// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "foliage_edit_mode.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "game_common/world_entity_loader.h"
#include "math/angle.h"
#include "math/quaternion.h"
#include "math/ray.h"
#include "scene_graph/camera.h"
#include "scene_graph/instanced_foliage.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene_node.h"
#include "terrain/constants.h"
#include "terrain/terrain.h"

namespace mmo
{
	namespace
	{
		constexpr float k2Pi = 6.28318530718f;
		constexpr float kRadToDeg = 57.2957795131f;
	}

	FoliageEditMode::FoliageEditMode(IWorldEditor& worldEditor, InstancedFoliage& foliage, terrain::Terrain* terrain, Camera& camera)
		: WorldEditMode(worldEditor)
		, m_foliage(foliage)
		, m_terrain(terrain)
		, m_camera(camera)
	{
		m_brushCircle = m_worldEditor.CreateManualRenderObject("FoliageBrushCircle");
		if (m_brushCircle)
		{
			m_brushCircle->SetCastShadows(false);
			m_brushCircle->SetQueryFlags(0);
		}

		m_brushCircleNode = m_worldEditor.CreateChildSceneNode();
		if (m_brushCircleNode && m_brushCircle)
		{
			m_brushCircleNode->AttachObject(*m_brushCircle);
		}
	}

	FoliageEditMode::~FoliageEditMode()
	{
		if (m_brushCircle)
		{
			m_worldEditor.DestroyManualRenderObject(*m_brushCircle);
			m_brushCircle = nullptr;
		}
		if (m_brushCircleNode)
		{
			m_worldEditor.DestroySceneNode(*m_brushCircleNode);
			m_brushCircleNode = nullptr;
		}
	}

	const char* FoliageEditMode::GetName() const
	{
		static const char* s_name = "Foliage";
		return s_name;
	}

	uint16 FoliageEditMode::PageIndexFromPosition(const Vector3& position)
	{
		const auto px = static_cast<uint32>(std::floor(position.x / terrain::constants::PageSize)) + 32;
		const auto py = static_cast<uint32>(std::floor(position.z / terrain::constants::PageSize)) + 32;
		return static_cast<uint16>((px << 8) | py);
	}

	void FoliageEditMode::OnActivate()
	{
		UpdateBrushOverlay();
	}

	void FoliageEditMode::OnDeactivate()
	{
		m_brushPositionValid = false;
		m_leftDown = false;
		m_hasLastPaint = false;
		UpdateBrushOverlay();
	}

	bool FoliageEditMode::UpdateBrushPosition(const float viewportX, const float viewportY)
	{
		if (!m_terrain)
		{
			m_brushPositionValid = false;
			return false;
		}

		const Ray ray = m_camera.GetCameraToViewportRay(viewportX, viewportY, 10000.0f);
		const auto hitResult = m_terrain->RayIntersects(ray);
		if (!hitResult.first)
		{
			m_brushPositionValid = false;
			return false;
		}

		m_brushPosition = hitResult.second.position;
		m_brushPositionValid = true;
		return true;
	}

	void FoliageEditMode::OnMouseMoved(const float x, const float y)
	{
		UpdateBrushPosition(x, y);
		UpdateBrushOverlay();
	}

	void FoliageEditMode::OnMouseDown(const float x, const float y)
	{
		m_leftDown = true;

		if (m_brushMode == FoliageBrushMode::Select)
		{
			SelectAt(x, y);
			return;
		}

		UpdateBrushPosition(x, y);
		m_hasLastPaint = false;

		if (m_brushMode == FoliageBrushMode::Paint)
		{
			ApplyPaint();
		}
		else if (m_brushMode == FoliageBrushMode::Erase)
		{
			ApplyErase();
		}
	}

	void FoliageEditMode::OnMouseHold(const float deltaSeconds)
	{
		if (!m_leftDown)
		{
			return;
		}

		if (m_brushMode == FoliageBrushMode::Paint)
		{
			ApplyPaint();
		}
		else if (m_brushMode == FoliageBrushMode::Erase)
		{
			ApplyErase();
		}
	}

	void FoliageEditMode::OnMouseUp(const float x, const float y)
	{
		m_leftDown = false;
		m_hasLastPaint = false;
		m_foliage.RebuildDirtyCells();
	}

	void FoliageEditMode::OnMouseWheel(const float delta)
	{
		m_brushRadius = std::clamp(m_brushRadius + delta * 2.0f, 1.0f, 256.0f);
		UpdateBrushOverlay();
	}

	void FoliageEditMode::ApplyPaint()
	{
		if (!m_brushPositionValid || !m_terrain || m_meshPath[0] == '\0')
		{
			return;
		}

		// Throttle continuous painting: only deposit a new dab once the brush has travelled a
		// meaningful fraction of its radius since the previous one.
		if (m_hasLastPaint)
		{
			const float threshold = m_brushRadius * 0.5f;
			if ((m_brushPosition - m_lastPaintPosition).GetSquaredLength() < threshold * threshold)
			{
				return;
			}
		}
		m_lastPaintPosition = m_brushPosition;
		m_hasLastPaint = true;

		std::uniform_real_distribution<float> u01(0.0f, 1.0f);

		const float area = 3.14159265f * m_brushRadius * m_brushRadius;
		const int count = std::max(1, static_cast<int>(area * m_density));

		const String meshName = m_meshPath;

		for (int i = 0; i < count; ++i)
		{
			const float angle = u01(m_rng) * k2Pi;
			const float radius = std::sqrt(u01(m_rng)) * m_brushRadius;
			const float x = m_brushPosition.x + std::cos(angle) * radius;
			const float z = m_brushPosition.z + std::sin(angle) * radius;

			if (m_terrain->IsHoleAt(x, z))
			{
				continue;
			}

			const Vector3 normal = m_terrain->GetSmoothNormalAt(x, z);
			const float slope = std::acos(std::clamp(normal.y, -1.0f, 1.0f)) * kRadToDeg;
			if (slope > m_maxSlopeAngle)
			{
				continue;
			}

			const float height = m_terrain->GetSmoothHeightAt(x, z);
			const Vector3 position(x, height, z);

			// Enforce a minimum spacing so instances don't clump together.
			if (m_minSpacing > 0.0f)
			{
				std::vector<uint64> nearby;
				m_foliage.QueryInstancesInRadius(position, m_minSpacing, nearby);
				if (!nearby.empty())
				{
					continue;
				}
			}

			Quaternion rotation = Quaternion::Identity;
			if (m_alignToNormal)
			{
				rotation = Vector3::UnitY.GetRotationTo(normal);
			}
			if (m_randomYaw)
			{
				rotation = rotation * Quaternion(Radian(u01(m_rng) * k2Pi), Vector3::UnitY);
			}

			const float scale = m_minScale + u01(m_rng) * (m_maxScale - m_minScale);

			InstancedFoliageInstance instance;
			instance.uniqueId = GenerateUniqueId();
			instance.meshName = meshName;
			instance.position = position;
			instance.rotation = rotation;
			instance.scale = Vector3(scale, scale, scale);
			instance.pageIndex = PageIndexFromPosition(position);

			if (m_foliage.AddInstance(instance))
			{
				m_dirtyPages.insert(instance.pageIndex);
			}
		}

		m_foliage.RebuildDirtyCells();
	}

	void FoliageEditMode::ApplyErase()
	{
		if (!m_brushPositionValid)
		{
			return;
		}

		std::vector<uint64> ids;
		m_foliage.QueryInstancesInRadius(m_brushPosition, m_brushRadius, ids);

		for (const uint64 id : ids)
		{
			InstancedFoliageInstance instance;
			if (m_foliage.TryGetInstance(id, instance))
			{
				m_dirtyPages.insert(instance.pageIndex);
			}

			if (id == m_selectedInstance)
			{
				m_selectedInstance = 0;
			}

			m_foliage.RemoveInstance(id);
		}

		if (!ids.empty())
		{
			m_foliage.RebuildDirtyCells();
		}
	}

	void FoliageEditMode::SelectAt(const float viewportX, const float viewportY)
	{
		const Ray ray = m_camera.GetCameraToViewportRay(viewportX, viewportY, 10000.0f);
		float distance = 0.0f;
		m_selectedInstance = m_foliage.Raycast(ray, distance);
	}

	void FoliageEditMode::DeleteSelected()
	{
		if (m_selectedInstance == 0)
		{
			return;
		}

		InstancedFoliageInstance instance;
		if (m_foliage.TryGetInstance(m_selectedInstance, instance))
		{
			m_dirtyPages.insert(instance.pageIndex);
		}

		m_foliage.RemoveInstance(m_selectedInstance);
		m_selectedInstance = 0;
		m_foliage.RebuildDirtyCells();
	}

	void FoliageEditMode::OnViewportDrop(const float x, const float y)
	{
		WorldEditMode::OnViewportDrop(x, y);

		// Dropping a mesh selects it as the active brush mesh and immediately places one instance
		// at the drop location for instant feedback.
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmsh");
		if (!payload || !payload->Data)
		{
			return;
		}

		const String meshName = *static_cast<String*>(payload->Data);
#ifdef _MSC_VER
		strncpy_s(m_meshPath, meshName.c_str(), _TRUNCATE);
#else
		std::strncpy(m_meshPath, meshName.c_str(), sizeof(m_meshPath) - 1);
		m_meshPath[sizeof(m_meshPath) - 1] = '\0';
#endif

		if (!UpdateBrushPosition(x, y))
		{
			return;
		}

		InstancedFoliageInstance instance;
		instance.uniqueId = GenerateUniqueId();
		instance.meshName = meshName;
		instance.position = m_brushPosition;
		instance.rotation = Quaternion::Identity;
		instance.scale = Vector3::UnitScale;
		instance.pageIndex = PageIndexFromPosition(m_brushPosition);

		if (m_foliage.AddInstance(instance))
		{
			m_dirtyPages.insert(instance.pageIndex);
			m_foliage.RebuildDirtyCells();
		}
	}

	void FoliageEditMode::UpdateBrushOverlay()
	{
		if (!m_brushCircle)
		{
			return;
		}

		m_brushCircle->Clear();

		if (!m_brushPositionValid || !m_terrain || m_brushMode == FoliageBrushMode::Select)
		{
			return;
		}

		if (m_brushCircleNode)
		{
			m_brushCircleNode->SetPosition(Vector3::Zero);
		}

		MaterialPtr mat = MaterialManager::Get().Load("Editor/Wireframe.hmat");
		if (!mat)
		{
			return;
		}

		auto lineOp = m_brushCircle->AddLineListOperation(mat);

		constexpr int kSegments = 64;
		const float cx = m_brushPosition.x;
		const float cz = m_brushPosition.z;
		const float r = m_brushRadius;

		for (int i = 0; i < kSegments; ++i)
		{
			const float a1 = (static_cast<float>(i) / kSegments) * k2Pi;
			const float a2 = (static_cast<float>(i + 1) / kSegments) * k2Pi;

			const float x1 = cx + r * std::cos(a1);
			const float z1 = cz + r * std::sin(a1);
			const float x2 = cx + r * std::cos(a2);
			const float z2 = cz + r * std::sin(a2);
			const float y1 = m_terrain->GetSmoothHeightAt(x1, z1) + 0.2f;
			const float y2 = m_terrain->GetSmoothHeightAt(x2, z2) + 0.2f;

			lineOp->AddLine(Vector3(x1, y1, z1), Vector3(x2, y2, z2));
		}
	}

	void FoliageEditMode::DrawViewportOverlay(ImDrawList* /*drawList*/, const ImVec2& /*viewportMin*/, const ImVec2& /*viewportSize*/)
	{
		// The brush footprint is drawn as a 3-D circle on the terrain via UpdateBrushOverlay.
	}

	void FoliageEditMode::DrawDetails()
	{
		ImGui::TextWrapped("Authoring instanced foliage (trees). These are individually placed and persisted, but rendered with hardware instancing.");
		ImGui::Separator();

		int mode = static_cast<int>(m_brushMode);
		const char* modes[] = { "Paint", "Erase", "Select" };
		if (ImGui::Combo("Brush mode", &mode, modes, static_cast<int>(std::size(modes))))
		{
			m_brushMode = static_cast<FoliageBrushMode>(mode);
			UpdateBrushOverlay();
		}

		ImGui::Separator();

		ImGui::InputText("Mesh", m_meshPath, sizeof(m_meshPath));
		ImGui::TextDisabled("Drag a .hmsh from the Asset Browser onto the viewport to set the brush mesh.");

		if (m_brushMode != FoliageBrushMode::Select)
		{
			ImGui::Separator();
			ImGui::SliderFloat("Brush radius", &m_brushRadius, 1.0f, 256.0f, "%.1f");

			if (m_brushMode == FoliageBrushMode::Paint)
			{
				ImGui::SliderFloat("Density", &m_density, 0.001f, 0.5f, "%.3f");
				ImGui::SliderFloat("Min spacing", &m_minSpacing, 0.0f, 30.0f, "%.1f");
				ImGui::SliderFloat("Min scale", &m_minScale, 0.05f, 5.0f, "%.2f");
				ImGui::SliderFloat("Max scale", &m_maxScale, 0.05f, 5.0f, "%.2f");
				if (m_maxScale < m_minScale)
				{
					m_maxScale = m_minScale;
				}
				ImGui::SliderFloat("Max slope", &m_maxSlopeAngle, 0.0f, 90.0f, "%.0f deg");
				ImGui::Checkbox("Random yaw", &m_randomYaw);
				ImGui::SameLine();
				ImGui::Checkbox("Align to normal", &m_alignToNormal);

				if (ImGui::Checkbox("Cast shadows", &m_castShadows))
				{
					m_foliage.SetCastShadows(m_castShadows);
				}
			}
		}

		// Per-instance editing for the currently selected instance.
		if (m_brushMode == FoliageBrushMode::Select)
		{
			ImGui::Separator();
			InstancedFoliageInstance instance;
			if (m_selectedInstance != 0 && m_foliage.TryGetInstance(m_selectedInstance, instance))
			{
				ImGui::Text("Selected: %llu", static_cast<unsigned long long>(m_selectedInstance));
				ImGui::TextDisabled("%s", instance.meshName.c_str());

				bool changed = false;
				float position[3] = { instance.position.x, instance.position.y, instance.position.z };
				if (ImGui::DragFloat3("Position", position, 0.1f))
				{
					instance.position = Vector3(position[0], position[1], position[2]);
					changed = true;
				}

				float uniformScale = instance.scale.x;
				if (ImGui::DragFloat("Scale", &uniformScale, 0.01f, 0.05f, 20.0f))
				{
					instance.scale = Vector3(uniformScale, uniformScale, uniformScale);
					changed = true;
				}

				if (changed)
				{
					m_dirtyPages.insert(instance.pageIndex);
					m_foliage.UpdateInstanceTransform(m_selectedInstance, instance.position, instance.rotation, instance.scale);
					// Re-tag the page in case the move crossed a page border.
					m_dirtyPages.insert(PageIndexFromPosition(instance.position));
					m_foliage.RebuildDirtyCells();
				}

				if (ImGui::Button("Delete selected") || ImGui::IsKeyPressed(ImGuiKey_Delete))
				{
					DeleteSelected();
				}
			}
			else
			{
				ImGui::TextDisabled("Click an instance in the viewport to select it.");
			}
		}

		ImGui::Separator();
		ImGui::Text("Instances: %zu", m_foliage.GetInstanceCount());
		if (HasUnsavedChanges())
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(unsaved)");
		}
	}
}
