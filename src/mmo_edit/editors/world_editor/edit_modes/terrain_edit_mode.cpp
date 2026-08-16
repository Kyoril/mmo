// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_edit_mode.h"
#include "water_edit_mode.h"
#include "terrain/terrain.h"
#include "terrain/constants.h"

#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <cfloat>

#include "frame_ui/color.h"
#include "scene_graph/material_manager.h"
#include "math/vector3.h"
#include "math/vector4.h"
#include "math/matrix4.h"
#include "math/noise.h"
#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"
#include "graphics/buffer_base.h"
#include "scene_graph/camera.h"

#include "file_dialog/file_dialog.h"
#include "log/default_log_levels.h"

#include <filesystem>

#include "stb_image/stb_image.h"


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

		/// 16-entry colour palette used to render per-area tile overlays.
		/// Colors are 0xAARRGGBB; all fully opaque.
		static constexpr uint32 kAreaColorPalette[] = {
			0xFF5599FFu, // 1  – Blue
			0xFF55FF77u, // 2  – Green
			0xFFFF5555u, // 3  – Red
			0xFFFFDD44u, // 4  – Yellow
			0xFFFF55CCu, // 5  – Pink
			0xFF55FFFFu, // 6  – Cyan
			0xFFFFAA44u, // 7  – Orange
			0xFFAA55FFu, // 8  – Purple
			0xFF44FFAAu, // 9  – Mint
			0xFFFF5588u, // 10 – Rose
			0xFF5566FFu, // 11 – Indigo
			0xFFEEFF44u, // 12 – Lime
			0xFFFFBB88u, // 13 – Peach
			0xFF44FFCCu, // 14 – Aqua
			0xFFBB55FFu, // 15 – Violet
			0xFFFF55EEu, // 16 – Magenta
		};
		static constexpr uint32 kAreaColorPaletteSize = sizeof(kAreaColorPalette) / sizeof(kAreaColorPalette[0]);
	}

	static const char* s_terrainEditModeStrings[] = {
		"Select",
		"Region Select",
		"Deform",
		"Paint",
		"Area",
		"Vertex Shading",
		"Holes",
		"Water"
	};

	static_assert(std::size(s_terrainEditModeStrings) == static_cast<uint32>(TerrainEditType::Count_), "There needs to be one string per enum value to display!");

	static const char* s_terrainDeformModeStrings[] = {
		"Sculpt",
		"Smooth",
		"Flatten",
		"Noise",
		"Stamp"
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

		// Area-ID overlay: coloured tile outlines shown in Area edit mode.
		m_areaOverlay = m_worldEditor.CreateManualRenderObject("TerrainAreaOverlay");
		if (m_areaOverlay)
		{
			m_areaOverlay->SetCastShadows(false);
		}

		m_areaOverlayNode = m_worldEditor.CreateChildSceneNode();
		if (m_areaOverlayNode && m_areaOverlay)
		{
			m_areaOverlayNode->AttachObject(*m_areaOverlay);
		}

		m_regionOverlay = m_worldEditor.CreateManualRenderObject("TerrainRegionOverlay");
		if (m_regionOverlay)
		{
			m_regionOverlay->SetCastShadows(false);
		}
		m_regionOverlayNode = m_worldEditor.CreateChildSceneNode();
		if (m_regionOverlayNode && m_regionOverlay)
		{
			m_regionOverlayNode->AttachObject(*m_regionOverlay);
		}

		m_ghostOverlay = m_worldEditor.CreateManualRenderObject("TerrainRegionGhost");
		if (m_ghostOverlay)
		{
			m_ghostOverlay->SetCastShadows(false);
		}
		m_ghostOverlayNode = m_worldEditor.CreateChildSceneNode();
		if (m_ghostOverlayNode && m_ghostOverlay)
		{
			m_ghostOverlayNode->AttachObject(*m_ghostOverlay);
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
		if (m_areaOverlay)
		{
			m_worldEditor.DestroyManualRenderObject(*m_areaOverlay);
			m_areaOverlay = nullptr;
		}
		if (m_areaOverlayNode)
		{
			m_worldEditor.DestroySceneNode(*m_areaOverlayNode);
			m_areaOverlayNode = nullptr;
		}
		if (m_regionOverlay)
		{
			m_worldEditor.DestroyManualRenderObject(*m_regionOverlay);
			m_regionOverlay = nullptr;
		}
		if (m_regionOverlayNode)
		{
			m_worldEditor.DestroySceneNode(*m_regionOverlayNode);
			m_regionOverlayNode = nullptr;
		}
		if (m_ghostOverlay)
		{
			m_worldEditor.DestroyManualRenderObject(*m_ghostOverlay);
			m_ghostOverlay = nullptr;
		}
		if (m_ghostOverlayNode)
		{
			m_worldEditor.DestroySceneNode(*m_ghostOverlayNode);
			m_ghostOverlayNode = nullptr;
		}
	}

	const char* TerrainEditMode::GetName() const
	{
		static const char* s_name = "Terrain";
		return s_name;
	}

	uint32 TerrainEditMode::GetColorForAreaId(const uint32 areaId)
	{
		if (areaId == 0)
		{
			return 0xFF888888u; // neutral grey for "no area"
		}
		return kAreaColorPalette[(areaId - 1) % kAreaColorPaletteSize];
	}

	void TerrainEditMode::UpdateAreaOverlay()
	{
		// The 3-D ManualRenderObject overlay is no longer used for area colouring
		// (the material's pixel shader ignores vertex colour, rendering everything white).
		// Area tile colours are now drawn as a 2-D ImGui screen-space overlay every frame
		// via DrawViewportOverlay().  We only keep this function to clear the object so it
		// does not accidentally show stale line geometry.
		if (m_areaOverlay)
		{
			m_areaOverlay->Clear();
		}
	}

	terrain::region_math::VertexRect TerrainEditMode::SelectionFromWorldCorners(const Vector3& a, const Vector3& b) const
	{
		const int32 pagesW = static_cast<int32>(m_terrain.GetWidth());
		const int32 pagesH = static_cast<int32>(m_terrain.GetHeight());

		const int32 x0 = terrain::region_math::RoundWorldToVertex(std::min(a.x, b.x), pagesW);
		const int32 x1 = terrain::region_math::RoundWorldToVertex(std::max(a.x, b.x), pagesW);
		const int32 z0 = terrain::region_math::RoundWorldToVertex(std::min(a.z, b.z), pagesH);
		const int32 z1 = terrain::region_math::RoundWorldToVertex(std::max(a.z, b.z), pagesH);

		return terrain::region_math::VertexRect{ x0, z0, x1 - x0, z1 - z0 };
	}

	void TerrainEditMode::UpdateRegionOverlay()
	{
		if (!m_regionOverlay)
		{
			return;
		}

		m_regionOverlay->Clear();

		if (m_type != TerrainEditType::Region || m_regionState == RegionEditState::Idle || m_selection.IsEmpty())
		{
			return;
		}

		if (m_regionOverlayNode)
		{
			m_regionOverlayNode->SetPosition(Vector3::Zero);
		}

		const int32 pagesW = static_cast<int32>(m_terrain.GetWidth());
		const int32 pagesH = static_cast<int32>(m_terrain.GetHeight());
		constexpr float yBias = 0.2f;

		MaterialPtr mat = MaterialManager::Get().Load("Editor/Wireframe.hmat");
		auto lineOp = m_regionOverlay->AddLineListOperation(mat);

		// Draw the four draped edges; cap segments so huge selections stay cheap.
		const int32 stepX = std::max(1, m_selection.sizeX / 128);
		const int32 stepZ = std::max(1, m_selection.sizeZ / 128);

		auto edgePoint = [&](const int32 vx, const int32 vz) -> Vector3
		{
			const float wx = terrain::region_math::VertexToWorld(vx, pagesW);
			const float wz = terrain::region_math::VertexToWorld(vz, pagesH);
			return Vector3(wx, m_terrain.GetSmoothHeightAt(wx, wz) + yBias, wz);
		};

		constexpr uint32 selColor = 0xFFFFD800u; // selection yellow

		for (int32 x = 0; x < m_selection.sizeX; x += stepX)
		{
			const int32 x2 = std::min(x + stepX, m_selection.sizeX);
			auto& l1 = lineOp->AddLine(edgePoint(m_selection.minX + x, m_selection.minZ), edgePoint(m_selection.minX + x2, m_selection.minZ));
			l1.SetColor(selColor);
			auto& l2 = lineOp->AddLine(edgePoint(m_selection.minX + x, m_selection.minZ + m_selection.sizeZ), edgePoint(m_selection.minX + x2, m_selection.minZ + m_selection.sizeZ));
			l2.SetColor(selColor);
		}
		for (int32 z = 0; z < m_selection.sizeZ; z += stepZ)
		{
			const int32 z2 = std::min(z + stepZ, m_selection.sizeZ);
			auto& l1 = lineOp->AddLine(edgePoint(m_selection.minX, m_selection.minZ + z), edgePoint(m_selection.minX, m_selection.minZ + z2));
			l1.SetColor(selColor);
			auto& l2 = lineOp->AddLine(edgePoint(m_selection.minX + m_selection.sizeX, m_selection.minZ + z), edgePoint(m_selection.minX + m_selection.sizeX, m_selection.minZ + z2));
			l2.SetColor(selColor);
		}
	}

	void TerrainEditMode::ClearRegionSelection()
	{
		m_regionState = RegionEditState::Idle;
		m_selection = {};
		m_ghostIsMove = false;
		m_ghostHeightOffset = 0.0f;
		if (m_ghostOverlay)
		{
			m_ghostOverlay->Clear();
		}
		UpdateRegionOverlay();
	}

	void TerrainEditMode::CopySelection()
	{
		if (!HasRegionSelection())
		{
			return;
		}

		auto snapshot = m_terrain.CaptureRegion(m_selection);
		if (snapshot.IsValid())
		{
			m_clipboard = std::move(snapshot);
		}
	}

	void TerrainEditMode::CutSelection()
	{
		if (!HasRegionSelection())
		{
			return;
		}

		auto snapshot = m_terrain.CaptureRegion(m_selection);
		if (!snapshot.IsValid())
		{
			return;
		}

		m_clipboard = snapshot;
		std::vector<terrain::TerrainRegionSnapshot> before;
		before.push_back(std::move(snapshot));
		m_undoStack.Push("Cut Region", std::move(before));
		m_terrain.FillRegionFromEdges(m_selection);
		UpdateRegionOverlay();
	}

	void TerrainEditMode::BeginGhostDrag(const bool isMove)
	{
		if (isMove)
		{
			if (!HasRegionSelection())
			{
				return;
			}
			CopySelection();
		}

		if (!m_clipboard || !m_clipboard->IsValid())
		{
			return;
		}

		m_ghostIsMove = isMove;
		m_ghostHeightOffset = 0.0f;
		m_regionState = RegionEditState::GhostDrag;
		UpdateGhostOverlay();
	}

	terrain::region_math::VertexRect TerrainEditMode::ComputeGhostDestRect() const
	{
		if (!m_clipboard)
		{
			return {};
		}

		const int32 pagesW = static_cast<int32>(m_terrain.GetWidth());
		const int32 pagesH = static_cast<int32>(m_terrain.GetHeight());
		const int32 sizeX = m_clipboard->rect.sizeX;
		const int32 sizeZ = m_clipboard->rect.sizeZ;

		int32 minX = terrain::region_math::RoundWorldToVertex(m_brushPosition.x, pagesW) - sizeX / 2;
		int32 minZ = terrain::region_math::RoundWorldToVertex(m_brushPosition.z, pagesH) - sizeZ / 2;

		// Clamp so the whole rect stays inside the terrain without shrinking.
		minX = std::clamp(minX, 0, pagesW * terrain::region_math::CellsPerPage - sizeX);
		minZ = std::clamp(minZ, 0, pagesH * terrain::region_math::CellsPerPage - sizeZ);

		return terrain::region_math::VertexRect{ minX, minZ, sizeX, sizeZ };
	}

	void TerrainEditMode::CommitGhostDrag()
	{
		if (!m_clipboard || !m_clipboard->IsValid() || !m_brushPositionValid)
		{
			CancelGhostDrag();
			return;
		}

		const auto destRect = ComputeGhostDestRect();
		if (destRect.IsEmpty())
		{
			CancelGhostDrag();
			return;
		}

		std::vector<terrain::TerrainRegionSnapshot> before;
		before.push_back(m_terrain.CaptureRegion(destRect));
		if (m_ghostIsMove)
		{
			// The clipboard IS the source's before-state.
			before.push_back(*m_clipboard);
		}
		m_undoStack.Push(m_ghostIsMove ? "Move Region" : "Paste Region", std::move(before));

		if (m_ghostIsMove)
		{
			m_terrain.FillRegionFromEdges(m_clipboard->rect);
		}
		m_terrain.ApplyRegion(*m_clipboard, destRect.minX, destRect.minZ, m_ghostHeightOffset);

		// The pasted area becomes the new selection.
		m_selection = destRect;
		m_regionState = RegionEditState::Selected;
		m_ghostIsMove = false;
		if (m_ghostOverlay)
		{
			m_ghostOverlay->Clear();
		}
		UpdateRegionOverlay();
	}

	void TerrainEditMode::CancelGhostDrag()
	{
		if (m_regionState != RegionEditState::GhostDrag)
		{
			return;
		}

		m_ghostIsMove = false;
		m_regionState = m_selection.IsEmpty() ? RegionEditState::Idle : RegionEditState::Selected;
		if (m_ghostOverlay)
		{
			m_ghostOverlay->Clear();
		}
	}

	void TerrainEditMode::UpdateGhostOverlay()
	{
		if (!m_ghostOverlay)
		{
			return;
		}

		m_ghostOverlay->Clear();

		if (m_regionState != RegionEditState::GhostDrag || !m_clipboard || !m_brushPositionValid)
		{
			return;
		}

		if (m_ghostOverlayNode)
		{
			m_ghostOverlayNode->SetPosition(Vector3::Zero);
		}

		const auto destRect = ComputeGhostDestRect();
		if (destRect.IsEmpty())
		{
			return;
		}

		const int32 pagesW = static_cast<int32>(m_terrain.GetWidth());
		const int32 pagesH = static_cast<int32>(m_terrain.GetHeight());
		const int32 sizeX = destRect.sizeX;
		const int32 sizeZ = destRect.sizeZ;

		// Decimate so even page-sized ghosts stay around ~32x32 grid lines.
		const int32 step = std::max(1, std::max(sizeX, sizeZ) / 32);

		MaterialPtr mat = MaterialManager::Get().Load("Editor/Wireframe.hmat");
		auto lineOp = m_ghostOverlay->AddLineListOperation(mat);

		auto ghostPoint = [&](const int32 x, const int32 z) -> Vector3
		{
			const float wx = terrain::region_math::VertexToWorld(destRect.minX + x, pagesW);
			const float wz = terrain::region_math::VertexToWorld(destRect.minZ + z, pagesH);
			const float h = m_clipboard->outerHeights[static_cast<size_t>(z) * (m_clipboard->rect.sizeX + 1) + x]
				+ m_ghostHeightOffset;
			return Vector3(wx, h, wz);
		};

		constexpr uint32 ghostColor = 0xFF40C8FFu; // ghost cyan

		for (int32 z = 0; z <= sizeZ; z += step)
		{
			const int32 zc = std::min(z, sizeZ);
			for (int32 x = 0; x < sizeX; x += step)
			{
				const int32 x2 = std::min(x + step, sizeX);
				auto& line = lineOp->AddLine(ghostPoint(x, zc), ghostPoint(x2, zc));
				line.SetColor(ghostColor);
			}
		}
		for (int32 x = 0; x <= sizeX; x += step)
		{
			const int32 xc = std::min(x, sizeX);
			for (int32 z = 0; z < sizeZ; z += step)
			{
				const int32 z2 = std::min(z + step, sizeZ);
				auto& line = lineOp->AddLine(ghostPoint(xc, z), ghostPoint(xc, z2));
				line.SetColor(ghostColor);
			}
		}
	}

	void TerrainEditMode::HandleShortcuts()
	{
		const ImGuiIO& io = ImGui::GetIO();
		if (io.WantTextInput)
		{
			return;
		}

		// Undo/redo apply to all terrain sub-modes (they only cover region/stamp ops).
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
		{
			if (io.KeyShift)
			{
				m_undoStack.Redo(m_terrain);
			}
			else
			{
				m_undoStack.Undo(m_terrain);
			}
			UpdateRegionOverlay();
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
		{
			m_undoStack.Redo(m_terrain);
			UpdateRegionOverlay();
		}

		if (m_type != TerrainEditType::Region)
		{
			return;
		}

		const bool ghostActive = m_regionState == RegionEditState::GhostDrag;

		if (!ghostActive && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
		{
			CopySelection();
		}
		if (!ghostActive && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false))
		{
			CutSelection();
		}
		if (!ghostActive && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
		{
			BeginGhostDrag(false);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			if (m_regionState == RegionEditState::GhostDrag)
			{
				CancelGhostDrag();
			}
			else
			{
				ClearRegionSelection();
			}
		}
	}

	void TerrainEditMode::DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize)
	{
		HandleShortcuts();

		if (m_type == TerrainEditType::Water)
		{
			if (m_waterEditMode)
			{
				m_waterEditMode->DrawViewportOverlay(drawList, viewportMin, viewportSize);
			}
			return;
		}

		if (m_type != TerrainEditType::Area || !drawList)
		{
			return;
		}

		// Build the combined view-projection matrix for this frame.
		const Matrix4 viewProj = m_camera.GetProjectionMatrix() * m_camera.GetViewMatrix();

		const float halfTerrainWidth  = m_terrain.GetWidth()  * static_cast<float>(terrain::constants::PageSize) * 0.5f;
		const float halfTerrainHeight = m_terrain.GetHeight() * static_cast<float>(terrain::constants::PageSize) * 0.5f;
		constexpr float tileSize = static_cast<float>(terrain::constants::TileSize);

		const uint32 totalTilesX = m_terrain.GetWidth()  * terrain::constants::TilesPerPage;
		const uint32 totalTilesY = m_terrain.GetHeight() * terrain::constants::TilesPerPage;

		// Projects a single world-space point to screen-space.
		// Returns false when the point is behind the camera (clip.w <= 0).
		auto Project = [&](const Vector3& wp, ImVec2& sp) -> bool
		{
			const Vector4 clip = viewProj * Vector4(wp, 1.0f);
			if (clip.w <= 0.0f)
			{
				return false;
			}
			const float ndcX =  clip.x / clip.w;
			const float ndcY =  clip.y / clip.w;
			sp.x = viewportMin.x + (ndcX * 0.5f + 0.5f) * viewportSize.x;
			sp.y = viewportMin.y + (1.0f - (ndcY * 0.5f + 0.5f)) * viewportSize.y;
			return true;
		};

		// Push a clip rect so overlay pixels never escape the 3-D viewport window.
		drawList->PushClipRect(viewportMin,
		                       ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y),
		                       true /*intersect with current clip*/);

		constexpr float yBias = 0.2f;

		for (uint32 ty = 0; ty < totalTilesY; ++ty)
		{
			for (uint32 tx = 0; tx < totalTilesX; ++tx)
			{
				const uint32 areaId = m_terrain.GetAreaForTile(tx, ty);
				if (areaId == 0)
				{
					continue;
				}

				// Palette colour (0xAARRGGBB).
				const uint32 palCol = GetColorForAreaId(areaId);
				const uint8  r = static_cast<uint8>((palCol >> 16) & 0xFF);
				const uint8  g = static_cast<uint8>((palCol >>  8) & 0xFF);
				const uint8  b = static_cast<uint8>( palCol        & 0xFF);

				// Semi-transparent fill + fully-opaque border in the same colour.
				const ImU32 fillCol   = IM_COL32(r, g, b, 70);
				const ImU32 borderCol = IM_COL32(r, g, b, 220);

				// World-space tile corners.
				const float x1 =  tx      * tileSize - halfTerrainWidth;
				const float x2 = (tx + 1) * tileSize - halfTerrainWidth;
				const float z1 =  ty      * tileSize - halfTerrainHeight;
				const float z2 = (ty + 1) * tileSize - halfTerrainHeight;

				// Sample terrain height at each corner so the overlay follows the terrain.
				const float yTL = m_terrain.GetSmoothHeightAt(x1, z1) + yBias;
				const float yTR = m_terrain.GetSmoothHeightAt(x2, z1) + yBias;
				const float yBL = m_terrain.GetSmoothHeightAt(x1, z2) + yBias;
				const float yBR = m_terrain.GetSmoothHeightAt(x2, z2) + yBias;

				// Project all four corners; skip if any is behind the camera.
				ImVec2 sTL, sTR, sBL, sBR;
				if (!Project({x1, yTL, z1}, sTL)) { continue; }
				if (!Project({x2, yTR, z1}, sTR)) { continue; }
				if (!Project({x1, yBL, z2}, sBL)) { continue; }
				if (!Project({x2, yBR, z2}, sBR)) { continue; }

				// Filled quad then border outline (TL → TR → BR → BL).
				drawList->AddQuadFilled(sTL, sTR, sBR, sBL, fillCol);
				drawList->AddQuad(sTL, sTR, sBR, sBL, borderCol, 1.5f);
			}
		}

		drawList->PopClipRect();

		// When Alt is held, show an eyedropper-style highlight on the tile under the cursor.
		if (ImGui::GetIO().KeyAlt && m_brushPositionValid)
		{
			int32 pickTileX = 0, pickTileZ = 0;
			if (m_terrain.GetTileIndexByWorldPosition(m_brushPosition, pickTileX, pickTileZ)
				&& pickTileX >= 0 && pickTileZ >= 0)
			{
				const float x1 =  static_cast<float>(pickTileX)      * tileSize - halfTerrainWidth;
				const float x2 = (static_cast<float>(pickTileX) + 1) * tileSize - halfTerrainWidth;
				const float z1 =  static_cast<float>(pickTileZ)      * tileSize - halfTerrainHeight;
				const float z2 = (static_cast<float>(pickTileZ) + 1) * tileSize - halfTerrainHeight;

				const float yTL = m_terrain.GetSmoothHeightAt(x1, z1) + yBias;
				const float yTR = m_terrain.GetSmoothHeightAt(x2, z1) + yBias;
				const float yBL = m_terrain.GetSmoothHeightAt(x1, z2) + yBias;
				const float yBR = m_terrain.GetSmoothHeightAt(x2, z2) + yBias;

				ImVec2 sTL, sTR, sBL, sBR;
				if (Project({x1, yTL, z1}, sTL) && Project({x2, yTR, z1}, sTR)
					&& Project({x1, yBL, z2}, sBL) && Project({x2, yBR, z2}, sBR))
				{
					// Bright white fill + thick yellow outline to signal eyedropper mode.
					drawList->PushClipRect(viewportMin,
					                       ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y),
					                       true);
					drawList->AddQuadFilled(sTL, sTR, sBR, sBL, IM_COL32(255, 255, 255, 50));
					drawList->AddQuad(sTL, sTR, sBR, sBL, IM_COL32(255, 220, 0, 255), 2.5f);
					drawList->PopClipRect();
				}
			}
		}
	}

	void TerrainEditMode::ApplyStamp()
	{
		const float radius = m_terrainBrushSize;
		const auto rect = terrain::region_math::VertexRectForBrush(
			m_brushPosition.x, m_brushPosition.z, radius,
			static_cast<int32>(m_terrain.GetWidth()), static_cast<int32>(m_terrain.GetHeight()));
		if (rect.IsEmpty())
		{
			return;
		}

		std::vector<terrain::TerrainRegionSnapshot> before;
		before.push_back(m_terrain.CaptureRegion(rect));
		m_undoStack.Push("Stamp", std::move(before));

		const float strength = ImGui::GetIO().KeyShift ? -m_stampStrength : m_stampStrength;

		terrain::BrushMaskSampler sampler;
		if (m_useBrushMask && !m_brushMaskData.empty())
		{
			sampler = [this](const float u, const float v)
			{
				return SampleBrushMask(u, v);
			};
		}
		else
		{
			// Procedural fallback: fBm noise with a radial falloff so the stamp blends
			// into the surroundings instead of leaving a square seam.
			sampler = [this](const float u, const float v)
			{
				if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
				{
					return 0.0f;
				}
				const float du = (u - 0.5f) * 2.0f;
				const float dv = (v - 0.5f) * 2.0f;
				const float falloff = std::max(0.0f, 1.0f - std::sqrt(du * du + dv * dv));
				const float n = (noise::fBm(u * m_noiseFrequency * 200.0f, v * m_noiseFrequency * 200.0f,
					m_noiseOctaves, m_noisePersistence) + 1.0f) * 0.5f;
				return n * falloff;
			};
		}

		m_terrain.Stamp(m_brushPosition.x, m_brushPosition.z, radius, strength, sampler);
	}

	void TerrainEditMode::DrawRegionDetails()
	{
		if (HasRegionSelection())
		{
			const double cellSize = terrain::constants::PageSize / static_cast<double>(terrain::region_math::CellsPerPage);
			ImGui::Text("Selection: %d x %d cells (%.0f x %.0f units)",
				m_selection.sizeX, m_selection.sizeZ,
				m_selection.sizeX * cellSize, m_selection.sizeZ * cellSize);
		}
		else
		{
			ImGui::TextDisabled("Drag on the terrain to select a rectangle.");
		}

		const bool hasSelection = HasRegionSelection();
		const bool hasClipboard = m_clipboard && m_clipboard->IsValid();
		const bool ghostActive = m_regionState == RegionEditState::GhostDrag;

		ImGui::BeginDisabled(!hasSelection || ghostActive);
		if (ImGui::Button("Copy (Ctrl+C)"))
		{
			CopySelection();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cut (Ctrl+X)"))
		{
			CutSelection();
		}
		ImGui::SameLine();
		if (ImGui::Button("Move"))
		{
			BeginGhostDrag(true);
		}
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!hasClipboard || ghostActive);
		if (ImGui::Button("Paste (Ctrl+V)"))
		{
			BeginGhostDrag(false);
		}
		ImGui::EndDisabled();

		if (ghostActive)
		{
			ImGui::TextDisabled("Click to place, Esc to cancel, mouse wheel adjusts height.");
			if (ImGui::InputFloat("Height Offset", &m_ghostHeightOffset, 0.5f, 5.0f, "%.1f"))
			{
				UpdateGhostOverlay();
			}
		}

		ImGui::BeginDisabled(!hasSelection || ghostActive);
		if (ImGui::Button("Deselect (Esc)"))
		{
			ClearRegionSelection();
		}
		ImGui::EndDisabled();
	}

	void TerrainEditMode::DrawBrushMaskControls()
	{
		// --- Brush mask (stencil/pattern painting) ---
		ImGui::Separator();
		ImGui::Checkbox("Use Brush Mask", &m_useBrushMask);
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Import a greyscale image (red channel used as mask) to paint\n"
				"patterns into the splat layers. The mask modulates the brush falloff.");
		}

		if (ImGui::Button("Import Mask..."))
		{
			const std::vector<FileDialogFilter> filters = {
				FileDialogFilter("Image files", "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.psd"),
			};
			const std::optional<String> path = FileDialog::ShowOpen("Import Brush Mask", filters);
			if (path)
			{
				if (LoadBrushMask(*path))
				{
					m_useBrushMask = true;
				}
			}
		}

		if (!m_brushMaskData.empty())
		{
			ImGui::SameLine();
			if (ImGui::Button("Clear Mask"))
			{
				m_brushMaskData.clear();
				m_brushMaskWidth = m_brushMaskHeight = 0;
				m_brushMaskName.clear();
				m_brushMaskPreviewTex.reset();
				m_useBrushMask = false;
			}

			ImGui::TextDisabled("%s (%dx%d)", m_brushMaskName.c_str(), m_brushMaskWidth, m_brushMaskHeight);

			ImGui::Checkbox("Invert Mask", &m_brushMaskInvert);
			ImGui::SliderFloat("Mask Rotation", &m_brushMaskRotation, 0.0f, 360.0f, "%.0f deg");

			if (m_brushMaskPreviewInvert != m_brushMaskInvert)
			{
				UpdateBrushMaskPreview();
			}

			if (m_brushMaskPreviewTex && m_brushMaskPreviewTex->GetTextureObject())
			{
				ImGui::Spacing();
				ImGui::Text("Mask Preview:");
				ImGui::Image(m_brushMaskPreviewTex->GetTextureObject(), ImVec2(96.0f, 96.0f));
			}
		}
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

		// Rebuild (or clear) the area overlay whenever the edit type changes.
		if (m_type != m_lastTerrainType)
		{
			const TerrainEditType previousType = m_lastTerrainType;
			m_lastTerrainType = m_type;
			UpdateAreaOverlay();

			// Clear the water brush circle when switching away from water mode.
			if (previousType == TerrainEditType::Water && m_waterEditMode)
			{
				m_waterEditMode->ClearBrushPosition();
			}

			// Leaving Region mode drops any in-progress selection or ghost drag.
			if (previousType == TerrainEditType::Region)
			{
				ClearRegionSelection();
			}
		}

		{
			const String* undoLabel = m_undoStack.GetUndoLabel();
			const String* redoLabel = m_undoStack.GetRedoLabel();

			ImGui::BeginDisabled(!m_undoStack.CanUndo());
			if (ImGui::Button(undoLabel ? ("Undo " + *undoLabel + "##terrainUndo").c_str() : "Undo##terrainUndo"))
			{
				m_undoStack.Undo(m_terrain);
				UpdateRegionOverlay();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!m_undoStack.CanRedo());
			if (ImGui::Button(redoLabel ? ("Redo " + *redoLabel + "##terrainRedo").c_str() : "Redo##terrainRedo"))
			{
				m_undoStack.Redo(m_terrain);
				UpdateRegionOverlay();
			}
			ImGui::EndDisabled();
		}

		// Water editing is a sub-mode: delegate entirely to WaterEditMode.
		if (m_type == TerrainEditType::Water)
		{
			if (m_waterEditMode)
			{
				m_waterEditMode->DrawDetails();
			}
			return;
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

			if (m_deformMode == TerrainDeformMode::Stamp)
			{
				ImGui::SliderFloat("Stamp Strength", &m_stampStrength, 0.1f, 100.0f, "%.1f");
				ImGui::TextDisabled("Click to stamp. Hold Shift to carve downward.");
				DrawBrushMaskControls();
			}

			if (m_deformMode == TerrainDeformMode::Noise
				|| (m_deformMode == TerrainDeformMode::Stamp && (!m_useBrushMask || m_brushMaskData.empty())))
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

			DrawBrushMaskControls();
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
		else if (m_type == TerrainEditType::Region)
		{
			DrawRegionDetails();
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

		if (m_type != TerrainEditType::Water && m_type != TerrainEditType::Region)
		{
			ImGui::SliderFloat("Brush Radius", &m_terrainBrushSize, 0.01f, 256.0f);
			ImGui::SliderFloat("Brush Hardness", &m_terrainBrushHardness, 0.0f, 1.0f);
			ImGui::SliderFloat("Brush Power", &m_terrainBrushPower, 0.01f, 10.0f);
		}

		ImGui::Separator();

		if (m_type == TerrainEditType::Area)
		{
			ImGui::TextDisabled("Colour key: each zone has a unique colour shown in the viewport.");
			ImGui::TextDisabled("Hold Alt while clicking to pick the area of the tile under the cursor.");

			// Render a list of all zones.  Each entry shows a colour swatch that matches the
			// tile-outline colour rendered in the 3-D viewport so users can see at a glance
			// which tile belongs to which zone.
			if (ImGui::BeginListBox("##areas"))
			{
				// "(None)" entry – no swatch (grey placeholder keeps alignment tidy).
				{
					const ImVec4 greyCol(0.53f, 0.53f, 0.53f, 1.0f);
					ImGui::ColorButton("##c0", greyCol,
						ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoTooltip,
						ImVec2(12.0f, 12.0f));
					ImGui::SameLine(0.0f, 4.0f);
					if (ImGui::Selectable("(None)", 0 == m_selectedArea))
					{
						m_selectedArea = 0;
					}
				}

				for (const auto& zone : m_zones.getTemplates().entry())
				{
					ImGui::PushID(zone.id());

					// Colour swatch matching the viewport overlay.
					const uint32 col32 = GetColorForAreaId(zone.id());
					const ImVec4 imCol(
						static_cast<float>((col32 >> 16) & 0xFF) / 255.0f,
						static_cast<float>((col32 >>  8) & 0xFF) / 255.0f,
						static_cast<float>( col32        & 0xFF) / 255.0f,
						1.0f);
					ImGui::ColorButton("##c", imCol,
						ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoTooltip,
						ImVec2(12.0f, 12.0f));
					ImGui::SameLine(0.0f, 4.0f);

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

	void TerrainEditMode::OnMouseDown(float x, float y)
	{
		WorldEditMode::OnMouseDown(x, y);

		// A fresh stroke has no previous position to interpolate from.
		m_strokeActive = false;

		if (m_type == TerrainEditType::Water && m_waterEditMode)
		{
			m_waterEditMode->OnMouseDown(x, y);
		}

		if (m_type == TerrainEditType::Region)
		{
			if (m_regionState == RegionEditState::GhostDrag)
			{
				CommitGhostDrag();
				return;
			}

			if (m_brushPositionValid)
			{
				m_regionDragStart = m_brushPosition;
				m_selection = SelectionFromWorldCorners(m_regionDragStart, m_brushPosition);
				m_regionState = RegionEditState::Dragging;
				UpdateRegionOverlay();
			}
			return;
		}

		if (m_type == TerrainEditType::Deform && m_deformMode == TerrainDeformMode::Stamp && m_brushPositionValid)
		{
			ApplyStamp();
			return;
		}
	}

	void TerrainEditMode::OnMouseHold(const float deltaSeconds)
	{
		WorldEditMode::OnMouseHold(deltaSeconds);

		if (m_type == TerrainEditType::Water)
		{
			if (m_waterEditMode)
			{
				m_waterEditMode->OnMouseHold(deltaSeconds);
			}
			return;
		}

		if (m_type == TerrainEditType::Region)
		{
			if (m_regionState == RegionEditState::Dragging && m_brushPositionValid)
			{
				m_selection = SelectionFromWorldCorners(m_regionDragStart, m_brushPosition);
				UpdateRegionOverlay();
			}
			return;
		}

		// A missed raycast leaves m_brushPosition holding whatever the cursor last hit.
		// Applying anyway keeps hammering that stale spot for as long as the pointer is off
		// the terrain, which is the other half of what made strokes tear.
		if (!m_brushPositionValid)
		{
			m_strokeActive = false;
			return;
		}

		const float factor = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? -1.0f : 1.0f;

		const float outerRadius = m_terrainBrushSize;
		const float innerRadius = std::max(0.05f, m_terrainBrushSize * m_terrainBrushHardness);

		// Flatten's ctrl modifier samples a reference height rather than painting, so it is
		// not part of a stroke.
		if (m_type == TerrainEditType::Deform && m_deformMode == TerrainDeformMode::Flatten && ImGui::IsKeyDown(ImGuiKey_LeftControl))
		{
			m_deformFlattenHeight = m_terrain.GetSmoothHeightAt(m_brushPosition.x, m_brushPosition.z);
			m_lastStrokePosition = m_brushPosition;
			m_strokeActive = true;
			return;
		}

		// Area's alt modifier is an eyedropper, likewise not a stroke.
		if (m_type == TerrainEditType::Area && ImGui::GetIO().KeyAlt)
		{
			m_selectedArea = m_terrain.GetArea(m_brushPosition);
			m_lastStrokePosition = m_brushPosition;
			m_strokeActive = true;
			return;
		}

		// The brush is swept along the segment the cursor covered since the last application
		// rather than stamped at a single point. A brush stamped once per frame lays down separate
		// blobs the moment the cursor moves further than its own diameter between frames, which a
		// far camera makes trivial: a few pixels of pointer movement then span a large distance in
		// world space. One swept application per frame is gap-free at any speed, and costs the
		// swept area rather than the area of every sample along it.
		const terrain::BrushStroke stroke = m_strokeActive
			? terrain::BrushStroke{ m_lastStrokePosition.x, m_lastStrokePosition.z, m_brushPosition.x, m_brushPosition.z }
			: terrain::BrushStroke::At(m_brushPosition.x, m_brushPosition.z);

		ApplyBrushStroke(stroke, innerRadius, outerRadius, factor, deltaSeconds);

		m_lastStrokePosition = m_brushPosition;
		m_strokeActive = true;
	}

	uint32 TerrainEditMode::StampCountForStroke(const terrain::BrushStroke& stroke, const float spacing)
	{
		// A very fast movement from a far camera can cover thousands of world units in a frame,
		// so this is capped. Unlike the swept operations, these two cannot cover such a segment
		// without cost proportional to its length, and a stalled frame makes the next segment
		// longer still.
		constexpr uint32 maxStamps = 64;

		const float deltaX = stroke.toX - stroke.fromX;
		const float deltaZ = stroke.toZ - stroke.fromZ;
		const float length = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);

		if (length <= spacing || spacing <= 0.0f)
		{
			return 1;
		}

		return std::min(static_cast<uint32>(length / spacing) + 1, maxStamps);
	}

	void TerrainEditMode::ApplyBrushStroke(const terrain::BrushStroke& stroke, const float innerRadius, const float outerRadius, const float factor, const float deltaSeconds)
	{
		if (m_type == TerrainEditType::Deform)
		{
			switch (m_deformMode)
			{
			case TerrainDeformMode::Sculpt:
			{
				m_terrain.Deform(stroke, innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds);
			} break;
			case TerrainDeformMode::Smooth:
			{
				m_terrain.Smooth(stroke, innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds);
			} break;
			case TerrainDeformMode::Flatten:
			{
				m_terrain.Flatten(stroke, innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds, m_deformFlattenHeight);
			} break;
			case TerrainDeformMode::Noise:
			{
				// ApplyNoise displaces by a fixed amplitude instead of integrating over the frame
				// delta, so sweeping it is what keeps a stroke from applying it repeatedly at full
				// strength. fBm is deterministic per world position, so overlapping applications
				// accumulate rather than cancel.
				m_terrain.ApplyNoise(stroke, innerRadius, outerRadius, m_noiseAmplitude * factor, m_noiseFrequency,
					m_noiseOctaves, m_noisePersistence);
			} break;
			case TerrainDeformMode::Stamp:
			{
				// One stamp per click — applied in OnMouseDown.
			} break;
			}
		}
		else if (m_type == TerrainEditType::Paint)
		{
			if (m_useBrushMask && !m_brushMaskData.empty())
			{
				// A mask is a stamp: its UVs are anchored on one footprint, so it cannot be swept
				// without smearing the pattern. Step along the segment instead, spacing the stamps
				// closely enough that they overlap.
				const terrain::BrushMaskSampler sampler =
					[this](const float u, const float v) { return SampleBrushMask(u, v); };

				const uint32 stampCount = StampCountForStroke(stroke, outerRadius);
				const float stampScale = 1.0f / static_cast<float>(stampCount);

				for (uint32 stamp = 1; stamp <= stampCount; ++stamp)
				{
					const float t = static_cast<float>(stamp) * stampScale;
					const float x = stroke.fromX + (stroke.toX - stroke.fromX) * t;
					const float z = stroke.fromZ + (stroke.toZ - stroke.fromZ) * t;

					m_terrain.Paint(m_terrainPaintLayer, terrain::BrushStroke::At(x, z),
						innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds * stampScale, &sampler);
				}
			}
			else
			{
				m_terrain.Paint(m_terrainPaintLayer, stroke,
					innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds);
			}
		}
		else if (m_type == TerrainEditType::Area)
		{
			// Area IDs are set per tile rather than through a falloff brush, so the segment is
			// walked at tile resolution. The alt-held eyedropper is handled in OnMouseHold.
			const uint32 stampCount = StampCountForStroke(stroke, static_cast<float>(terrain::constants::TileSize) * 0.5f);

			for (uint32 stamp = 0; stamp <= stampCount; ++stamp)
			{
				const float t = static_cast<float>(stamp) / static_cast<float>(stampCount);
				m_terrain.SetArea(Vector3(
					stroke.fromX + (stroke.toX - stroke.fromX) * t,
					0.0f,
					stroke.fromZ + (stroke.toZ - stroke.fromZ) * t), m_selectedArea);
			}

			m_areaOverlayDirty = true; // overlay will be refreshed on mouse-up
		}
		else if (m_type == TerrainEditType::VertexShading)
		{
			m_terrain.Color(stroke, innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds, m_selectedColor);
		}
		else if (m_type == TerrainEditType::Holes)
		{
			const bool addHole = (m_holeMode == TerrainHoleMode::Add);
			m_terrain.PaintHoles(stroke, outerRadius, addHole);
		}
	}

	void TerrainEditMode::OnMouseMoved(float x, float y)
	{
		WorldEditMode::OnMouseMoved(x, y);

		if (m_type == TerrainEditType::Water)
		{
			if (m_waterEditMode)
			{
				m_waterEditMode->OnMouseMoved(x, y);
			}
			return;
		}

		// Reset validity; WorldEditorInstance will call SetBrushPosition (which sets it true)
		// only if the terrain raycast hits. If it misses, we stay false and the overlay clears.
		m_brushPositionValid = false;
		UpdateBrushOverlay();
	}

	void TerrainEditMode::OnMouseUp(float x, float y)
	{
		WorldEditMode::OnMouseUp(x, y);

		m_strokeActive = false;

		if (m_type == TerrainEditType::Water)
		{
			if (m_waterEditMode)
			{
				m_waterEditMode->OnMouseUp(x, y);
			}
			return;
		}

		if (m_type == TerrainEditType::Region)
		{
			if (m_regionState == RegionEditState::Dragging)
			{
				m_regionState = m_selection.IsEmpty() ? RegionEditState::Idle : RegionEditState::Selected;
				UpdateRegionOverlay();
			}
			return;
		}

		// Refresh the area overlay after a paint stroke finishes.
		if (m_areaOverlayDirty)
		{
			UpdateAreaOverlay();
			m_areaOverlayDirty = false;
		}
	}

	void TerrainEditMode::OnMouseWheel(const float delta)
	{
		if (m_type == TerrainEditType::Water)
		{
			if (m_waterEditMode)
			{
				m_waterEditMode->OnMouseWheel(delta);
			}
			return;
		}

		if (m_type == TerrainEditType::Region)
		{
			if (m_regionState == RegionEditState::GhostDrag && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyCtrl)
			{
				m_ghostHeightOffset += delta * 0.5f;
				UpdateGhostOverlay();
			}
			return;
		}

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

		if (m_type == TerrainEditType::Region && m_regionState == RegionEditState::GhostDrag)
		{
			UpdateGhostOverlay();
		}
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

		// Water sub-mode uses its own brush circle; hide terrain overlay.
		if (m_type == TerrainEditType::Water)
		{
			return;
		}

		// Region mode uses its own selection/ghost overlays; hide the brush visuals.
		if (m_type == TerrainEditType::Region)
		{
			return;
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

	bool TerrainEditMode::LoadBrushMask(const String& path)
	{
		int x = 0, y = 0, channels = 0;
		// Force RGBA so the red channel is always at byte offset 0 regardless of source format.
		uint8* data = stbi_load(path.c_str(), &x, &y, &channels, 4);
		if (!data)
		{
			ELOG("Failed to load brush mask image '" << path << "': " << (stbi_failure_reason() ? stbi_failure_reason() : "unknown error"));
			return false;
		}

		if (x <= 0 || y <= 0)
		{
			ELOG("Brush mask image has invalid dimensions");
			stbi_image_free(data);
			return false;
		}

		m_brushMaskWidth = x;
		m_brushMaskHeight = y;
		m_brushMaskData.resize(static_cast<size_t>(x) * static_cast<size_t>(y));
		for (size_t i = 0; i < m_brushMaskData.size(); ++i)
		{
			// Red channel as greyscale mask.
			m_brushMaskData[i] = data[i * 4] / 255.0f;
		}

		stbi_image_free(data);

		m_brushMaskName = std::filesystem::path(path).filename().string();

		// Force a preview rebuild for the freshly loaded mask.
		m_brushMaskPreviewTex.reset();
		UpdateBrushMaskPreview();

		ILOG("Loaded terrain brush mask '" << m_brushMaskName << "' (" << x << "x" << y << ")");
		return true;
	}

	void TerrainEditMode::UpdateBrushMaskPreview()
	{
		if (m_brushMaskData.empty() || m_brushMaskWidth <= 0 || m_brushMaskHeight <= 0)
		{
			m_brushMaskPreviewTex.reset();
			return;
		}

		constexpr int kPreviewSize = 96;

		if (!m_brushMaskPreviewTex)
		{
			m_brushMaskPreviewTex = TextureManager::Get().CreateManual(
				"__TerrainBrushMaskPreview__", kPreviewSize, kPreviewSize,
				PixelFormat::R8G8B8A8, BufferUsage::DynamicWriteOnly);
		}

		if (!m_brushMaskPreviewTex)
		{
			return;
		}

		std::vector<uint32> pixels(static_cast<size_t>(kPreviewSize) * kPreviewSize);
		for (int py = 0; py < kPreviewSize; ++py)
		{
			for (int px = 0; px < kPreviewSize; ++px)
			{
				// Nearest-sample the source mask into the fixed-size preview.
				const int sx = std::min(m_brushMaskWidth - 1, px * m_brushMaskWidth / kPreviewSize);
				const int sy = std::min(m_brushMaskHeight - 1, py * m_brushMaskHeight / kPreviewSize);
				float value = m_brushMaskData[static_cast<size_t>(sy) * m_brushMaskWidth + sx];
				if (m_brushMaskInvert)
				{
					value = 1.0f - value;
				}

				const uint8 g = static_cast<uint8>(std::max(0.0f, std::min(1.0f, value)) * 255.0f);
				pixels[static_cast<size_t>(py) * kPreviewSize + px] = 0xFF000000u | (g << 16) | (g << 8) | g;
			}
		}

		m_brushMaskPreviewTex->UpdateFromMemory(pixels.data(), pixels.size() * sizeof(uint32));
		m_brushMaskPreviewInvert = m_brushMaskInvert;
	}

	float TerrainEditMode::SampleBrushMask(float u, float v) const
	{
		if (m_brushMaskData.empty() || m_brushMaskWidth <= 0 || m_brushMaskHeight <= 0)
		{
			return 1.0f;
		}

		// Rotate the sample coordinates around the mask center.
		if (m_brushMaskRotation != 0.0f)
		{
			constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
			const float rad = m_brushMaskRotation * kDegToRad;
			const float c = std::cos(rad);
			const float s = std::sin(rad);
			const float cu = u - 0.5f;
			const float cv = v - 0.5f;
			u = 0.5f + (cu * c - cv * s);
			v = 0.5f + (cu * s + cv * c);
		}

		// Anything outside the footprint contributes nothing.
		if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
		{
			return 0.0f;
		}

		// Bilinear sample of the mask.
		const float fx = u * static_cast<float>(m_brushMaskWidth - 1);
		const float fy = v * static_cast<float>(m_brushMaskHeight - 1);
		const int x0 = static_cast<int>(std::floor(fx));
		const int y0 = static_cast<int>(std::floor(fy));
		const int x1 = std::min(x0 + 1, m_brushMaskWidth - 1);
		const int y1 = std::min(y0 + 1, m_brushMaskHeight - 1);
		const float tx = fx - static_cast<float>(x0);
		const float ty = fy - static_cast<float>(y0);

		auto at = [this](const int x, const int y) -> float
		{
			return m_brushMaskData[static_cast<size_t>(y) * m_brushMaskWidth + x];
		};

		const float top = at(x0, y0) * (1.0f - tx) + at(x1, y0) * tx;
		const float bottom = at(x0, y1) * (1.0f - tx) + at(x1, y1) * tx;
		float value = top * (1.0f - ty) + bottom * ty;

		if (m_brushMaskInvert)
		{
			value = 1.0f - value;
		}

		return value;
	}
}
