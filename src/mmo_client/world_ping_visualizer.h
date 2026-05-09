// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/manual_render_object.h"
#include "game_client/game_object_c.h"

#include <vector>
#include <memory>

namespace mmo
{
	class Camera;

	/// Maximum simultaneous pings visible in the world.
	static constexpr int32 kMaxWorldPings = 4;

	/// Duration in seconds a ping remains visible.
	static constexpr float kWorldPingDuration = 5.0f;

	/// Height of the billboard icon above the ping position (or unit).
	static constexpr float kPingIconHeight = 3.0f;

	/// Half-size (radius) of the billboard quad in world units.
	static constexpr float kPingIconHalfSize = 0.5f;

	/// Length of the vertical line descending from the icon to the ground.
	/// Only drawn for position pings, not unit pings.
	static constexpr float kPingLineLength = kPingIconHeight;

	enum class PingTargetType : uint8
	{
		Position = 0,
		Unit = 1
	};

	struct WorldPingEntry
	{
		/// Sender's character GUID (for deduplication).
		uint64 senderGuid = 0;

		/// What kind of ping this is.
		PingTargetType type = PingTargetType::Position;

		/// World-space ground position for Position pings.
		Vector3 groundPosition;

		/// GUID of the targeted unit (for Unit pings); 0 if Position ping.
		uint64 targetUnitGuid = 0;

		/// Seconds remaining until this ping disappears.
		float remainingTime = kWorldPingDuration;

		// --- Scene graph objects for this ping slot ---

		/// Parent scene node; positioned at the ping location each frame.
		SceneNode* node = nullptr;

		/// Vertical line from icon down to ground (position pings only).
		ManualRenderObject* lineObject = nullptr;

		/// Billboard icon quad (always present).
		ManualRenderObject* iconObject = nullptr;
	};

	/// Manages up to kMaxWorldPings simultaneous 3-D ping markers.
	/// Each marker consists of:
	///   - A billboard icon floating above the ping location
	///   - A vertical coloured line from the icon to the ground (position pings)
	///
	/// Call Update() every frame so billboards face the camera and timers tick.
	class WorldPingVisualizer
	{
	public:
		explicit WorldPingVisualizer(Scene& scene);
		~WorldPingVisualizer();

		/// Add or refresh a position-based ping.
		void AddPositionPing(uint64 senderGuid, const Vector3& groundPosition);

		/// Add or refresh a unit-based ping (icon tracks the unit's world position).
		void AddUnitPing(uint64 senderGuid, uint64 unitGuid);

		/// Update billboard orientation and tick timers. Call every frame.
		void Update(float deltaSeconds, const Camera& camera);

		/// Remove all pings (e.g., on map transition).
		void Clear();

	private:
		/// Find an existing entry for senderGuid, or grab the oldest slot.
		WorldPingEntry& AcquireSlot(uint64 senderGuid);

		/// Rebuild the geometry for the given entry based on its current position.
		void RebuildGeometry(WorldPingEntry& entry);

		/// Destroy scene objects for the given entry (does not erase from vector).
		void DestroyEntryObjects(WorldPingEntry& entry);

	private:
		Scene& m_scene;
		std::vector<WorldPingEntry> m_pings;

		MaterialPtr m_lineMaterial;
		MaterialPtr m_iconMaterial;
	};
}
