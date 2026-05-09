// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_edit_mode.h"
#include "terrain/terrain.h"
#include "terrain/constants.h"

#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <cfloat>

#include "frame_ui/color.h"
#include "scene_graph/material_manager.h"
#include "math/vector3.h"
#include "math/noise.h"
#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"
#include "graphics/buffer_base.h"


namespace mmo
{
	namespace
	{
		static uint32 LerpColor(uint32 a, uint32 b, float t)
		{
			const uint8 ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
			const uint8 br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
			return 0xFF000000u
				| (static_cast<uint32>(static_cast<uint8>(ar + static_cast<int>(br - ar) * t)) << 16)
				| (static_cast<uint32>(static_cast<uint8>(ag + static_cast<int>(bg - ag) * t)) << 8)
				| static_cast<uint32>(static_cast<uint8>(ab + static_cast<int>(bb - ab) * t));
		}
	}

	static const char* s_terrainEditModeStrings[] = {
		"Select",
		"Deform",
		"Paint",
		"Area",
		"Vertex Shading",
		"Holes"
	};

	static_assert(std::size(s_terrainEditModeStrings) == static_cast<uint32>(TerrainEditType::Count_), "There needs to be one string per enum value to display!");

	static const char* s_terrainDeformModeStrings[] = {
		"Sculpt",
		"Smooth",
		"Flatten",
		"Noise"
	};

	static_assert(std::size(s_terrainDeformModeStrings) == static_cast<uint32>(TerrainDeformMode::Count_), "There needs to be one string per enum value to display!");

	static const char* s_terrainHoleModeStrings[] = {
		"Add",
		"Remove"
	};

	static_assert(std::size(s_terrainHoleModeStrings) == static_cast<uint32>(TerrainHoleMode::Count_), "There needs to be one string per enum value to display!");

	TerrainEditMode::TerrainEditMode(IWorldEditor& worldEditor, terrain::Terrain& terrain, const proto::ZoneManager& zones, Camera& camera)
		: WorldEditMode(worldEditor)
		, m_terrain(terrain)
		, m_zones(zones)
		, m_camera(camera)
	{
		m_brushCircles = m_worldEditor.CreateManualRenderObject("TerrainBrushCircles");
		if (m_brushCircles)
		{
			m_brushCircles->SetCastShadows(false);
		}

		m_brushCirclesNode = m_worldEditor.CreateChildSceneNode();
		if (m_brushCirclesNode && m_brushCircles)
		{
			m_brushCirclesNode->AttachObject(*m_brushCircles);
		}

		m_vertexDots = m_worldEditor.CreateManualRenderObject("TerrainBrushVertexDots");
		if (m_vertexDots)
		{
			m_vertexDots->SetCastShadows(false);
		}

		m_vertexDotsNode = m_worldEditor.CreateChildSceneNode();
		if (m_vertexDotsNode && m_vertexDots)
		{
			m_vertexDotsNode->AttachObject(*m_vertexDots);
		}
	}

	TerrainEditMode::~TerrainEditMode()
	{
		if (m_brushCircles)
		{
			m_worldEditor.DestroyManualRenderObject(*m_brushCircles);
			m_brushCircles = nullptr;
		}
		if (m_brushCirclesNode)
		{
			m_worldEditor.DestroySceneNode(*m_brushCirclesNode);
			m_brushCirclesNode = nullptr;
		}
		if (m_vertexDots)
		{
			m_worldEditor.DestroyManualRenderObject(*m_vertexDots);
			m_vertexDots = nullptr;
		}
		if (m_vertexDotsNode)
		{
			m_worldEditor.DestroySceneNode(*m_vertexDotsNode);
			m_vertexDotsNode = nullptr;
		}
	}

	const char* TerrainEditMode::GetName() const
	{
		static const char* s_name = "Terrain";
		return s_name;
	}

