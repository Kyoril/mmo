// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/manual_render_object.h"

namespace mmo
{
	/// A class for rendering a grid in the world.
	class WorldGrid final
	{
	public:
		/// @brief Creates a new instance of the WorldGrid class and initializes it.
		/// @param scene The scene to which the world grid will be added.
		/// @param name A unique name for the world grid objects.
		explicit WorldGrid(Scene& scene, const String& name);

	public:
		/// @brief Snaps a given world position to the world grid.
		/// @param position The world position to snap.
		/// @return The snapped world position.
		Vector3 SnapToGrid(const Vector3& position);

		/// @brief Updates the grid location based on the given camera's world position.
		/// @param cameraPosition The world position of the active camera.
		void UpdatePosition(const Vector3& cameraPosition);

	public:
		/// @brief Gets the scene that the world grid is placing it's resources in.
		[[nodiscard]] Scene& GetScene() const noexcept { return m_scene; }

		/// @brief Gets the number of rows that should be displayed by the world grid.
		[[nodiscard]] uint8 GetRowCount() const noexcept { return m_numRows; }

		/// @brief Gets the number of columns that should be displayed by the world grid.
		[[nodiscard]] uint8 GetColumnCount() const noexcept { return m_numCols; }

		/// @brief Gets the interval of rows / columns after which a major line should be rendered to split the grid
		///	       into large chunks, optically.
		[[nodiscard]] uint8 GetLargeGridInterval() const noexcept { return m_largeGrid; }

		/// @brief Gets the size of a single square in the grid, which is the distance between each row / column in world units.
		[[nodiscard]] float GetGridSize() const noexcept { return m_gridSize; }

		/// @brief Sets the size of the grid in world units, which is the distance between each row / column.
		/// @param size The new grid size.
		void SetGridSize(const float size) { m_gridSize = size; m_invalidated = true; }

		/// @brief Sets the number of rows to display in total.
		/// @param numRows The new number of rows to display in total.
		void SetRowCount(const uint8 numRows) { m_numRows = numRows; m_invalidated = true; }

		/// @brief Sets the number of columns to display in total.
		/// @param numCols The number of columns to display in total.
		void SetColumnCount(const uint8 numCols) { m_numCols = numCols; m_invalidated = true; }

		/// @brief Sets the interval of rows / columns after which a major line is drawn.
		/// @param interval The new interval for displaying large lines.
		void SetLargeGridInterval(const uint8 interval) { m_largeGrid = interval; m_invalidated = true; }

		/// @brief Sets whether the world grid object will be visible.
		/// @param visible True if the grid should be visible on screen.
		void SetVisible(const bool visible) { ASSERT(m_renderObject); m_renderObject->SetVisible(visible); }

		/// @brief Determines whether the world grid is visible on the screen.
		[[nodiscard]] bool IsVisible() const { ASSERT(m_renderObject); return m_renderObject->IsVisible(); }

	private:
		/// @brief Setup the render object for rendering the world grid.
		void SetupGrid() const;

		/// @brief Callback just before the grid is rendered.
		/// @param movableObject The grid's movable object.
		/// @param camera The camera that will be used for rendering the grid.
		/// @return If false, the grid won't be rendered this time.
		bool BeforeGridRendering(const MovableObject& movableObject, const Camera& camera);
		
	private:
		Scene& m_scene;
		SceneNode* m_sceneNode { nullptr };
		ManualRenderObject* m_renderObject { nullptr };
		scoped_connection m_gridRendering;
		uint8 m_numRows { 48 };
		uint8 m_numCols { 48 };
		uint8 m_largeGrid { 16 };
		float m_gridSize { 33.3333f };
		bool m_invalidated { false };
	};
}
