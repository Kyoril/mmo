// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "minimap_grid_view.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "binary_io/string_sink.h"
#include "graphics/graphics_device.h"
#include "terrain/constants.h"
#include "tex/pre_header.h"
#include "tex/pre_header_load.h"
#include "tex_v1_0/header.h"
#include "tex_v1_0/header_load.h"
#include "tex_v1_0/header_save.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <sstream>

namespace mmo
{
	namespace
	{
		/// @brief Number of empty tiles drawn around the bounding box so that the user can also pick
		///        locations just outside the area that currently has minimaps.
		constexpr int32 BorderPadding = 1;

		/// @brief Minimum and maximum allowed page coordinate.
		constexpr int32 MinPageCoord = 0;
		constexpr int32 MaxPageCoord = static_cast<int32>(terrain::constants::MaxPages) - 1;

		/// @brief Minimum / maximum zoom (pixels per tile).
		constexpr float MinTileSize = 8.0f;
		constexpr float MaxTileSize = 1024.0f;

		/// @brief Time per frame that may be spent loading tile textures.
		constexpr std::chrono::milliseconds TileLoadBudget{ 8 };

		/// @brief Smallest mip edge length that is loaded. The D3D11 texture loader cannot create
		///        DXT1 textures narrower than 8 pixels.
		constexpr uint32 MinTileMipSize = 8;

		uint32 NextPowerOfTwo(const uint32 value)
		{
			uint32 result = 1;
			while (result < value)
			{
				result <<= 1;
			}
			return result;
		}

		uint16 BuildPageIndex(const int32 x, const int32 y)
		{
			return static_cast<uint16>((static_cast<uint16>(x) << 8) | static_cast<uint16>(y));
		}
	}

	float MinimapGridView::PageCoordToWorldCenter(const int32 page)
	{
		constexpr int32 center = static_cast<int32>(terrain::constants::MaxPages) / 2;
		return static_cast<float>((page - center) * terrain::constants::PageSize)
			+ static_cast<float>(terrain::constants::PageSize) * 0.5f;
	}

	void MinimapGridView::SetWorld(const String& worldName)
	{
		if (m_worldName == worldName)
		{
			return;
		}

		m_worldName = worldName;
		m_viewInitialized = false;
		m_hasSelection = false;
		RescanTiles();
	}

	void MinimapGridView::Refresh()
	{
		RescanTiles();
	}

	void MinimapGridView::RescanTiles()
	{
		m_availablePages.clear();
		m_tiles.clear();
		m_hasTiles = false;
		m_minX = m_minY = m_maxX = m_maxY = 0;

		if (m_worldName.empty())
		{
			return;
		}

		const String prefix = "Textures/Minimaps/" + m_worldName + "/";
		const std::vector<std::string> files = AssetRegistry::ListFiles(prefix, ".htex");

		for (const auto& file : files)
		{
			// Extract the numeric page index from the file name (e.g. "8224.htex").
			const std::filesystem::path path(file);
			const std::string stem = path.stem().string();

			uint32 pageIndex;
			try
			{
				size_t consumed = 0;
				const unsigned long value = std::stoul(stem, &consumed);
				if (consumed != stem.size())
				{
					continue;
				}
				pageIndex = static_cast<uint32>(value);
			}
			catch (const std::exception&)
			{
				continue;
			}

			const int32 x = static_cast<int32>((pageIndex >> 8) & 0xFF);
			const int32 y = static_cast<int32>(pageIndex & 0xFF);

			m_availablePages.insert(static_cast<uint16>(pageIndex));

			if (!m_hasTiles)
			{
				m_minX = m_maxX = x;
				m_minY = m_maxY = y;
				m_hasTiles = true;
			}
			else
			{
				m_minX = std::min(m_minX, x);
				m_minY = std::min(m_minY, y);
				m_maxX = std::max(m_maxX, x);
				m_maxY = std::max(m_maxY, y);
			}
		}
	}