	void TerrainEditMode::DrawDetails()
	{
		if (ImGui::BeginCombo("Terrain Edit Mode", s_terrainEditModeStrings[static_cast<uint32>(m_type)], ImGuiComboFlags_None))
		{
			for (uint32 i = 0; i < static_cast<uint32>(TerrainEditType::Count_); ++i)
			{
				ImGui::PushID(i);
				if (ImGui::Selectable(s_terrainEditModeStrings[i], i == static_cast<uint32>(m_type)))
				{
					m_type = static_cast<TerrainEditType>(i);
					m_worldEditor.ClearSelection();
				}
				ImGui::PopID();
			}

			ImGui::EndCombo();
		}

		if (m_type == TerrainEditType::Deform)
		{
			if (ImGui::BeginCombo("Deform Mode", s_terrainDeformModeStrings[static_cast<uint32>(m_deformMode)], ImGuiComboFlags_None))
			{
				for (uint32 i = 0; i < static_cast<uint32>(TerrainDeformMode::Count_); ++i)
				{
					ImGui::PushID(i);
					if (ImGui::Selectable(s_terrainDeformModeStrings[i], i == static_cast<uint32>(m_deformMode)))
					{
						m_deformMode = static_cast<TerrainDeformMode>(i);
						m_worldEditor.ClearSelection();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			if (m_deformMode == TerrainDeformMode::Noise)
			{
				ImGui::SliderFloat("Frequency", &m_noiseFrequency, 0.001f, 0.1f);
				ImGui::SliderFloat("Amplitude", &m_noiseAmplitude, 0.1f, 50.0f);
				ImGui::SliderInt("Octaves", &m_noiseOctaves, 1, 8);
				ImGui::SliderFloat("Persistence", &m_noisePersistence, 0.1f, 0.9f);

				// Rebuild noise preview texture when parameters change
				const bool paramsChanged =
					m_noiseFrequency   != m_noisePreviewFrequency ||
					m_noiseAmplitude   != m_noisePreviewAmplitude  ||
					m_noiseOctaves     != m_noisePreviewOctaves    ||
					m_noisePersistence != m_noisePreviewPersistence;

				constexpr int kPreviewSize = 128;
				if (paramsChanged || !m_noisePreviewTex)
				{
					m_noisePreviewFrequency   = m_noiseFrequency;
					m_noisePreviewAmplitude   = m_noiseAmplitude;
					m_noisePreviewOctaves     = m_noiseOctaves;
					m_noisePreviewPersistence = m_noisePersistence;

					if (!m_noisePreviewTex)
					{
						m_noisePreviewTex = TextureManager::Get().CreateManual(
							"__NoisePreview__", kPreviewSize, kPreviewSize,
							PixelFormat::R8G8B8A8, BufferUsage::DynamicWriteOnly);
					}

					if (m_noisePreviewTex)
					{
						std::vector<uint32> pixels(kPreviewSize * kPreviewSize);
						// Sample fBm over the square, normalise to [0,1]
						float minV = FLT_MAX, maxV = -FLT_MAX;
						std::vector<float> raw(kPreviewSize * kPreviewSize);
						for (int py = 0; py < kPreviewSize; ++py)
						{
							for (int px = 0; px < kPreviewSize; ++px)
							{
								const float fx = static_cast<float>(px) / kPreviewSize;
								const float fz = static_cast<float>(py) / kPreviewSize;
								const float v = noise::fBm(fx * m_noiseFrequency * 200.0f,
								                           fz * m_noiseFrequency * 200.0f,
								                           m_noiseOctaves,
								                           m_noisePersistence);
								raw[py * kPreviewSize + px] = v;
								minV = std::min(minV, v);
								maxV = std::max(maxV, v);
							}
						}
						const float range = (maxV - minV) > 1e-6f ? (maxV - minV) : 1.0f;
						for (int i = 0; i < kPreviewSize * kPreviewSize; ++i)
						{
							const uint8 g = static_cast<uint8>(((raw[i] - minV) / range) * 255.0f);
							pixels[i] = 0xFF000000u | (g << 16) | (g << 8) | g; // ARGB grey
						}
						m_noisePreviewTex->UpdateFromMemory(pixels.data(), pixels.size() * sizeof(uint32));
					}
				}

				if (m_noisePreviewTex && m_noisePreviewTex->GetTextureObject())
				{
					ImGui::Spacing();
					ImGui::Text("Noise Preview:");
					ImGui::Image(m_noisePreviewTex->GetTextureObject(),
					             ImVec2(static_cast<float>(kPreviewSize), static_cast<float>(kPreviewSize)));
				}
			}
		}
		else if (m_type == TerrainEditType::Paint)
		{
			static const char* s_layerNames[] = { "Layer 1", "Layer 2", "Layer 3", "Layer 4" };

			if (ImGui::BeginCombo("Layer", s_layerNames[m_terrainPaintLayer]))
			{
				for (uint32 i = 0; i < std::size(s_layerNames); ++i)
				{
					ImGui::PushID(i);
					if (ImGui::Selectable(s_layerNames[i], i == m_terrainPaintLayer))
					{
						m_terrainPaintLayer = i;
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}
		}
		else if (m_type == TerrainEditType::Holes)
		{
			if (ImGui::BeginCombo("Hole Mode", s_terrainHoleModeStrings[static_cast<uint32>(m_holeMode)], ImGuiComboFlags_None))
			{
				for (uint32 i = 0; i < static_cast<uint32>(TerrainHoleMode::Count_); ++i)
				{
					ImGui::PushID(i);
					if (ImGui::Selectable(s_terrainHoleModeStrings[i], i == static_cast<uint32>(m_holeMode)))
					{
						m_holeMode = static_cast<TerrainHoleMode>(i);
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}
		}
		if (m_type == TerrainEditType::VertexShading)
		{
			const Color color(m_selectedColor);
			float rgba[4] = { color.GetRed(), color.GetGreen(), color.GetBlue(), color.GetAlpha() };
			if (ImGui::ColorPicker4("Vertex Color", rgba, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_DisplayHSV))
			{
				m_selectedColor = Color(rgba[0], rgba[1], rgba[2], rgba[3]);
			}
		}

		ImGui::SliderFloat("Brush Radius", &m_terrainBrushSize, 0.01f, 256.0f);
		ImGui::SliderFloat("Brush Hardness", &m_terrainBrushHardness, 0.0f, 1.0f);
		ImGui::SliderFloat("Brush Power", &m_terrainBrushPower, 0.01f, 10.0f);

		ImGui::Separator();

		if (m_type == TerrainEditType::Area)
		{
			// Render a list of all zones
			if (ImGui::BeginListBox("##areas"))
			{
				if (ImGui::Selectable("(None)", 0 == m_selectedArea))
				{
					m_selectedArea = 0;
				}

				for (const auto& zone : m_zones.getTemplates().entry())
				{
					ImGui::PushID(zone.id());
					if (ImGui::Selectable(zone.name().c_str(), zone.id() == m_selectedArea))
					{
						m_selectedArea = zone.id();
					}
					ImGui::PopID();
				}

				ImGui::EndListBox();
			}
		}

		ImGui::Separator();

		if (ImGui::Button("Reset Inner Vertices to Interpolated Height", ImVec2(-1.0f, 0.0f)))
		{
			const int maxX = static_cast<int>(m_terrain.GetWidth() * (terrain::constants::OuterVerticesPerPageSide - 1)) - 1;
			const int maxZ = static_cast<int>(m_terrain.GetHeight() * (terrain::constants::OuterVerticesPerPageSide - 1)) - 1;
			m_terrain.UpdateInnerVertices(0, 0, maxX, maxZ);
			m_terrain.UpdateTiles(0, 0, maxX, maxZ);
		}
	}

	void TerrainEditMode::OnMouseHold(const float deltaSeconds)
	{
		WorldEditMode::OnMouseHold(deltaSeconds);

		const float factor = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? -1.0f : 1.0f;

		const float outerRadius = m_terrainBrushSize;
		const float innerRadius = std::max(0.05f, m_terrainBrushSize * m_terrainBrushHardness);

		if (m_type == TerrainEditType::Deform)
		{
			if (m_deformMode == TerrainDeformMode::Flatten && ImGui::IsKeyDown(ImGuiKey_LeftControl))
			{
				m_deformFlattenHeight = m_terrain.GetSmoothHeightAt(m_brushPosition.x, m_brushPosition.z);
			}
			else
			{
				switch (m_deformMode)
				{
				case TerrainDeformMode::Sculpt:
				{
					m_terrain.Deform(m_brushPosition.x, m_brushPosition.z,
						innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds);
				} break;
				case TerrainDeformMode::Smooth:
				{
					m_terrain.Smooth(m_brushPosition.x, m_brushPosition.z,
						innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds);
				} break;
				case TerrainDeformMode::Flatten:
				{
					m_terrain.Flatten(m_brushPosition.x, m_brushPosition.z,
						innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds, m_deformFlattenHeight);
				} break;
				case TerrainDeformMode::Noise:
				{
					m_terrain.ApplyNoise(m_brushPosition.x, m_brushPosition.z,
						innerRadius, outerRadius, m_noiseAmplitude * factor, m_noiseFrequency,
						m_noiseOctaves, m_noisePersistence);
				} break;
				}
			}
		}
		else if (m_type == TerrainEditType::Paint)
		{
			m_terrain.Paint(m_terrainPaintLayer, m_brushPosition.x, m_brushPosition.z,
				innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds);
		}
		else if (m_type == TerrainEditType::Area)
		{
			m_terrain.SetArea(m_brushPosition, m_selectedArea);
		}
		else if (m_type == TerrainEditType::VertexShading)
		{
			m_terrain.Color(m_brushPosition.x, m_brushPosition.z,
				innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds, m_selectedColor);
		}
		else if (m_type == TerrainEditType::Holes)
		{
			const bool addHole = (m_holeMode == TerrainHoleMode::Add);
			m_terrain.PaintHoles(m_brushPosition.x, m_brushPosition.z, outerRadius, addHole);
		}
	}

	void TerrainEditMode::OnMouseMoved(float x, float y)
	{
		WorldEditMode::OnMouseMoved(x, y);

		// Reset validity; WorldEditorInstance will call SetBrushPosition (which sets it true)
		// only if the terrain raycast hits. If it misses, we stay false and the overlay clears.
		m_brushPositionValid = false;
		UpdateBrushOverlay();
	}

	void TerrainEditMode::OnMouseUp(float x, float y)
	{
		WorldEditMode::OnMouseUp(x, y);
	}

	void TerrainEditMode::OnMouseWheel(const float delta)
	{
		if (ImGui::GetIO().KeyShift)
		{
			m_terrainBrushSize = std::max(0.01f, std::min(m_terrainBrushSize + delta * 2.0f, 256.0f));
			UpdateBrushOverlay();
		}
		else if (ImGui::GetIO().KeyCtrl)
		{
			m_terrainBrushHardness = std::max(0.0f, std::min(m_terrainBrushHardness + delta * 0.05f, 1.0f));
			UpdateBrushOverlay();
		}
	}

	void TerrainEditMode::SetBrushPosition(const Vector3& position)
	{
		m_brushPosition = position;
		m_brushPositionValid = true;
		UpdateBrushOverlay();
	}

	void TerrainEditMode::UpdateBrushOverlay()
	{
		if (m_brushCircles)
		{
			m_brushCircles->Clear();
		}
		if (m_vertexDots)
		{
			m_vertexDots->Clear();
		}

		if (!m_brushPositionValid)
		{
			return;
		}

		// Circles and dots are both rendered in world space with the node at origin,
		// so we always keep both nodes at Vector3::Zero and use absolute world positions.
		if (m_brushCirclesNode)
		{
			m_brushCirclesNode->SetPosition(Vector3::Zero);
		}
		if (m_vertexDotsNode)
		{
			m_vertexDotsNode->SetPosition(Vector3::Zero);
		}

		const float outerRadius = m_terrainBrushSize;
		const float innerRadius = std::max(0.05f, m_terrainBrushSize * m_terrainBrushHardness);

		const float brushCenterX = m_brushPosition.x;
		const float brushCenterZ = m_brushPosition.z;

		// Build circle geometry in world space, sampling terrain height for each vertex
		// so the circles drape over the terrain instead of being flat.
		if (m_brushCircles)
		{
			constexpr int kSegments = 64;
			constexpr float k2Pi = 6.28318530718f;

			MaterialPtr mat = MaterialManager::Get().Load("Editor/Wireframe.hmat");
			auto lineOp = m_brushCircles->AddLineListOperation(mat);

			for (int i = 0; i < kSegments; ++i)
			{
				const float a1 = (static_cast<float>(i)     / kSegments) * k2Pi;
				const float a2 = (static_cast<float>(i + 1) / kSegments) * k2Pi;

				// Inner circle — sample terrain height at each endpoint
				{
					const float ix1 = brushCenterX + innerRadius * std::cos(a1);
					const float iz1 = brushCenterZ + innerRadius * std::sin(a1);
					const float ix2 = brushCenterX + innerRadius * std::cos(a2);
					const float iz2 = brushCenterZ + innerRadius * std::sin(a2);
					const float iy1 = m_terrain.GetSmoothHeightAt(ix1, iz1) + 0.1f;
					const float iy2 = m_terrain.GetSmoothHeightAt(ix2, iz2) + 0.1f;
					lineOp->AddLine(Vector3(ix1, iy1, iz1), Vector3(ix2, iy2, iz2));
				}

				// Outer circle — same treatment
				{
					const float ox1 = brushCenterX + outerRadius * std::cos(a1);
					const float oz1 = brushCenterZ + outerRadius * std::sin(a1);
					const float ox2 = brushCenterX + outerRadius * std::cos(a2);
					const float oz2 = brushCenterZ + outerRadius * std::sin(a2);
					const float oy1 = m_terrain.GetSmoothHeightAt(ox1, oz1) + 0.1f;
					const float oy2 = m_terrain.GetSmoothHeightAt(ox2, oz2) + 0.1f;
					lineOp->AddLine(Vector3(ox1, oy1, oz1), Vector3(ox2, oy2, oz2));
				}
			}
		}

		// Build vertex dot geometry — mirrors TerrainVertexBrush iteration exactly,
		// covering both outer vertices and inner (cell-center) vertices.
		if (m_vertexDots)
		{
			MaterialPtr mat = MaterialManager::Get().Load("Editor/Wireframe.hmat");
			auto dotOp = m_vertexDots->AddLineListOperation(mat);

			int dotCount = 0;
			constexpr int kMaxDots = 4000;
			constexpr float kCrossHalf = 0.2f;

			const bool holesMode = (m_type == TerrainEditType::Holes);

			constexpr float scale = static_cast<float>(
				terrain::constants::PageSize /
				static_cast<double>(terrain::constants::OuterVerticesPerPageSide - 1));

			const float halfTerrainWidth  = (m_terrain.GetWidth()  * static_cast<float>(terrain::constants::PageSize)) * 0.5f;
			const float halfTerrainHeight = (m_terrain.GetHeight() * static_cast<float>(terrain::constants::PageSize)) * 0.5f;

			const float globalCenterX = (brushCenterX + halfTerrainWidth)  / scale;
			const float globalCenterZ = (brushCenterZ + halfTerrainHeight) / scale;

			int minVertX = static_cast<int>(std::floor(globalCenterX - (outerRadius / scale)));
			int maxVertX = static_cast<int>(std::ceil (globalCenterX + (outerRadius / scale)));
			minVertX = std::max(0, minVertX);
			maxVertX = std::min<int>(maxVertX, m_terrain.GetWidth()  * (terrain::constants::OuterVerticesPerPageSide - 1));

			int minVertZ = static_cast<int>(std::floor(globalCenterZ - (outerRadius / scale)));
			int maxVertZ = static_cast<int>(std::ceil (globalCenterZ + (outerRadius / scale)));
			minVertZ = std::max(0, minVertZ);
			maxVertZ = std::min<int>(maxVertZ, m_terrain.GetHeight() * (terrain::constants::OuterVerticesPerPageSide - 1));

			// Helper to draw a dot at a world position with a colour determined by brush falloff
			auto DrawDot = [&](const float worldX, const float worldZ, const float dist)
			{
				float factor;
				if (dist <= innerRadius || outerRadius <= innerRadius)
				{
					factor = 1.0f;
				}
				else
				{
					factor = 1.0f - (dist - innerRadius) / (outerRadius - innerRadius);
				}

				if (holesMode && factor < 1.0f) return;

				const float worldY = m_terrain.GetSmoothHeightAt(worldX, worldZ) + 0.15f;
				// Green (factor=1, inner) → red (factor=0, outer)
				const uint32 color = LerpColor(0xFF00FF00u, 0xFFFF0000u, 1.0f - factor);

				auto& hLine = dotOp->AddLine(
					Vector3(worldX - kCrossHalf, worldY, worldZ),
					Vector3(worldX + kCrossHalf, worldY, worldZ));
				hLine.SetColor(color);

				auto& vLine = dotOp->AddLine(
					Vector3(worldX, worldY, worldZ - kCrossHalf),
					Vector3(worldX, worldY, worldZ + kCrossHalf));
				vLine.SetColor(color);

				++dotCount;
			};

			// --- Outer vertices ---
			for (int vx = minVertX; vx <= maxVertX && dotCount < kMaxDots; ++vx)
			{
				for (int vz = minVertZ; vz <= maxVertZ && dotCount < kMaxDots; ++vz)
				{
					const float worldX = vx * scale - halfTerrainWidth;
					const float worldZ = vz * scale - halfTerrainHeight;

					const float dx = worldX - brushCenterX;
					const float dz = worldZ - brushCenterZ;
					const float dist = std::sqrt(dx * dx + dz * dz);

					if (dist > outerRadius) continue;

					DrawDot(worldX, worldZ, dist);
				}
			}

			// --- Inner vertices (cell-center quads between outer vertices) ---
			// Mirrors the inner-vertex loop in TerrainVertexBrush exactly.
			const int minInnerX = std::max(0, minVertX - 1);
			const int maxInnerX = std::min(
				static_cast<int>(m_terrain.GetWidth()  * (terrain::constants::OuterVerticesPerPageSide - 1) - 1), maxVertX);
			const int minInnerZ = std::max(0, minVertZ - 1);
			const int maxInnerZ = std::min(
				static_cast<int>(m_terrain.GetHeight() * (terrain::constants::OuterVerticesPerPageSide - 1) - 1), maxVertZ);

			for (int ix = minInnerX; ix < maxInnerX && dotCount < kMaxDots; ++ix)
			{
				for (int iz = minInnerZ; iz < maxInnerZ && dotCount < kMaxDots; ++iz)
				{
					// Inner vertex position = average of the 4 surrounding outer-vertex world positions
					const float v0x = ix       * scale - halfTerrainWidth;
					const float v0z = iz       * scale - halfTerrainHeight;
					const float v1x = (ix + 1) * scale - halfTerrainWidth;
					const float v1z = iz       * scale - halfTerrainHeight;
					const float v2x = ix       * scale - halfTerrainWidth;
					const float v2z = (iz + 1) * scale - halfTerrainHeight;
					const float v3x = (ix + 1) * scale - halfTerrainWidth;
					const float v3z = (iz + 1) * scale - halfTerrainHeight;

					const float worldX = (v0x + v1x + v2x + v3x) * 0.25f;
					const float worldZ = (v0z + v1z + v2z + v3z) * 0.25f;

					const float dx = worldX - brushCenterX;
					const float dz = worldZ - brushCenterZ;
					const float dist = std::sqrt(dx * dx + dz * dz);

					if (dist > outerRadius) continue;

					DrawDot(worldX, worldZ, dist);
				}
			}
		}
	}
}
