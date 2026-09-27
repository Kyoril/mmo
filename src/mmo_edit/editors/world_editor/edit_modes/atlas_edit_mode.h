// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"

#include "nlohmann/json.hpp"

#include <filesystem>
#include <functional>
#include <limits>
#include <vector>

namespace mmo
{
	namespace proto
	{
		class MapEntry;
	}

	/// @brief World editor mode for the tool-side world atlas (data/world/atlas/map_<id>.json).
	/// @details Draws the atlas' named places (pins with radius rings or outlines) and roads as a viewport
	///          overlay. Places and road points can be dragged across the terrain; places can be added,
	///          edited, confirmed (placeholder -> canon) and deleted. The file is saved explicitly and is
	///          editor-only data: it is never exported to the client, the servers or the ClientDB. Unknown
	///          JSON fields are preserved so fields written by the Python tooling (tools/world) survive a
	///          round trip, and the file keeps the tooling's format (2-space indent, floats rounded to 0.1).
	class AtlasEditMode final : public WorldEditMode
	{
	public:
		/// @brief Creates the atlas edit mode.
		/// @param worldEditor The owning world editor.
		/// @param projectPath The editor project data path (proto::Project::getLastPath(), i.e. data/editor/data).
		/// @param mapEntryProvider Returns the map currently being edited, or nullptr if none is selected.
		AtlasEditMode(IWorldEditor& worldEditor, std::filesystem::path projectPath, std::function<const proto::MapEntry*()> mapEntryProvider);

		~AtlasEditMode() override = default;

	public:
		/// @copydoc WorldEditMode::GetName
		const char* GetName() const override;

		/// @copydoc WorldEditMode::DrawDetails
		void DrawDetails() override;

		/// @brief Loads the atlas of the current map (unless there are unsaved changes).
		void OnActivate() override;

		/// @brief Places a new pin (when placing) or starts dragging the handle under the cursor.
		/// @param x Normalized viewport X coordinate (0-1).
		/// @param y Normalized viewport Y coordinate (0-1).
		void OnMouseDown(float x, float y) override;

		/// @brief Moves the dragged handle to the terrain point under the cursor.
		/// @param x Normalized viewport X coordinate (0-1).
		/// @param y Normalized viewport Y coordinate (0-1).
		void OnMouseMoved(float x, float y) override;

		/// @brief Ends a drag.
		void OnMouseUp(float x, float y) override;

		/// @copydoc WorldEditMode::DrawViewportOverlay
		void DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize) override;

		/// @brief Whether a place or road point is being dragged (the camera must not rotate meanwhile).
		[[nodiscard]] bool IsDragging() const { return m_drag.type != HandleType::None; }

	private:
		enum class HandleType
		{
			None,
			Poi,
			RoadPoint
		};

		struct Handle
		{
			HandleType type = HandleType::None;
			int index = -1;
			int pointIndex = -1;
		};

		struct ScreenHandle
		{
			Handle handle;
			float x = 0.0f;
			float y = 0.0f;
		};

		/// @brief (Re)loads the atlas file of the current map. Returns false and sets m_loadError on failure.
		bool Load();

		/// @brief Writes the atlas file. Refuses (and sets m_saveError) when the file changed on disk
		///        since it was loaded, because agents edit the same file as text.
		/// @param overwrite Write even if the file changed on disk (the user chose to keep their edits).
		/// @return false if nothing was loaded, the file changed on disk, or the write failed.
		bool Save(bool overwrite = false);

		/// @brief Last write time of the current atlas file, or the minimum value if it does not exist.
		[[nodiscard]] std::filesystem::file_time_type GetFileTime() const;

		/// @brief Path of a map's atlas file.
		[[nodiscard]] std::filesystem::path GetAtlasPath(uint32 mapId) const;

		/// @brief Intersects the view ray through a normalized viewport position with the terrain.
		bool RaycastTerrain(float viewportX, float viewportY, Vector3& outPosition) const;

		/// @brief Terrain height at a world position, or 0 when no terrain is loaded there.
		[[nodiscard]] float GroundHeight(float worldX, float worldZ) const;

		/// @brief The handle drawn closest to a normalized viewport position (within a few pixels), if any.
		[[nodiscard]] Handle PickHandle(float viewportX, float viewportY) const;

		/// @brief Moves a place (with its polygon) or a road point to a world position.
		void MoveHandle(const Handle& handle, float worldX, float worldZ);

		/// @brief Appends a new user-authored place at a world position and selects it.
		void AddPoi(float worldX, float worldZ);

		/// @brief Returns a lower_snake_case id derived from a name that no place uses yet.
		[[nodiscard]] String UniquePoiId(const String& name) const;

		/// @brief Draws the property editor of a place.
		void DrawPoiProperties(nlohmann::ordered_json& poi);

		/// @brief Draws the property editor of a road.
		void DrawRoadProperties(nlohmann::ordered_json& road);

		/// @brief Lists every placeholder that still carries a question for the user.
		void DrawNeedsInput();

	private:
		std::filesystem::path m_projectPath;
		std::function<const proto::MapEntry*()> m_mapEntryProvider;
		nlohmann::ordered_json m_atlas;
		uint32 m_loadedMapId = std::numeric_limits<uint32>::max();
		String m_loadError;
		String m_saveError;
		std::filesystem::file_time_type m_loadedFileTime = std::filesystem::file_time_type::min();
		bool m_dirty = false;
		bool m_confirmReload = false;
		float m_dragOffsetX = 0.0f;
		float m_dragOffsetZ = 0.0f;
		bool m_placing = false;
		int m_placeKind = 0;
		Handle m_selected;
		Handle m_drag;
		std::vector<ScreenHandle> m_screenHandles;
		float m_viewportMinX = 0.0f;
		float m_viewportMinY = 0.0f;
		float m_viewportWidth = 1.0f;
		float m_viewportHeight = 1.0f;
	};
}