	MinimapGridView::TileTexture& MinimapGridView::LoadTile(const uint16 pageIndex, const uint32 desiredSize)
	{
		// The entry is kept even when loading fails so the tile is not retried every frame.
		TileTexture& tile = m_tiles[pageIndex];

		// Should this load fail, whatever is loaded counts as the best available, so that a failed
		// upgrade keeps the current texture and is not retried every frame either.
		tile.fullSize = tile.size;

		const String filename = "Textures/Minimaps/" + m_worldName + "/" + std::to_string(pageIndex) + ".htex";
		auto file = AssetRegistry::OpenFile(filename);
		if (!file)
		{
			return tile;
		}

		io::StreamSource source{ *file };
		io::Reader reader{ source };

		tex::PreHeader preHeader;
		tex::v1_0::Header header(tex::Version_1_0);
		if (!tex::loadPreHeader(preHeader, reader) || preHeader.version != tex::Version_1_0 || !tex::v1_0::loadHeader(header, reader)
			|| header.mipmapLengths[0] == 0)
		{
			WLOG("Invalid minimap tile " << filename);
			return tile;
		}

		// Smallest mip level that is still at least as large as the tile on screen.
		uint32 level = 0;
		while (level + 1 < header.mipmapLengths.size() && header.mipmapLengths[level + 1] != 0
			&& (static_cast<uint32>(header.width) >> (level + 1)) >= std::max(desiredSize, MinTileMipSize)
			&& (static_cast<uint32>(header.height) >> (level + 1)) >= MinTileMipSize)
		{
			++level;
		}

		std::string mipData(header.mipmapLengths[level], '\0');
		file->seekg(header.mipmapOffsets[level], std::ios::beg);
		file->read(mipData.data(), static_cast<std::streamsize>(mipData.size()));
		if (!*file)
		{
			WLOG("Failed to read minimap tile " << filename);
			return tile;
		}

		// Repackage the one mip level as a texture of its own, so that only it is uploaded.
		tex::v1_0::Header mipHeader(tex::Version_1_0);
		mipHeader.format = header.format;
		mipHeader.hasMips = false;
		mipHeader.width = static_cast<uint16>(header.width >> level);
		mipHeader.height = static_cast<uint16>(header.height >> level);
		mipHeader.mipmapOffsets.fill(0);
		mipHeader.mipmapLengths.fill(0);

		std::string buffer;
		buffer.reserve(256 + mipData.size());
		{
			io::StringSink sink(buffer);
			tex::v1_0::HeaderSaver saver(sink, mipHeader);
			mipHeader.mipmapOffsets[0] = static_cast<uint32>(sink.Position());
			mipHeader.mipmapLengths[0] = static_cast<uint32>(mipData.size());
			sink.Write(mipData.data(), mipData.size());
			saver.finish();
		}

		std::unique_ptr<std::istream> stream = std::make_unique<std::istringstream>(std::move(buffer), std::ios::binary);
		TexturePtr texture = GraphicsDevice::Get().CreateTexture();
		texture->Load(stream);

		// The replaced texture may already be in this frame's ImGui draw list, so it has to outlive it.
		if (tile.texture)
		{
			m_retiredTextures.push_back(std::move(tile.texture));
		}

		tile.texture = std::move(texture);
		tile.size = mipHeader.width;
		tile.fullSize = header.width;
		return tile;
	}

	void MinimapGridView::GetSelectedWorldCenter(float& outWorldX, float& outWorldZ) const
	{
		outWorldX = PageCoordToWorldCenter(m_selectedX);
		outWorldZ = PageCoordToWorldCenter(m_selectedY);
	}

