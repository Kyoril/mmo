// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <array>
#include <memory>
#include <string>

namespace mmo
{
	class Scene;
	class SceneNode;
	class ManualRenderObject;
	class Camera;

	/// Manages up to kMaxPings simultaneous 3D world ping markers.
	///
	/// Each marker consists of:
	///   - A billboard quad (icon with UV-mapped texture), parented to a SceneNode so
	///     billboarding is achieved by rotating the node to match camera orientation each
	///     frame — no vertex-buffer rebuild needed.
	///   - An optional vertical line strip from the icon down to the ground (position pings
	///     only; unit pings get the icon above the unit with no line).
	///
	/// Geometry is created once when a ping arrives and destroyed when it expires or is
	/// replaced. Only node orientation updates happen per frame.
	class WorldPingVisualizer
	{
	public:
		static constexpr int32 kMaxPings = 4;
		static constexpr float kPingDuration = 5.0f;
		static constexpr float kIconHalfSize = 0.5f;  // half-width/height of billboard in world units
		static constexpr float kIconHeight = 3.0f;    // above ground / above unit origin

		enum class PingType { Position, Unit };

		struct PingSlot
		{
			bool active = false;
			PingType type = PingType::Position;
			float remainingTime = 0.0f;
			uint64 senderGuid = 0;
			uint64 targetGuid = 0;          // unit pings only
			Vector3 worldPosition;          // position pings: floor hit; unit pings: updated each frame

			SceneNode* iconNode = nullptr;
			ManualRenderObject* iconMesh = nullptr;
			SceneNode* lineNode = nullptr;
			ManualRenderObject* lineMesh = nullptr;  // null for unit pings
			uint8 lastAlpha = 255;  // quantized alpha from last geometry rebuild
		};

	public:
		explicit WorldPingVisualizer(Scene& scene);
		~WorldPingVisualizer();

		WorldPingVisualizer(const WorldPingVisualizer&) = delete;
		WorldPingVisualizer& operator=(const WorldPingVisualizer&) = delete;

	public:
		/// Add or replace a position ping from senderGuid at world XZ position.
		void AddPositionPing(uint64 senderGuid, const Vector3& worldPos);

		/// Add or replace a unit ping from senderGuid targeting a unit by GUID.
		void AddUnitPing(uint64 senderGuid, uint64 targetGuid);

		/// Tick — expires pings, updates unit positions, orients billboards to camera.
		void Update(float deltaSeconds, const Camera& camera);

		/// Destroy all active pings (called on map transfer / OnLeave).
		void Clear();

	private:
		/// Find the slot owned by senderGuid, or the oldest/inactive slot to evict.
		int32 AcquireSlot(uint64 senderGuid);

		/// Build billboard quad geometry for the given slot.
		void BuildIconMesh(PingSlot& slot);

		/// Build vertical line geometry for the given slot (position pings only).
		void BuildLineMesh(PingSlot& slot);

		/// Destroy all scene objects for a slot and reset it to inactive.
		void FreeSlot(PingSlot& slot);

	private:
		Scene& m_scene;
		std::array<PingSlot, kMaxPings> m_slots;

		/// Counter used to generate unique node names.
		uint32 m_nameCounter = 0;
	};
}
