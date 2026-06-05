// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "water_edit_mode.h"
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

namespace mmo
{
	static const char* s_waterBrushModeStrings[] = {
		"Paint",
		"Erase",
		"Flatten",
		"Ramp",
		"Raise",
		"Lower",
	};
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
			}
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
			ImGui::InputText("##watermat", m_materialName, sizeof(m_materialName));
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
	}

	void WaterEditMode::OnMouseWheel(const float delta)
	{
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

		drawList->PushClipRect(viewportMin,
		                       ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y),
		                       true);

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

		if (!m_brushPositionValid)
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
}