	bool MinimapGridView::Draw(const ImVec2& size)
	{
		// Last frame's draw list has been rendered by now.
		m_retiredTextures.clear();

		ImVec2 canvasSize = size;
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		if (canvasSize.x <= 0.0f)
		{
			canvasSize.x = avail.x;
		}
		if (canvasSize.y <= 0.0f)
		{
			canvasSize.y = avail.y;
		}

		// Guard against degenerate sizes (e.g. when the panel is collapsed).
		canvasSize.x = std::max(canvasSize.x, 16.0f);
		canvasSize.y = std::max(canvasSize.y, 16.0f);

		const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
		const ImVec2 canvasMax = ImVec2(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y);

		ImGui::InvisibleButton("##minimapGridCanvas", canvasSize,
			ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
		const bool hovered = ImGui::IsItemHovered();

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImGuiIO& io = ImGui::GetIO();

		// Background and clipping.
		drawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(20, 20, 24, 255));
		drawList->PushClipRect(canvasMin, canvasMax, true);

		if (!m_hasTiles)
		{
			const char* message = m_worldName.empty()
				? "No world selected."
				: "No minimaps generated for this world yet.";
			const ImVec2 textSize = ImGui::CalcTextSize(message);
			drawList->AddText(ImVec2(canvasMin.x + (canvasSize.x - textSize.x) * 0.5f,
				canvasMin.y + (canvasSize.y - textSize.y) * 0.5f), IM_COL32(160, 160, 160, 255), message);
			drawList->PopClipRect();
			drawList->AddRect(canvasMin, canvasMax, IM_COL32(80, 80, 90, 255));
			return false;
		}

		// Padded bounding box (clamped to the valid page range).
		const int32 originX = std::max(MinPageCoord, m_minX - BorderPadding);
		const int32 originY = std::max(MinPageCoord, m_minY - BorderPadding);
		const int32 endX = std::min(MaxPageCoord, m_maxX + BorderPadding);
		const int32 endY = std::min(MaxPageCoord, m_maxY + BorderPadding);
		const int32 gridW = endX - originX + 1;
		const int32 gridH = endY - originY + 1;

		// Initialise the view so the whole grid fits and is centered.
		if (!m_viewInitialized)
		{
			const float fitX = canvasSize.x / static_cast<float>(gridW);
			const float fitY = canvasSize.y / static_cast<float>(gridH);
			m_tileSize = std::clamp(std::min(fitX, fitY), MinTileSize, MaxTileSize);

			const float gridPixelW = m_tileSize * gridW;
			const float gridPixelH = m_tileSize * gridH;
			m_scroll.x = (canvasSize.x - gridPixelW) * 0.5f;
			m_scroll.y = (canvasSize.y - gridPixelH) * 0.5f;
			m_viewInitialized = true;
		}

		// Mouse wheel zoom, centered on the cursor.
		if (hovered && io.MouseWheel != 0.0f)
		{
			const float oldSize = m_tileSize;
			const float factor = std::pow(1.1f, io.MouseWheel);
			const float newSize = std::clamp(oldSize * factor, MinTileSize, MaxTileSize);
			if (newSize != oldSize)
			{
				const ImVec2 mouseRel(io.MousePos.x - canvasMin.x - m_scroll.x,
					io.MousePos.y - canvasMin.y - m_scroll.y);
				const ImVec2 gridCoord(mouseRel.x / oldSize, mouseRel.y / oldSize);
				m_scroll.x = (io.MousePos.x - canvasMin.x) - gridCoord.x * newSize;
				m_scroll.y = (io.MousePos.y - canvasMin.y) - gridCoord.y * newSize;
				m_tileSize = newSize;
			}
		}

		// Right / middle button drag pans the view.
		if (ImGui::IsItemActive() &&
			(ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)))
		{
			m_scroll.x += io.MouseDelta.x;
			m_scroll.y += io.MouseDelta.y;
		}

		// Determine the tile currently under the cursor.
		int32 hoveredX = -1;
		int32 hoveredY = -1;
		if (hovered)
		{
			const float relX = io.MousePos.x - canvasMin.x - m_scroll.x;
			const float relY = io.MousePos.y - canvasMin.y - m_scroll.y;
			const int32 px = originX + static_cast<int32>(std::floor(relX / m_tileSize));
			const int32 py = originY + static_cast<int32>(std::floor(relY / m_tileSize));
			if (px >= MinPageCoord && px <= MaxPageCoord && py >= MinPageCoord && py <= MaxPageCoord)
			{
				hoveredX = px;
				hoveredY = py;
			}
		}

		bool selectionChanged = false;
		if (clicked && hoveredX >= 0)
		{
			if (!m_hasSelection || m_selectedX != hoveredX || m_selectedY != hoveredY)
			{
				selectionChanged = true;
			}
			m_hasSelection = true;
			m_selectedX = hoveredX;
			m_selectedY = hoveredY;
		}

		// Tile textures load lazily under a per-frame time budget so that opening a world with
		// many minimap pages does not stall the editor; tiles not loaded yet are drawn as pending
		// and fill in over the following frames.
		const auto loadDeadline = std::chrono::steady_clock::now() + TileLoadBudget;
		bool anyPending = false;

