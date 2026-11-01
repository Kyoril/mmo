// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "water_edit_mode.h"
#include "editors/world_editor/flow_paths_file.h"
#include "terrain/terrain.h"
#include "terrain/page.h"
#include "terrain/constants.h"

#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <cstring>

#include "scene_graph/camera.h"
#include "scene_graph/material_manager.h"
#include "math/vector4.h"
#include "math/matrix4.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"

#include <cstdio>
#include <iterator>
#include <sstream>

namespace mmo
{
	static const char* s_waterBrushModeStrings[] = {
		"Paint",
		"Erase",
		"Flatten",
		"Ramp",
		"Raise",
		"Lower",
		"Flow",
	};

	namespace
	{
		/// Delay between the last flow edit and the recompute, in seconds.
		constexpr float FlowApplyDelay = 0.35f;

		/// Colour of a flow arrow: deep blue for still water up to white for the fastest current.
		ImU32 FlowSpeedColor(const float speed, const int alpha)
		{
			const float t = std::clamp(speed, 0.0f, 1.0f);
			const int r = static_cast<int>(40.0f + 215.0f * t * t);
			const int g = static_cast<int>(110.0f + 145.0f * t);
			return IM_COL32(r, g, 255, alpha);
		}
	}
	static_assert(std::size(s_waterBrushModeStrings) == static_cast<size_t>(WaterBrushMode::Count_),
		"String table must match WaterBrushMode::Count_");

	static const char* s_waterTypeStrings[] = {
		"None",
		"Water",
		"Ocean",
		"Lava",
		"Slime",
	};
	static_assert(std::size(s_waterTypeStrings) == static_cast<size_t>(terrain::WaterType::Count_),
		"String table must match WaterType::Count_");

	// -------------------------------------------------------------------------

	WaterEditMode::WaterEditMode(IWorldEditor& worldEditor, terrain::Terrain& terrain, Camera& camera)
		: WorldEditMode(worldEditor)
		, m_terrain(terrain)
		, m_camera(camera)
		, m_splineEditor(camera, [this](const float x, const float z) { return GetFlowPointHeight(x, z); })
	{
		m_brushCircle = m_worldEditor.CreateManualRenderObject("WaterBrushCircle");
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

	WaterEditMode::~WaterEditMode()
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

	const char* WaterEditMode::GetName() const
	{
		static const char* s_name = "Water";
		return s_name;
	}

	void WaterEditMode::DrawDetails()
	{
		ImGui::Separator();
		ImGui::Text("Water Brush");

		// Brush mode — use a distinct label to avoid ID collision with the top-level Mode combo.
		{
			int modeIdx = static_cast<int>(m_brushMode);
			if (ImGui::Combo("Brush Mode##water", &modeIdx, s_waterBrushModeStrings, static_cast<int>(WaterBrushMode::Count_)))
			{
				m_brushMode = static_cast<WaterBrushMode>(modeIdx);
				m_splineEditor.CancelDrag();
				UpdateBrushOverlay();
			}
		}

		if (m_brushMode == WaterBrushMode::Flow)
		{
			DrawFlowDetails();
			return;
		}

		ImGui::SliderFloat("Radius##water", &m_brushRadius, 1.0f, 512.0f, "%.1f");

		if (m_brushMode == WaterBrushMode::Paint)
		{
			// DragFloat allows Ctrl+Click to type an exact value.
			ImGui::DragFloat("Height##water", &m_waterHeight, 0.5f, -2000.0f, 10000.0f, "%.2f");
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("Ctrl+Click: type an exact height value.");
				ImGui::Text("Ctrl+Scroll: nudge height up/down by 0.5 per tick.");
				ImGui::Text("Ctrl+LClick in viewport: pick water height at cursor.");
				ImGui::EndTooltip();
			}

			int typeIdx = static_cast<int>(m_waterType) - 1;
			if (ImGui::Combo("Type##water", &typeIdx, s_waterTypeStrings + 1, static_cast<int>(terrain::WaterType::Count_) - 1))
			{
				m_waterType = static_cast<terrain::WaterType>(typeIdx + 1);
			}

			ImGui::Separator();
			ImGui::Text("Water Material");

			// Show what the selected liquid type resolves to through the water profile table, so
			// it is clear before painting that leaving the override blank is the normal case.
			const String resolved = terrain::Page::ResolveWaterMaterial(m_waterType);
			if (resolved.empty())
			{
				ImGui::TextDisabled("From profile: <none authored>");
			}
			else
			{
				ImGui::TextDisabled("From profile: %s", resolved.c_str());
			}

			ImGui::InputText("Override##watermat", m_materialName, sizeof(m_materialName));
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("Per-page material override. Leave blank to use the water profile's");
				ImGui::Text("material for the selected liquid type. A page that has an override");
				ImGui::Text("set uses it for every liquid type on that page.");
				ImGui::EndTooltip();
			}

			if (ImGui::Button("Apply Material to Brush Area") && m_brushPositionValid)
			{
				m_terrain.SetWaterMaterial(m_brushPosition.x, m_brushPosition.z, m_brushRadius, m_materialName);
			}
		}
		else if (m_brushMode == WaterBrushMode::Flatten)
		{
			ImGui::DragFloat("Height##water", &m_waterHeight, 0.5f, -2000.0f, 10000.0f, "%.2f");
		}
		else if (m_brushMode == WaterBrushMode::Ramp)
		{
			ImGui::DragFloat("End Height##water", &m_rampEndHeight, 0.5f, -2000.0f, 10000.0f, "%.2f");
			ImGui::TextDisabled("Start height is picked from the");
			ImGui::TextDisabled("water surface on mouse-down.");
			if (m_rampActive)
			{
				ImGui::TextDisabled("Start: %.2f  End: %.2f", m_rampStartHeight, m_rampEndHeight);
			}
		}
		else if (m_brushMode == WaterBrushMode::Raise || m_brushMode == WaterBrushMode::Lower)
		{
			ImGui::DragFloat("Speed##water", &m_raiseLowerSpeed, 0.05f, 0.1f, 50.0f, "%.2f");
		}

