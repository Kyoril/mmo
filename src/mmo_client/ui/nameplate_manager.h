// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "frame_ui/frame.h"

#include <map>
#include <memory>

namespace mmo
{
	class Camera;
	class GamePlayerC;
	class GameUnitC;
	class NameplateFrame;

	/// Manages the in-world unit nameplates: decides per frame which nearby units should
	/// show a plate (based on the Nameplate* console variables), creates and removes the
	/// plate frames accordingly and keeps them positioned over their units.
	///
	/// The plates live in a dedicated full-screen container frame under the WorldFrame so
	/// they render below the regular UI but stay clickable above the chat bubbles.
	class NameplateManager final : public NonCopyable
	{
	public:
		/// Per-frame update driven by WorldState::OnIdle.
		void Update(float elapsed, Camera& camera);

		/// Releases all plate frames and the container layer. Must be called before the
		/// frame tree is torn down (world leave / UI reload) so the shared_ptrs don't
		/// keep the old WorldFrame alive.
		void Clear();

	private:
		/// Snapshot of the nameplate console variable values for one update.
		struct Filters
		{
			bool enemyNpcs = true;
			bool enemyPlayers = true;
			bool friendlyNpcs = false;
			bool friendlyPlayers = false;
			bool enemyPets = false;
			bool friendlyPets = false;
			float maxDistance = 40.0f;
		};

		/// Reads the current console variable values.
		[[nodiscard]] static Filters ReadFilters();

		/// Determines whether the given unit should currently show a nameplate.
		[[nodiscard]] static bool ShouldShowNameplate(GameUnitC& unit, const GamePlayerC& player, const Filters& filters);

		/// Lazily creates the container frame under the WorldFrame.
		/// @returns false if the WorldFrame doesn't exist (yet).
		bool EnsureLayer();

	private:
		/// Container frame parented under the WorldFrame so nameplates render above the
		/// 3D world (and the chat bubbles) but below the regular UI.
		FramePtr m_layer;

		/// All active nameplates, keyed by the unit GUID they follow.
		std::map<ObjectGuid, std::shared_ptr<NameplateFrame>> m_nameplates;

		/// Counter used to give every created plate frame a unique name.
		uint32 m_nameplateCounter = 0;
	};
}