		// Each tile only needs the mip level matching its size on screen. Tiles shown blurrier than
		// that are refined after the missing ones are loaded, closest to the canvas centre first.
		const uint32 desiredSize = NextPowerOfTwo(static_cast<uint32>(std::ceil(m_tileSize)));
		struct Upgrade
		{
			uint16 pageIndex;
			float distanceSq;
		};
		std::vector<Upgrade> upgrades;
		const ImVec2 canvasCenter((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f);

		// Draw the grid tiles.
		for (int32 py = originY; py <= endY; ++py)
		{
			for (int32 px = originX; px <= endX; ++px)
			{
				const ImVec2 p0(canvasMin.x + m_scroll.x + (px - originX) * m_tileSize,
					canvasMin.y + m_scroll.y + (py - originY) * m_tileSize);
				const ImVec2 p1(p0.x + m_tileSize, p0.y + m_tileSize);

				// Cull tiles outside the visible canvas.
				if (p1.x < canvasMin.x || p0.x > canvasMax.x || p1.y < canvasMin.y || p0.y > canvasMax.y)
				{
					continue;
				}

				const uint16 pageIndex = BuildPageIndex(px, py);
				bool drewTexture = false;
				bool pending = false;
				if (m_availablePages.find(pageIndex) != m_availablePages.end())
				{
					const TileTexture* tile = nullptr;
					if (const auto it = m_tiles.find(pageIndex); it != m_tiles.end())
					{
						tile = &it->second;
						if (tile->texture && tile->size < std::min(desiredSize, tile->fullSize))
						{
							const float dx = (p0.x + p1.x) * 0.5f - canvasCenter.x;
							const float dy = (p0.y + p1.y) * 0.5f - canvasCenter.y;
							upgrades.push_back({ pageIndex, dx * dx + dy * dy });
						}
					}
					else if (std::chrono::steady_clock::now() < loadDeadline)
					{
						tile = &LoadTile(pageIndex, desiredSize);
					}
					else
					{
						pending = true;
					}

					if (tile && tile->texture && tile->texture->GetTextureObject())
					{
						drawList->AddImage(tile->texture->GetTextureObject(), p0, p1);
						drewTexture = true;
					}
				}

				if (!drewTexture)
				{
					drawList->AddRectFilled(p0, p1, pending ? IM_COL32(45, 45, 55, 255) : IM_COL32(0, 0, 0, 255));
					anyPending |= pending;
				}

				// Grid outline.
				drawList->AddRect(p0, p1, IM_COL32(70, 70, 80, 255));
			}
		}

		// Spend what is left of the budget on sharper mip levels. They show from the next frame on.
		if (!anyPending && !upgrades.empty())
		{
			std::sort(upgrades.begin(), upgrades.end(), [](const Upgrade& a, const Upgrade& b) { return a.distanceSq < b.distanceSq; });
			for (const Upgrade& upgrade : upgrades)
			{
				if (std::chrono::steady_clock::now() >= loadDeadline)
				{
					break;
				}

				LoadTile(upgrade.pageIndex, desiredSize);
			}
		}

		// Highlight the camera/current page.
		if (m_highlightX >= originX && m_highlightX <= endX && m_highlightY >= originY && m_highlightY <= endY)
		{
			const ImVec2 p0(canvasMin.x + m_scroll.x + (m_highlightX - originX) * m_tileSize,
				canvasMin.y + m_scroll.y + (m_highlightY - originY) * m_tileSize);
			const ImVec2 p1(p0.x + m_tileSize, p0.y + m_tileSize);
			drawList->AddRect(p0, p1, IM_COL32(80, 220, 120, 255), 0.0f, 0, 2.0f);
		}

		// Highlight the hovered tile.
		if (hoveredX >= 0)
		{
			const ImVec2 p0(canvasMin.x + m_scroll.x + (hoveredX - originX) * m_tileSize,
				canvasMin.y + m_scroll.y + (hoveredY - originY) * m_tileSize);
			const ImVec2 p1(p0.x + m_tileSize, p0.y + m_tileSize);
			drawList->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 40));
		}

		// Highlight the selected tile.
		if (m_hasSelection && m_selectedX >= originX && m_selectedX <= endX &&
			m_selectedY >= originY && m_selectedY <= endY)
		{
			const ImVec2 p0(canvasMin.x + m_scroll.x + (m_selectedX - originX) * m_tileSize,
				canvasMin.y + m_scroll.y + (m_selectedY - originY) * m_tileSize);
			const ImVec2 p1(p0.x + m_tileSize, p0.y + m_tileSize);
			drawList->AddRectFilled(p0, p1, IM_COL32(255, 210, 60, 50));
			drawList->AddRect(p0, p1, IM_COL32(255, 210, 60, 255), 0.0f, 0, 2.5f);
		}

		if (anyPending)
		{
			drawList->AddText(ImVec2(canvasMin.x + 6.0f, canvasMin.y + 4.0f), IM_COL32(200, 200, 200, 255), "Loading minimaps...");
		}

		drawList->PopClipRect();
		drawList->AddRect(canvasMin, canvasMax, IM_COL32(80, 80, 90, 255));

		// Tooltip with page and world coordinates of the hovered tile.
		if (hoveredX >= 0)
		{
			const float worldX = PageCoordToWorldCenter(hoveredX);
			const float worldZ = PageCoordToWorldCenter(hoveredY);
			ImGui::BeginTooltip();
			ImGui::Text("Page (%d, %d)", hoveredX, hoveredY);
			ImGui::Text("World (%.0f, %.0f)", worldX, worldZ);
			ImGui::EndTooltip();
		}

		return selectionChanged;
	}
}
