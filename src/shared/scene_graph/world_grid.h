// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/clock.h"
#include "base/typedefs.h"
#include "math/vector3.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/manual_render_object.h"

#include <functional>

namespace mmo
{
	/// A class for rendering a grid in the world.
	class WorldGrid final
	{
	public:
		/// @brief Resolves the surface height the grid should lie on at a world XZ position.
		///
		/// The grid lives in scene_graph and the terrain library is built on top of it, so the
		/// grid cannot ask the terrain anything directly. Whoever owns both hands it this instead.
		///
		/// @param x World X position to sample.
		/// @param z World Z position to sample.
		/// @param outHeight Receives the surface height when the sample succeeds.
		/// @return True if there is a surface at that position. False leaves the grid flat there,
		///         which is what happens over a hole, off the edge of the map, or above a page
		///         that has not streamed in yet.
		using HeightProvider = std::function<bool(float x, float z, float& outHeight)>;

	public:
		/// @brief Creates a new instance of the WorldGrid class and initializes it.
		/// @param scene The scene to which the world grid will be added.
		/// @param name A unique name for the world grid objects.
		explicit WorldGrid(Scene& scene, const String& name);

		/// @brief Destructor. Cleans up scene nodes and render objects.
		~WorldGrid();

	public:
		/// @brief Snaps a given world position to the world grid.
		/// @param position The world position to snap.
		/// @return The snapped world position.
		Vector3 SnapToGrid(const Vector3& position);

		/// @brief Updates the grid location based on the given camera's world position.
		/// @param cameraPosition The world position of the active camera.
		void UpdatePosition(const Vector3& cameraPosition);

		/// @brief Moves the grid to follow the camera and rebuilds it if anything invalidated it.
		///
		/// This is what runs just before the grid renders. It is public so the grid can be driven
		/// without a render pass.
		///
		/// @param cameraPosition The world position of the active camera.
		void Update(const Vector3& cameraPosition);

	public:
		/// @brief Gets the scene that the world grid is placing it's resources in.
		[[nodiscard]] Scene& GetScene() const { return m_scene; }

		/// @brief Gets the number of rows that should be displayed by the world grid.
		[[nodiscard]] uint8 GetRowCount() const { return m_numRows; }

		/// @brief Gets the number of columns that should be displayed by the world grid.
		[[nodiscard]] uint8 GetColumnCount() const { return m_numCols; }

		/// @brief Gets the interval of rows / columns after which a major line should be rendered to split the grid
		///	       into large chunks, optically.
		[[nodiscard]] uint8 GetLargeGridInterval() const { return m_largeGrid; }

		/// @brief Gets the size of a single square in the grid, which is the distance between each row / column in world units.
		[[nodiscard]] float GetGridSize() const { return m_gridSize; }

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

		void SetQueryFlags(uint32 mask) const;

	public:
		/// @brief Sets the source the grid samples surface heights from.
		/// @param provider The provider, or an empty function to drop the current one.
		void SetHeightProvider(HeightProvider provider);

		/// @brief Whether a height provider has been supplied.
		[[nodiscard]] bool HasHeightProvider() const { return static_cast<bool>(m_heightProvider); }

		/// @brief Makes the grid drape over the surface instead of lying in a flat plane.
		///
		/// A flat grid cuts straight through any terrain that is not at its own height, which is
		/// of no use for judging where anything sits. Draping costs geometry -- each grid line has
		/// to be broken into segments that can follow the ground -- so it is opt-in and does
		/// nothing at all without a height provider.
		///
		/// @param follow True to drape the grid over the surface.
		void SetFollowTerrain(bool follow);

		/// @brief Whether the grid is draping over the surface.
		[[nodiscard]] bool IsFollowingTerrain() const { return m_followTerrain; }

		/// @brief Segments each grid cell is split into when draping. One segment per terrain
		///        vertex is the default, which is the finest the surface can actually describe.
		[[nodiscard]] uint8 GetTerrainSubdivisions() const { return m_terrainSubdivisions; }

		/// @brief Sets how finely each grid cell is split when draping.
		/// @param subdivisions Segments per cell, clamped to at least one.
		void SetTerrainSubdivisions(uint8 subdivisions);

		/// @brief Height the draped grid floats above the surface, to keep it out of a z-fight
		///        with the ground it is lying on.
		[[nodiscard]] float GetTerrainOffset() const { return m_terrainOffset; }

		/// @copydoc GetTerrainOffset
		void SetTerrainOffset(float offset);

		/// @brief Discards the sampled heights so the next frame re-reads them.
		///
		/// The heights are baked into the grid's geometry when it is built, so anything that moves
		/// the surface underneath it -- a deform brush, a page streaming in -- leaves the grid
		/// describing ground that is no longer there.
		void InvalidateHeights() { m_invalidated = true; }

	private:
		/// @brief Setup the render object for rendering the world grid.
		void SetupGrid();

		/// @brief Adds one grid line, split into segments that follow the surface when draping.
		/// @param operation The line list the segments are added to.
		/// @param start Local-space start of the line.
		/// @param end Local-space end of the line.
		/// @param segments Number of segments to split the line into.
		/// @param color Colour for every segment of the line.
		void AddGridLine(ManualLineListOperation& operation, const Vector3& start, const Vector3& end,
			uint32 segments, uint32 color);

		/// @brief Resolves a local-space grid position to the point the grid should draw at.
		///
		/// Not const: it notes when the provider had no answer, which is what drives the retry
		/// while terrain is still streaming in.
		///
		/// @param local Position in the grid node's local space.
		/// @return The same position, lifted onto the surface when draping.
		[[nodiscard]] Vector3 ResolvePoint(const Vector3& local);

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

		HeightProvider m_heightProvider;
		bool m_followTerrain { false };

		/// The default grid cell is exactly one terrain tile wide and a tile spans eight quads,
		/// so eight segments put a grid vertex on every terrain vertex the line crosses.
		uint8 m_terrainSubdivisions { 8 };

		float m_terrainOffset { 0.1f };

		/// World position the currently built geometry sampled its heights at. Draped geometry is
		/// only valid for the node position it was built for, so a move has to rebuild it.
		Vector3 m_builtAt { 0.0f, 0.0f, 0.0f };
		bool m_built { false };

		/// True when the last build hit a position the provider could not answer for. Terrain
		/// streams in, so that is usually a temporary state, and the grid retries until it is
		/// gone rather than leaving a flat patch where a page arrived a moment too late.
		bool m_hadMissingSamples { false };

		/// When the current geometry was built, for pacing those retries.
		GameTime m_builtTime { 0 };

		/// How long to wait before re-reading a surface that was not fully resident. A rebuild
		/// costs thousands of terrain lookups, so retrying every frame would be far worse than
		/// the flat patch it is fixing.
		static constexpr GameTime MissingSampleRetryMs = 1000;
	};
}
