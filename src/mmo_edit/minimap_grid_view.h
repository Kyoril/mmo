// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "graphics/texture.h"

#include <imgui.h>

#include <map>
#include <set>
#include <string>

namespace mmo
{
	/// @brief A reusable ImGui widget that renders the generated per-page minimap tiles of a world
	///        as a zoomable, pannable grid. Tiles without a generated minimap are drawn as empty
	///        (black with an outline). The user can pick a tile to obtain its world location.
	///
	/// Minimap tiles are loaded from "Textures/Minimaps/{world}/{pageIndex}.htex" where the page
	/// index is encoded as (pageX << 8) | pageY (see Minimap::BuildPageIndex). The widget keeps its
	/// own texture cache so that it can be refreshed after minimaps have been (re-)generated.
	class MinimapGridView final : public NonCopyable
	{
	public:
		MinimapGridView() = default;

	public:
		/// @brief Sets the world whose minimaps should be displayed. This rescans the available
		///        tiles and clears the texture cache. Has no effect if the world did not change.
		/// @param worldName The world name (the sub-folder below "Textures/Minimaps/").
		void SetWorld(const String& worldName);

		/// @brief Gets the currently displayed world name.
		[[nodiscard]] const String& GetWorld() const { return m_worldName; }

		/// @brief Rescans the available minimap tiles and drops all cached textures so that they are
		///        reloaded on demand. Call this after minimaps have been regenerated.
		void Refresh();

		/// @brief Draws the minimap grid into the current ImGui window using the given size.
		/// @param size The size of the canvas. Components <= 0 fall back to the available content region.
		/// @return True if the user picked a (new) tile this frame.
		bool Draw(const ImVec2& size);

		/// @brief Whether a tile is currently selected.
		[[nodiscard]] bool HasSelection() const { return m_hasSelection; }

		/// @brief Clears the current tile selection.
		void ClearSelection() { m_hasSelection = false; }

		/// @brief Gets the selected page coordinates. Only valid if HasSelection() is true.
		[[nodiscard]] int32 GetSelectedPageX() const { return m_selectedX; }
		[[nodiscard]] int32 GetSelectedPageY() const { return m_selectedY; }

		/// @brief Gets the world-space center position (X/Z plane) of the selected page.
		/// @param outWorldX Receives the world X coordinate.
		/// @param outWorldZ Receives the world Z coordinate.
		void GetSelectedWorldCenter(float& outWorldX, float& outWorldZ) const;

		/// @brief Highlights a page distinctly (e.g. the page the editor camera is currently over).
		/// @param x Page X coordinate, or -1 to disable highlighting.
		/// @param y Page Y coordinate, or -1 to disable highlighting.
		void SetHighlightPage(int32 x, int32 y) { m_highlightX = x; m_highlightY = y; }

	public:
		/// @brief Converts a page coordinate along a single axis to the world-space center of that page.
		static float PageCoordToWorldCenter(int32 page);

	private:
		/// @brief Rescans the available minimap tiles from the asset registry and recomputes the
		///        bounding box. Also drops cached textures.
		void RescanTiles();

		/// @brief Lazily loads (and caches) the minimap texture for the given page index.
		/// @return The loaded texture, or nullptr if it could not be loaded.
		TexturePtr GetTileTexture(uint16 pageIndex);

	private:
		String m_worldName;

		/// @brief Page indices for which a minimap file exists on disk.
		std::set<uint16> m_availablePages;

		/// @brief Cached textures by page index. A present key with a null value means a load was
		///        attempted but failed.
		std::map<uint16, TexturePtr> m_textures;

		/// @brief Inclusive bounding box of available tiles, in page coordinates.
		int32 m_minX{ 0 };
		int32 m_minY{ 0 };
		int32 m_maxX{ 0 };
		int32 m_maxY{ 0 };
		bool m_hasTiles{ false };

		/// @brief Current zoom level expressed as pixels per tile. 0 means "fit on next draw".
		float m_tileSize{ 0.0f };

		/// @brief Pixel offset of the grid origin relative to the canvas top-left.
		ImVec2 m_scroll{ 0.0f, 0.0f };

		bool m_viewInitialized{ false };

		bool m_hasSelection{ false };
		int32 m_selectedX{ 0 };
		int32 m_selectedY{ 0 };

		int32 m_highlightX{ -1 };
		int32 m_highlightY{ -1 };
	};
}