		// Water height at cursor (all modes)
		if (m_brushPositionValid)
		{
			if (m_terrain.HasWaterAtWorldPos(m_brushPosition.x, m_brushPosition.z))
			{
				const float existingH = m_terrain.GetWaterHeightAtWorldPos(m_brushPosition.x, m_brushPosition.z);
				ImGui::TextDisabled("Water height: %.2f", existingH);
			}
			else
			{
				ImGui::TextDisabled("Water height: (none)");
			}
		}

		ImGui::Separator();
		if (m_brushPositionValid)
		{
			ImGui::Text("Brush: (%.1f, %.1f, %.1f)", m_brushPosition.x, m_brushPosition.y, m_brushPosition.z);
		}
		else
		{
			ImGui::TextDisabled("Brush: (no hit)");
		}
	}

	void WaterEditMode::OnMouseDown(float x, float y)
	{
		WorldEditMode::OnMouseDown(x, y);

		if (m_brushMode == WaterBrushMode::Flow)
		{
			FlowMouseDown();
			return;
		}

		if (m_brushPositionValid)
		{
			if (ImGui::GetIO().KeyCtrl)
			{
				// Ctrl+click: pick water or terrain height into the active height field.
				const float picked = m_terrain.HasWaterAtWorldPos(m_brushPosition.x, m_brushPosition.z)
				                   ? m_terrain.GetWaterHeightAtWorldPos(m_brushPosition.x, m_brushPosition.z)
				                   : m_brushPosition.y;
				if (m_brushMode == WaterBrushMode::Ramp)
				{
					m_rampEndHeight = picked;
				}
				else
				{
					m_waterHeight = picked;
				}
			}
			else if (m_brushMode == WaterBrushMode::Ramp)
			{
				// Begin ramp: record start position and pick start height from water surface.
				m_rampStartPos    = m_brushPosition;
				m_rampStartHeight = m_terrain.HasWaterAtWorldPos(m_brushPosition.x, m_brushPosition.z)
				                  ? m_terrain.GetWaterHeightAtWorldPos(m_brushPosition.x, m_brushPosition.z)
				                  : m_waterHeight;
				m_rampActive = true;
			}
		}
	}

	void WaterEditMode::OnMouseHold(const float deltaSeconds)
	{
		WorldEditMode::OnMouseHold(deltaSeconds);

		if (!m_brushPositionValid)
		{
			return;
		}

		if (m_brushMode == WaterBrushMode::Flow)
		{
			auto splines = GetRiverSplines();
			m_splineEditor.OnMouseMoved(m_brushPosition.x, m_brushPosition.z, splines);
			return;
		}

		// Ctrl+hold performs height picking rather than painting.
		if (ImGui::GetIO().KeyCtrl)
		{
			return;
		}

		switch (m_brushMode)
		{
		case WaterBrushMode::Paint:
			m_terrain.FillWater(m_brushPosition.x, m_brushPosition.z, m_brushRadius, m_waterHeight, m_waterType);
			if (m_materialName[0] != '\0')
			{
				m_terrain.SetWaterMaterial(m_brushPosition.x, m_brushPosition.z, m_brushRadius, m_materialName);
			}
			break;

		case WaterBrushMode::Erase:
			m_terrain.EraseWater(m_brushPosition.x, m_brushPosition.z, m_brushRadius);
			break;

		case WaterBrushMode::Flatten:
			m_terrain.FlattenWater(m_brushPosition.x, m_brushPosition.z, m_brushRadius, m_waterHeight);
			break;

		case WaterBrushMode::Ramp:
			if (m_rampActive)
			{
				m_terrain.RampWater(
					m_rampStartPos.x, m_rampStartPos.z,
					m_brushPosition.x, m_brushPosition.z,
					m_brushRadius,
					m_rampStartHeight, m_rampEndHeight);
			}
			break;

		case WaterBrushMode::Raise:
			m_terrain.RaiseWater(m_brushPosition.x, m_brushPosition.z, m_brushRadius, m_raiseLowerSpeed * deltaSeconds);
			break;

		case WaterBrushMode::Lower:
			m_terrain.LowerWater(m_brushPosition.x, m_brushPosition.z, m_brushRadius, m_raiseLowerSpeed * deltaSeconds);
			break;

		default:
			break;
		}
	}

	void WaterEditMode::OnMouseMoved(float x, float y)
	{
		WorldEditMode::OnMouseMoved(x, y);

		m_brushPositionValid = false;
		UpdateBrushOverlay();
	}

	void WaterEditMode::OnMouseUp(float x, float y)
	{
		WorldEditMode::OnMouseUp(x, y);
		m_rampActive = false;

		if (m_splineEditor.IsDragging())
		{
			const bool moved = m_splineEditor.DragMoved();
			m_splineEditor.OnMouseUp();
			if (moved)
			{
				ScheduleFlowApply();
			}
		}
	}

	void WaterEditMode::OnMouseWheel(const float delta)
	{
		if (m_brushMode == WaterBrushMode::Flow)
		{
			// Shift+wheel widens or narrows the channel at the selected point.
			if (ImGui::GetIO().KeyShift && m_splineEditor.GetSelectedSpline() >= 0 && m_splineEditor.GetSelectedSpline() < static_cast<int32>(m_rivers.size()))
			{
				auto& path = m_rivers[m_splineEditor.GetSelectedSpline()].path.points;
				if (m_splineEditor.GetSelectedPoint() >= 0 && m_splineEditor.GetSelectedPoint() < static_cast<int32>(path.size()))
				{
					auto& point = path[m_splineEditor.GetSelectedPoint()];
					point.width = std::clamp(point.width * (delta > 0.0f ? 1.1f : 1.0f / 1.1f), 1.0f, 500.0f);
					ScheduleFlowApply();
				}
			}
			return;
		}

		if (ImGui::GetIO().KeyShift)
		{
			m_brushRadius = std::max(1.0f, std::min(m_brushRadius + delta * 4.0f, 512.0f));
			UpdateBrushOverlay();
		}
		else if (ImGui::GetIO().KeyCtrl && m_brushMode == WaterBrushMode::Paint)
		{
			m_waterHeight += delta * 0.5f;
		}
	}

	void WaterEditMode::DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize)
	{
		if (!drawList)
		{
			return;
		}

		const Matrix4 viewProj = m_camera.GetProjectionMatrix() * m_camera.GetViewMatrix();

		auto Project = [&](const Vector3& wp, ImVec2& sp) -> bool
		{
			const Vector4 clip = viewProj * Vector4(wp, 1.0f);
			if (clip.w <= 0.0f)
			{
				return false;
			}
			const float ndcX = clip.x / clip.w;
			const float ndcY = clip.y / clip.w;
			sp.x = viewportMin.x + (ndcX * 0.5f + 0.5f) * viewportSize.x;
			sp.y = viewportMin.y + (1.0f - (ndcY * 0.5f + 0.5f)) * viewportSize.y;
			return true;
		};

		m_overlayViewProj = viewProj;
		m_overlayViewportMin = viewportMin;
		m_overlayViewportSize = viewportSize;

		drawList->PushClipRect(viewportMin,
		                       ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y),
		                       true);

		if (m_brushMode == WaterBrushMode::Flow)
		{
			DrawFlowOverlay(*drawList);
			drawList->PopClipRect();
			return;
		}

		const uint32 numPagesX = m_terrain.GetWidth();
		const uint32 numPagesZ = m_terrain.GetHeight();
		const float halfW = static_cast<float>(numPagesX * terrain::constants::PageSize) * 0.5f;
		const float halfH = static_cast<float>(numPagesZ * terrain::constants::PageSize) * 0.5f;
		const float tileSize = static_cast<float>(terrain::constants::TileSize);
		const float quadSize = tileSize / 8.0f;

		constexpr ImU32 kWireColor = IM_COL32(0, 180, 255, 200);
		constexpr float kHeightBias = 0.1f;

		for (uint32 px = 0; px < numPagesX; ++px)
		{
			for (uint32 pz = 0; pz < numPagesZ; ++pz)
			{
				terrain::Page* page = m_terrain.GetPage(px, pz);
				if (!page || !page->IsLoaded())
				{
					continue;
				}

				const float pageOriginX = static_cast<float>(px) * static_cast<float>(terrain::constants::PageSize) - halfW;
				const float pageOriginZ = static_cast<float>(pz) * static_cast<float>(terrain::constants::PageSize) - halfH;

				for (uint32 tx = 0; tx < terrain::constants::TilesPerPage; ++tx)
				{
					for (uint32 tz = 0; tz < terrain::constants::TilesPerPage; ++tz)
					{
						const uint64 mask = page->GetWaterQuadMask(tx, tz);
						if (mask == 0)
						{
							continue;
						}

						for (uint32 qz = 0; qz < 8; ++qz)
						{
							for (uint32 qx = 0; qx < 8; ++qx)
							{
								if (!(mask & (1ULL << (qx + qz * 8))))
								{
									continue;
								}

								const uint32 pvx = tx * 8 + qx;
								const uint32 pvz = tz * 8 + qz;

								const float wx0 = pageOriginX + static_cast<float>(pvx) * quadSize;
								const float wz0 = pageOriginZ + static_cast<float>(pvz) * quadSize;
								const float wx1 = wx0 + quadSize;
								const float wz1 = wz0 + quadSize;

								const float h00 = page->GetWaterVertexHeight(pvx,     pvz    ) + kHeightBias;
								const float h10 = page->GetWaterVertexHeight(pvx + 1, pvz    ) + kHeightBias;
								const float h01 = page->GetWaterVertexHeight(pvx,     pvz + 1) + kHeightBias;
								const float h11 = page->GetWaterVertexHeight(pvx + 1, pvz + 1) + kHeightBias;

								ImVec2 s00, s10, s01, s11;
								const bool v00 = Project({wx0, h00, wz0}, s00);
								const bool v10 = Project({wx1, h10, wz0}, s10);
								const bool v01 = Project({wx0, h01, wz1}, s01);
								const bool v11 = Project({wx1, h11, wz1}, s11);

								if (v00 && v10) { drawList->AddLine(s00, s10, kWireColor, 1.0f); }
								if (v10 && v11) { drawList->AddLine(s10, s11, kWireColor, 1.0f); }
								if (v11 && v01) { drawList->AddLine(s11, s01, kWireColor, 1.0f); }
								if (v01 && v00) { drawList->AddLine(s01, s00, kWireColor, 1.0f); }
							}
						}
					}
				}
			}
		}

		drawList->PopClipRect();
	}

	void WaterEditMode::SetBrushPosition(const Vector3& position)
	{
		m_brushPosition = position;
		m_brushPositionValid = true;
		UpdateBrushOverlay();
	}

	void WaterEditMode::ClearBrushPosition()
	{
		m_brushPositionValid = false;
		UpdateBrushOverlay();
	}

	void WaterEditMode::UpdateBrushOverlay()
	{
		if (!m_brushCircle)
		{
			return;
		}

		m_brushCircle->Clear();

		// The flow mode draws its own path handles instead of a brush.
		if (!m_brushPositionValid || m_brushMode == WaterBrushMode::Flow)
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

		constexpr int   kSegments = 64;
		constexpr float k2Pi      = 6.28318530718f;

		const float cx = m_brushPosition.x;
		const float cz = m_brushPosition.z;
		const float r  = m_brushRadius;

		for (int i = 0; i < kSegments; ++i)
		{
			const float a1 = (static_cast<float>(i)     / kSegments) * k2Pi;
			const float a2 = (static_cast<float>(i + 1) / kSegments) * k2Pi;

			const float x1 = cx + r * std::cos(a1);
			const float z1 = cz + r * std::sin(a1);
			const float x2 = cx + r * std::cos(a2);
			const float z2 = cz + r * std::sin(a2);
			const float y1 = m_terrain.GetSmoothHeightAt(x1, z1) + 0.2f;
			const float y2 = m_terrain.GetSmoothHeightAt(x2, z2) + 0.2f;

			lineOp->AddLine(Vector3(x1, y1, z1), Vector3(x2, y2, z2));
		}
	}

	void WaterEditMode::ApplyBrush()
	{
		// Driven by OnMouseHold; kept for future single-click action.
	}

	void WaterEditMode::DrawFlowDetails()
	{
		ImGui::TextWrapped("Draw each river as a path from source to mouth. The water follows the nearest path; its speed falls off beyond the channel.");
		ImGui::TextDisabled("Click: add a point after the selected one");
		ImGui::TextDisabled("Drag a point: move it");
		ImGui::TextDisabled("Alt+Click a segment: insert a point");
		ImGui::TextDisabled("Ctrl+Click a point / Del: delete it");
		ImGui::TextDisabled("Shift+Wheel: channel width at the selected point");

		ImGui::Separator();
		ImGui::Text("Rivers");

		if (ImGui::Button("New River"))
		{
			River river;
			river.name = "River " + std::to_string(m_rivers.size() + 1);
			m_rivers.push_back(std::move(river));
			m_splineEditor.Select(static_cast<int32>(m_rivers.size()) - 1, -1);
			m_flowFileDirty = true;
		}

		const bool hasRiver = m_splineEditor.GetSelectedSpline() >= 0 && m_splineEditor.GetSelectedSpline() < static_cast<int32>(m_rivers.size());
		ImGui::SameLine();
		ImGui::BeginDisabled(!hasRiver);
		if (ImGui::Button("Reverse"))
		{
			auto& path = m_rivers[m_splineEditor.GetSelectedSpline()].path.points;
			std::reverse(path.begin(), path.end());
			if (m_splineEditor.GetSelectedPoint() >= 0)
			{
				m_splineEditor.Select(m_splineEditor.GetSelectedSpline(), static_cast<int32>(path.size()) - 1 - m_splineEditor.GetSelectedPoint());
			}
			ScheduleFlowApply();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Make the selected river flow the other way.");
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete River"))
		{
			m_rivers.erase(m_rivers.begin() + m_splineEditor.GetSelectedSpline());
			m_splineEditor.Select(m_rivers.empty() ? -1 : std::min(m_splineEditor.GetSelectedSpline(), static_cast<int32>(m_rivers.size()) - 1), -1);
			ScheduleFlowApply();
		}
		ImGui::EndDisabled();

		if (ImGui::BeginListBox("##rivers", ImVec2(-FLT_MIN, 5.0f * ImGui::GetTextLineHeightWithSpacing())))
		{
			for (int32 i = 0; i < static_cast<int32>(m_rivers.size()); ++i)
			{
				const River& river = m_rivers[i];
				float length = 0.0f;
				for (size_t p = 1; p < river.path.points.size(); ++p)
				{
					length += std::hypot(river.path.points[p].x - river.path.points[p - 1].x, river.path.points[p].z - river.path.points[p - 1].z);
				}

				std::ostringstream label;
				label << river.name << "  (" << river.path.points.size() << " points, " << static_cast<int32>(length) << " m)##river" << i;
				if (ImGui::Selectable(label.str().c_str(), i == m_splineEditor.GetSelectedSpline()))
				{
					m_splineEditor.Select(i, river.path.points.empty() ? -1 : static_cast<int32>(river.path.points.size()) - 1);
				}
			}
			ImGui::EndListBox();
		}

		if (hasRiver)
		{
			River& river = m_rivers[m_splineEditor.GetSelectedSpline()];
			char nameBuffer[128];
			std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", river.name.c_str());
			if (ImGui::InputText("Name##river", nameBuffer, sizeof(nameBuffer)))
			{
				river.name = nameBuffer;
				m_flowFileDirty = true;
			}

			if (m_splineEditor.GetSelectedPoint() >= 0 && m_splineEditor.GetSelectedPoint() < static_cast<int32>(river.path.points.size()))
			{
				SplinePoint& point = river.path.points[m_splineEditor.GetSelectedPoint()];
				ImGui::Text("Point %d of %d", m_splineEditor.GetSelectedPoint() + 1, static_cast<int32>(river.path.points.size()));
				if (ImGui::DragFloat("Half Width##flowpoint", &point.width, 0.25f, 1.0f, 500.0f, "%.1f m"))
				{
					ScheduleFlowApply();
				}
				float position[2] = { point.x, point.z };
				if (ImGui::DragFloat2("Position (x, z)##flowpoint", position, 0.5f))
				{
					point.x = position[0];
					point.z = position[1];
					ScheduleFlowApply();
				}
				if (ImGui::Button("Delete Point"))
				{
					DeleteSelectedFlowPoint();
				}
			}
			else
			{
				ImGui::TextDisabled("Click on the water to place the river's first point.");
			}
		}

		ImGui::DragFloat("New River Width##flow", &m_newRiverHalfWidth, 0.25f, 1.0f, 500.0f, "%.1f m");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Half width of the channel given to the first point of a river.");
		}

		ImGui::Separator();
		ImGui::Text("Flow Settings");

		// The material's ripples tile once per 1/uvScale metres; a length reads better than a scale.
		float rippleLength = 1.0f / std::max(m_flowSettings.uvScale, 0.0001f);
		if (ImGui::DragFloat("Ripple Length##flow", &rippleLength, 0.25f, 2.0f, 128.0f, "%.1f m"))
		{
			m_flowSettings.uvScale = 1.0f / std::max(rippleLength, 0.5f);
			ScheduleFlowApply();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("World size of one ripple texture repeat. The default 16 m matches still water.");
		}
		if (ImGui::DragFloat("Falloff##flow", &m_flowSettings.falloff, 0.5f, 1.0f, 500.0f, "%.0f m"))
		{
			ScheduleFlowApply();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Distance beyond the channel's banks over which the current slows to the minimum speed.");
		}
		if (ImGui::SliderFloat("Min Speed##flow", &m_flowSettings.minSpeed, 0.0f, 1.0f, "%.2f"))
		{
			ScheduleFlowApply();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Speed of still water away from every channel, such as bays and ponds.");
		}

		ImGui::Separator();
		ImGui::Checkbox("Show Flow Arrows##flow", &m_flowShowArrows);
		ImGui::Checkbox("Apply Automatically##flow", &m_flowAutoApply);
		ImGui::Checkbox("Assign River Material##flow", &m_flowAssignRiverMaterial);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Pages that receive flow get %s, the water material that reads it.\nWithout it the flow is stored but not visible.", RiverWaterMaterial);
		}

		if (ImGui::Button("Apply Flow Now"))
		{
			ApplyFlow();
		}

		ImGui::SliderFloat("Clear Radius##flow", &m_brushRadius, 1.0f, 512.0f, "%.1f");
		if (ImGui::Button("Clear Flow Around Cursor Position") && m_brushPositionValid)
		{
			const uint32 cleared = m_terrain.ClearWaterFlow(m_brushPosition.x, m_brushPosition.z, m_brushRadius);
			m_flowStatus = "Cleared flow on " + std::to_string(cleared) + " page(s).";
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Removes flow from whole pages touched by the radius, e.g. flow imported\nwith terrain_tool that no river path here describes.");
		}

		if (m_flowApplyCountdown >= 0.0f)
		{
			ImGui::TextDisabled("Applying...");
		}
		else if (!m_flowStatus.empty())
		{
			ImGui::TextDisabled("%s", m_flowStatus.c_str());
		}

		// Flow under the cursor, read back from the page so it shows what is actually stored.
		if (m_brushPositionValid)
		{
			const float halfW = static_cast<float>(m_terrain.GetWidth() * terrain::constants::PageSize) * 0.5f;
			const float halfH = static_cast<float>(m_terrain.GetHeight() * terrain::constants::PageSize) * 0.5f;
			const float pageSize = static_cast<float>(terrain::constants::PageSize);
			const float vertexSpacing = pageSize / static_cast<float>(terrain::constants::OuterVerticesPerPageSide - 1);
			const int32 pageX = static_cast<int32>(std::floor((m_brushPosition.x + halfW) / pageSize));
			const int32 pageZ = static_cast<int32>(std::floor((m_brushPosition.z + halfH) / pageSize));
			const terrain::Page* page = pageX >= 0 && pageZ >= 0 ? m_terrain.GetPage(pageX, pageZ) : nullptr;
			float u, v, speed;
			if (page && page->GetWaterFlow(
				static_cast<uint32>(std::lround((m_brushPosition.x + halfW - pageX * pageSize) / vertexSpacing)),
				static_cast<uint32>(std::lround((m_brushPosition.z + halfH - pageZ * pageSize) / vertexSpacing)), u, v, speed))
			{
				ImGui::TextDisabled("Flow at cursor: speed %.2f, page material %s", speed,
					page->GetWaterMaterialName().empty() ? "<profile>" : page->GetWaterMaterialName().c_str());
			}
			else
			{
				ImGui::TextDisabled("Flow at cursor: none (still water)");
			}
		}
	}

	void WaterEditMode::FlowMouseDown()
	{
		const ImGuiIO& io = ImGui::GetIO();
		auto splines = GetRiverSplines();

		// Ctrl+click on a point deletes it.
		int32 riverIndex, pointIndex;
		if (io.KeyCtrl && m_splineEditor.HitTestPoint(io.MousePos.x, io.MousePos.y, splines, riverIndex, pointIndex))
		{
			m_splineEditor.Select(riverIndex, pointIndex);
			DeleteSelectedFlowPoint();
			return;
		}

		// Select and drag a point, or Alt+click a segment to insert one.
		const Vector3* worldHit = m_brushPositionValid ? &m_brushPosition : nullptr;
		if (m_splineEditor.OnMouseDown(io.MousePos.x, io.MousePos.y, splines, io.KeyAlt, worldHit))
		{
			return;
		}

		if (!m_brushPositionValid)
		{
			return;
		}

		SplinePoint point;
		point.x = m_brushPosition.x;
		point.z = m_brushPosition.z;

		if (m_splineEditor.GetSelectedSpline() < 0 || m_splineEditor.GetSelectedSpline() >= static_cast<int32>(m_rivers.size()))
		{
			River river;
			river.name = "River " + std::to_string(m_rivers.size() + 1);
			river.path.interpolation = SplineInterpolation::Linear;
			river.path.closed = false;
			m_rivers.push_back(std::move(river));
			m_splineEditor.Select(static_cast<int32>(m_rivers.size()) - 1, -1);
		}

		auto& path = m_rivers[m_splineEditor.GetSelectedSpline()].path.points;
		const int32 selectedPoint = m_splineEditor.GetSelectedPoint();
		const bool hasSelection = selectedPoint >= 0 && selectedPoint < static_cast<int32>(path.size());
		point.width = hasSelection ? path[selectedPoint].width : (path.empty() ? m_newRiverHalfWidth : path.back().width);

		// With the source selected the river grows upstream, otherwise downstream after the selection.
		if (hasSelection && selectedPoint == 0 && path.size() > 1)
		{
			path.insert(path.begin(), point);
			m_splineEditor.Select(m_splineEditor.GetSelectedSpline(), 0);
		}
		else
		{
			const int32 insertAt = hasSelection ? selectedPoint + 1 : static_cast<int32>(path.size());
			path.insert(path.begin() + insertAt, point);
			m_splineEditor.Select(m_splineEditor.GetSelectedSpline(), insertAt);
		}

		// The new point follows the mouse until release, so click-and-drag places it precisely.
		m_splineEditor.BeginDrag(true);
	}

	void WaterEditMode::DeleteSelectedFlowPoint()
	{
		auto splines = GetRiverSplines();
		if (m_splineEditor.DeleteSelectedPoint(splines, 0))
		{
			ScheduleFlowApply();
		}
	}

	std::vector<PathSpline*> WaterEditMode::GetRiverSplines()
	{
		std::vector<PathSpline*> splines;
		splines.reserve(m_rivers.size());
		for (River& river : m_rivers)
		{
			splines.push_back(&river.path);
		}
		return splines;
	}

	void WaterEditMode::ScheduleFlowApply()
	{
		m_flowFileDirty = true;
		if (m_flowAutoApply)
		{
			m_flowApplyCountdown = FlowApplyDelay;
		}
	}

	std::vector<terrain_io::water_flow::FlowPath> WaterEditMode::CollectHandFlowPaths() const
	{
		std::vector<terrain_io::water_flow::FlowPath> paths;
		for (const River& river : m_rivers)
		{
			if (river.path.points.size() >= 2)
			{
				paths.push_back(terrain_io::water_flow::FromSpline(river.path));
			}
		}
		return paths;
	}

	std::vector<terrain_io::water_flow::FlowPath> WaterEditMode::CollectGeneratedFlowPaths() const
	{
		return m_generatedFlowPaths ? m_generatedFlowPaths() : std::vector<terrain_io::water_flow::FlowPath>();
	}

	void WaterEditMode::SetGeneratedFlowPathsProvider(GeneratedFlowPathsProvider provider)
	{
		m_generatedFlowPaths = std::move(provider);
	}

	void WaterEditMode::GetAppliedHandFlow(std::vector<terrain_io::water_flow::FlowPath>& paths, terrain_io::water_flow::FlowSettings& settings)
	{
		// A pending apply would otherwise land after the caller's write and work from a stale picture of the pages.
		if (m_flowApplyCountdown >= 0.0f)
		{
			ApplyFlow();
		}

		paths = m_appliedFlowPaths;
		settings = m_appliedFlowSettings;
	}

	void WaterEditMode::ApplyFlow()
	{
		m_flowApplyCountdown = -1.0f;

		// The generated paths are taken as they are now for both sets: a graph instance rebuild already wrote their
		// pages (and cleared what its old paths alone reached), so they are part of what is on the pages. Being in the
		// current set keeps their pages flowing; being in the previous set as well means only hand edits can clear a
		// page here.
		std::vector<terrain_io::water_flow::FlowPath> handPaths = CollectHandFlowPaths();
		const std::vector<terrain_io::water_flow::FlowPath> generated = CollectGeneratedFlowPaths();
		std::vector<terrain_io::water_flow::FlowPath> paths = handPaths;
		paths.insert(paths.end(), generated.begin(), generated.end());
		std::vector<terrain_io::water_flow::FlowPath> previous = m_appliedFlowPaths;
		previous.insert(previous.end(), generated.begin(), generated.end());

		const auto result = m_terrain.ApplyWaterFlow(paths, m_flowSettings, previous, m_appliedFlowSettings,
			m_flowAssignRiverMaterial ? String(RiverWaterMaterial) : String());
		m_appliedFlowPaths = std::move(handPaths);
		m_appliedFlowSettings = m_flowSettings;

		std::ostringstream status;
		status << "Flow written to " << result.pagesWritten << " page(s)";
		if (result.pagesCleared > 0)
		{
			status << ", cleared on " << result.pagesCleared;
		}
		status << ". Save the world to keep it.";
		m_flowStatus = status.str();
	}

	bool WaterEditMode::ProjectToScreen(const Vector3& worldPosition, ImVec2& screenPosition) const
	{
		const Vector4 clip = m_overlayViewProj * Vector4(worldPosition, 1.0f);
		if (clip.w <= 0.0f)
		{
			return false;
		}

		screenPosition.x = m_overlayViewportMin.x + (clip.x / clip.w * 0.5f + 0.5f) * m_overlayViewportSize.x;
		screenPosition.y = m_overlayViewportMin.y + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * m_overlayViewportSize.y;
		return true;
	}

	float WaterEditMode::GetFlowPointHeight(const float x, const float z) const
	{
		if (m_terrain.HasWaterAtWorldPos(x, z))
		{
			return m_terrain.GetWaterHeightAtWorldPos(x, z) + 0.3f;
		}

		return m_terrain.GetSmoothHeightAt(x, z) + 0.3f;
	}

	void WaterEditMode::DrawFlowOverlay(ImDrawList& drawList)
	{
		const ImGuiIO& io = ImGui::GetIO();

		// Deferred apply, and Delete for the selected point (unless a text field has the keyboard).
		if (m_flowApplyCountdown >= 0.0f && !m_splineEditor.IsDragging())
		{
			m_flowApplyCountdown -= io.DeltaTime;
			if (m_flowApplyCountdown < 0.0f)
			{
				ApplyFlow();
			}
		}
		if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			DeleteSelectedFlowPoint();
		}

		// Stored flow: an arrow every few water vertices, pointing downstream, longer and brighter
		// where the current is faster. Read from the pages, so it shows exactly what will render.
		if (m_flowShowArrows)
		{
			const Vector3 cameraPosition = m_camera.GetDerivedPosition();
			constexpr float maxArrowDistance = 700.0f;
			constexpr uint32 step = 4;
			constexpr uint32 side = terrain::constants::OuterVerticesPerPageSide;
			const float pageSize = static_cast<float>(terrain::constants::PageSize);
			const float vertexSpacing = pageSize / static_cast<float>(side - 1);
			const float halfW = static_cast<float>(m_terrain.GetWidth() * terrain::constants::PageSize) * 0.5f;
			const float halfH = static_cast<float>(m_terrain.GetHeight() * terrain::constants::PageSize) * 0.5f;

			for (uint32 pz = 0; pz < m_terrain.GetHeight(); ++pz)
			{
				for (uint32 px = 0; px < m_terrain.GetWidth(); ++px)
				{
					const float originX = static_cast<float>(px) * pageSize - halfW;
					const float originZ = static_cast<float>(pz) * pageSize - halfH;
					const float nearestX = std::clamp(cameraPosition.x, originX, originX + pageSize);
					const float nearestZ = std::clamp(cameraPosition.z, originZ, originZ + pageSize);
					if (std::hypot(nearestX - cameraPosition.x, nearestZ - cameraPosition.z) > maxArrowDistance)
					{
						continue;
					}

					const terrain::Page* page = m_terrain.GetPage(px, pz);
					if (!page || !page->IsLoaded() || !page->HasWaterFlow())
					{
						continue;
					}

					for (uint32 vz = step / 2; vz < side; vz += step)
					{
						for (uint32 vx = step / 2; vx < side; vx += step)
						{
							const float wx = originX + static_cast<float>(vx) * vertexSpacing;
							const float wz = originZ + static_cast<float>(vz) * vertexSpacing;
							if (!m_terrain.HasWaterAtWorldPos(wx, wz))
							{
								continue;
							}

							float u0, v0, s0, u1, v1, s1, u2, v2, s2, u3, v3, s3, u, v, speed;
							if (!page->GetWaterFlow(vx, vz, u, v, speed) ||
								!page->GetWaterFlow(vx - 1, vz, u0, v0, s0) || !page->GetWaterFlow(vx + 1, vz, u1, v1, s1) ||
								!page->GetWaterFlow(vx, vz - 1, u2, v2, s2) || !page->GetWaterFlow(vx, vz + 1, u3, v3, s3))
							{
								continue;
							}

							// Downstream is where u grows.
							float dx = u1 - u0;
							float dz = u3 - u2;
							const float length = std::hypot(dx, dz);
							if (length < 1e-6f)
							{
								continue;
							}
							dx /= length;
							dz /= length;

							const float y = m_terrain.GetWaterHeightAtWorldPos(wx, wz) + 0.2f;
							const float arrowLength = vertexSpacing * step * (0.3f + 0.6f * speed);
							const Vector3 tail(wx - dx * arrowLength * 0.5f, y, wz - dz * arrowLength * 0.5f);
							const Vector3 head(wx + dx * arrowLength * 0.5f, y, wz + dz * arrowLength * 0.5f);
							const Vector3 barbLeft(head.x - (dx - dz * 0.5f) * arrowLength * 0.35f, y, head.z - (dz + dx * 0.5f) * arrowLength * 0.35f);
							const Vector3 barbRight(head.x - (dx + dz * 0.5f) * arrowLength * 0.35f, y, head.z - (dz - dx * 0.5f) * arrowLength * 0.35f);

							ImVec2 sTail, sHead, sLeft, sRight;
							if (!ProjectToScreen(tail, sTail) || !ProjectToScreen(head, sHead) ||
								!ProjectToScreen(barbLeft, sLeft) || !ProjectToScreen(barbRight, sRight))
							{
								continue;
							}

							const ImU32 color = FlowSpeedColor(speed, 210);
							drawList.AddLine(sTail, sHead, color, 1.5f);
							drawList.AddLine(sHead, sLeft, color, 1.5f);
							drawList.AddLine(sHead, sRight, color, 1.5f);
						}
					}
				}
			}
		}

		// Generated river paths (graph instances): read-only, so a grey centre line with direction arrows and no
		// handles. They are not part of the spline editor, so they cannot be selected.
		{
			const ImU32 generatedColor = IM_COL32(170, 170, 170, 200);
			constexpr float arrowSpacing = 15.0f;
			for (const terrain_io::water_flow::FlowPath& path : CollectGeneratedFlowPaths())
			{
				// Arrows by distance along the whole path: generated paths have a point every few metres.
				float nextArrow = arrowSpacing * 0.5f;
				float travelled = 0.0f;
				for (size_t i = 0; i + 1 < path.size(); ++i)
				{
					const terrain_io::water_flow::FlowPoint& a = path[i];
					const terrain_io::water_flow::FlowPoint& b = path[i + 1];
					const float dx = b.x - a.x;
					const float dz = b.z - a.z;
					const float length = std::hypot(dx, dz);
					if (length <= 0.0f)
					{
						continue;
					}

					ImVec2 sa, sb;
					if (ProjectToScreen(Vector3(a.x, GetFlowPointHeight(a.x, a.z), a.z), sa) &&
						ProjectToScreen(Vector3(b.x, GetFlowPointHeight(b.x, b.z), b.z), sb))
					{
						drawList.AddLine(sa, sb, generatedColor, 2.0f);
					}

					// An arrow head every arrowSpacing metres along the path, pointing downstream.
					const float fx = dx / length;
					const float fz = dz / length;
					for (; nextArrow <= travelled + length; nextArrow += arrowSpacing)
					{
						const float t = (nextArrow - travelled) / length;
						const float cx = a.x + dx * t;
						const float cz = a.z + dz * t;
						const float size = 3.0f;
						const float y = GetFlowPointHeight(cx, cz);
						ImVec2 sTip, sLeft, sRight;
						if (ProjectToScreen(Vector3(cx + fx * size, y, cz + fz * size), sTip) &&
							ProjectToScreen(Vector3(cx - fx * size - fz * size, y, cz - fz * size + fx * size), sLeft) &&
							ProjectToScreen(Vector3(cx - fx * size + fz * size, y, cz - fz * size - fx * size), sRight))
						{
							drawList.AddLine(sLeft, sTip, generatedColor, 2.0f);
							drawList.AddLine(sRight, sTip, generatedColor, 2.0f);
						}
					}
					travelled += length;
				}
			}
		}

		// River paths: centre line with banks and direction chevrons, and a handle per point.
		{
			std::vector<const PathSpline*> splines;
			std::vector<SplineEditController::Style> styles;
			for (const River& river : m_rivers)
			{
				splines.push_back(&river.path);

				SplineEditController::Style style;
				style.lineColor = IM_COL32(255, 150, 40, 200);
				style.selectedColor = IM_COL32(255, 220, 60, 255);
				style.pointColor = IM_COL32(255, 255, 255, 255);
				style.drawWidthRibbon = true;
				style.drawDirection = true;
				style.label = river.name;
				styles.push_back(std::move(style));
			}
			m_splineEditor.Draw(drawList, m_overlayViewportMin, m_overlayViewportSize, splines, styles);
		}

		// Preview of where a click would place the next point.
		if (m_brushPositionValid && !m_splineEditor.IsDragging() && m_splineEditor.GetSelectedSpline() >= 0 && m_splineEditor.GetSelectedSpline() < static_cast<int32>(m_rivers.size()))
		{
			const auto& path = m_rivers[m_splineEditor.GetSelectedSpline()].path.points;
			auto splines = GetRiverSplines();
			int32 hitRiver, hitPoint;
			if (!path.empty() && m_splineEditor.GetSelectedPoint() >= 0 && m_splineEditor.GetSelectedPoint() < static_cast<int32>(path.size()) &&
				!m_splineEditor.HitTestPoint(io.MousePos.x, io.MousePos.y, splines, hitRiver, hitPoint))
			{
				const auto& from = path[m_splineEditor.GetSelectedPoint()];
				ImVec2 a, b;
				if (ProjectToScreen(Vector3(from.x, GetFlowPointHeight(from.x, from.z), from.z), a) &&
					ProjectToScreen(Vector3(m_brushPosition.x, GetFlowPointHeight(m_brushPosition.x, m_brushPosition.z), m_brushPosition.z), b))
				{
					drawList.AddLine(a, b, IM_COL32(255, 255, 255, 90), 1.0f);
				}
			}
		}
	}

	void WaterEditMode::LoadFlowPaths(const String& fileName)
	{
		m_rivers.clear();
		m_flowSettings = {};
		m_splineEditor.Select(-1, -1);
		m_splineEditor.CancelDrag();
		m_flowFileDirty = false;

		FlowPathsFile file;
		String error;
		if (!ReadFlowPathsFile(fileName, file, error))
		{
			WLOG("Failed to read river flow file: " << error << ", starting without rivers");
		}
		m_flowSettings = file.settings;
		for (auto& entry : file.rivers)
		{
			River river;
			river.name = std::move(entry.first);
			river.path = std::move(entry.second);
			m_rivers.push_back(std::move(river));
		}

		// The loaded paths wrote the flow that is on disk, so they own the pages they reach.
		m_appliedFlowPaths = CollectHandFlowPaths();
		m_appliedFlowSettings = m_flowSettings;
		DLOG("Loaded " << m_rivers.size() << " river flow path(s) from " << fileName);
	}

	void WaterEditMode::SaveFlowPaths(const String& fileName)
	{
		// A pending apply must reach the pages before they are written.
		if (m_flowApplyCountdown >= 0.0f)
		{
			ApplyFlow();
		}

		if (!m_flowFileDirty)
		{
			return;
		}

		// Only the hand-drawn rivers: generated paths belong to their graph instances.
		FlowPathsFile file;
		file.settings = m_flowSettings;
		for (const River& river : m_rivers)
		{
			file.rivers.emplace_back(river.name, river.path);
		}

		String error;
		if (!WriteFlowPathsFile(fileName, file, error))
		{
			ELOG("Failed to write river flow file: " << error);
			return;
		}
		m_flowFileDirty = false;
	}
}
