// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include <random>
#include <set>

struct ImDrawList;
struct ImVec2;

namespace mmo
{
	class Camera;
	class InstancedFoliage;
	class ManualRenderObject;
	class SceneNode;

	namespace terrain
	{
		class Terrain;
	}

	/// @brief The interaction mode of the foliage brush.
	enum class FoliageBrushMode : uint8
	{
		/// @brief Scatter instances across the brush area.
		Paint,

		/// @brief Remove instances within the brush area.
		Erase,

		/// @brief Click to select a single instance for fine editing.
		Select
	};

	/// @brief Edit mode for authoring instanced foliage (trees) directly on the terrain.
	/// @details Supports mass placement with a scatter brush, erasing, and per-instance selection
	///          for fine adjustments. Placed instances are stored in the world's InstancedFoliage
	///          system and persisted as per-page .hfol files when the world is saved.
	class FoliageEditMode final : public WorldEditMode
	{
	public:
		explicit FoliageEditMode(IWorldEditor& worldEditor, InstancedFoliage& foliage, terrain::Terrain* terrain, Camera& camera);
		~FoliageEditMode() override;

	public:
		const char* GetName() const override;

		void DrawDetails() override;

		void OnActivate() override;

		void OnDeactivate() override;

		void OnMouseDown(float x, float y) override;

		void OnMouseHold(float deltaSeconds) override;

		void OnMouseMoved(float x, float y) override;

		void OnMouseUp(float x, float y) override;

		void OnMouseWheel(float delta) override;

		bool SupportsViewportDrop() const override { return true; }

		void OnViewportDrop(float x, float y) override;

		void DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize) override;

	public:
		/// @brief Gets the set of terrain pages whose foliage was modified since the last save.
		[[nodiscard]] const std::set<uint16>& GetDirtyPages() const { return m_dirtyPages; }

		/// @brief Clears the dirty-page set (called after a successful save).
		void ClearDirtyPages() { m_dirtyPages.clear(); }

		/// @brief Returns whether there are unsaved foliage changes.
		[[nodiscard]] bool HasUnsavedChanges() const { return !m_dirtyPages.empty(); }

		/// @brief Computes the terrain page index for a world position (matching the world streaming layout).
		static uint16 PageIndexFromPosition(const Vector3& position);

	private:
		bool UpdateBrushPosition(float viewportX, float viewportY);

		void UpdateBrushOverlay();

		void ApplyPaint();

		void ApplyErase();

		void SelectAt(float viewportX, float viewportY);

		void DeleteSelected();

	private:
		InstancedFoliage& m_foliage;
		terrain::Terrain* m_terrain;
		Camera& m_camera;

		FoliageBrushMode m_brushMode = FoliageBrushMode::Paint;

		/// @brief The mesh asset placed by the brush (drop a .hmsh onto the viewport to set it).
		char m_meshPath[256] = {};

		float m_brushRadius = 25.0f;

		/// @brief Target instances per square world unit for the scatter brush.
		float m_density = 0.01f;

		/// @brief Minimum distance kept between instances to avoid clumping.
		float m_minSpacing = 3.0f;

		float m_minScale = 0.8f;
		float m_maxScale = 1.2f;
		bool  m_randomYaw = true;
		bool  m_alignToNormal = false;

		/// @brief Maximum terrain slope (degrees) the brush will scatter on.
		float m_maxSlopeAngle = 40.0f;

		bool m_castShadows = true;

		/// @brief Whether instances placed by the brush participate in collision (movement, camera).
		bool m_collides = true;

		Vector3 m_brushPosition{};
		bool m_brushPositionValid = false;

		bool m_leftDown = false;
		bool m_hasLastPaint = false;
		Vector3 m_lastPaintPosition{};

		uint64 m_selectedInstance = 0;

		std::mt19937 m_rng{ std::random_device{}() };
		std::set<uint16> m_dirtyPages;

		ManualRenderObject* m_brushCircle = nullptr;
		SceneNode* m_brushCircleNode = nullptr;
	};
}
