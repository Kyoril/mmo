// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/traversal.h"

namespace mmo
{
	/// How often (milliseconds) the predicted Vigor may notify the UI at most.
	constexpr GameTime vigorChangeNotifyIntervalMs = 100;

	/// @brief Predicts the own character's Vigor between the server's writes.
	///
	///	The server writes the Vigor field only on transitions (glide start/stop, a drain change,
	///	exhaustion, regeneration reaching full, combat change, jump and land). In between, the
	///	client advances the last written value with the same rules (AdvanceVigor) and re-anchors on
	///	every write. The seconds since the last drain are tracked locally, so a re-anchor does not
	///	restart the regeneration delay.
	class VigorPredictor final
	{
	public:
		explicit VigorPredictor(TraversalSettings settings = TraversalSettings());

	public:
		/// @brief Takes over a value the server wrote.
		/// @param value The replicated Vigor.
		/// @param maxValue The replicated maximum Vigor.
		/// @param now Current time in milliseconds.
		void Anchor(float value, float maxValue, GameTime now);

		/// @brief Advances the prediction to the given time. The first call with a gliding activity
		///	after one without also applies TraversalSettings::glideStartCost (opening the glider).
		/// @param now Current time in milliseconds. Times before the last step are ignored.
		/// @param activity What drains Vigor (gliding, gliding in an updraft, nothing).
		/// @param extraDrainPerSecond Drain on top of the glide drain (DrainVigor auras such as Dash).
		/// @param inCombat Whether the character is in combat (no regeneration).
		/// @param grounded Whether the character stands on the ground or swims (regeneration allowed).
		void Advance(GameTime now, VigorActivity activity, float extraDrainPerSecond, bool inCombat, bool grounded);

		/// @brief Whether the predictor received a server value yet.
		[[nodiscard]] bool IsAnchored() const { return m_anchored; }

		/// @brief The predicted Vigor.
		[[nodiscard]] float GetCurrent() const { return m_state.current; }

		/// @brief The maximum Vigor.
		[[nodiscard]] float GetMax() const { return m_state.max; }

		/// @brief Returns true once after the prediction dropped from above zero to zero.
		///	Re-arms when the value rises above zero again.
		[[nodiscard]] bool ConsumeBecameEmpty();

		/// @brief Returns true if the value changed since the last notification, at most once every
		///	vigorChangeNotifyIntervalMs. A pending change is reported on a later call.
		/// @param now Current time in milliseconds.
		[[nodiscard]] bool ConsumeChangeNotification(GameTime now);

	private:
		void UpdateEmptyFlag(float previous);

	private:
		TraversalSettings m_settings;
		VigorState m_state;
		GameTime m_lastTime = 0;
		bool m_anchored = false;
		bool m_wasGliding = false;
		bool m_becameEmpty = false;
		bool m_hasNotified = false;
		float m_notifiedValue = 0.0f;
		float m_notifiedMax = 0.0f;
		GameTime m_lastNotifyTime = 0;
	};
}
