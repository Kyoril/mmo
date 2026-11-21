// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"
#include "game/traversal.h"

namespace mmo
{
	/// Server-side Vigor of one character. Vigor only changes at a constant rate between
	/// transitions (glide start or stop, drain change, combat change, landing), so the tracker
	/// settles the state at every transition and computes everything in between from the time
	/// passed since. It does not own a timer: its owner arms one at GetEmptyTime / GetFullTime.
	class VigorTracker final : public NonCopyable
	{
	public:
		/// Fired by the owner's timer when Vigor ran out while something drained it.
		signal<void()> exhausted;

	public:
		/// Creates a tracker at full Vigor with the given tuning values.
		/// @param settings The tuning values (copied).
		explicit VigorTracker(const TraversalSettings& settings = TraversalSettings());

		/// Starts over at full Vigor (login, entering a world). Ends gliding and assumes the character
		/// stands on the ground. The extra drain and the combat state are kept: they follow auras and
		/// the unit's combat flag, which do not change here.
		/// @param now The current time.
		/// @param maxVigor The new maximum (0 if the character knows no traversal spell).
		void Reset(GameTime now, float maxVigor);

		/// Changes the maximum. Going from no Vigor to some fills it up, otherwise the current value is
		/// clamped to the new maximum.
		/// @param now The current time.
		/// @param maxVigor The new maximum.
		void SetMax(GameTime now, float maxVigor);

		/// Applies the time passed since the last settle to the state.
		/// @param now The current time. Times before the last settle are ignored.
		void Settle(GameTime now);

		/// Starts or stops gliding, or moves the glide into or out of an updraft. Starting a glide
		/// costs TraversalSettings::glideStartCost right away; changing the updraft costs nothing.
		/// @param now The current time.
		/// @param gliding Whether the character glides.
		/// @param inUpdraft Whether the glide is inside an updraft (ignored when not gliding).
		void SetGliding(GameTime now, bool gliding, bool inUpdraft);

		/// Sets the drain that applies on top of gliding (the sum of DrainVigor auras).
		/// @param now The current time.
		/// @param perSecond Vigor drained per second.
		void SetExtraDrain(GameTime now, float perSecond);

		/// Sets the combat state. No regeneration happens in combat.
		/// @param now The current time.
		/// @param inCombat Whether the character is in combat.
		void SetInCombat(GameTime now, bool inCombat);

		/// Sets whether the character stands on the ground. No regeneration happens in the air.
		/// @param now The current time.
		/// @param grounded Whether the character is grounded.
		void SetGrounded(GameTime now, bool grounded);

		/// @param now The current time.
		/// @return The Vigor at the given time, assuming nothing changes until then.
		[[nodiscard]] float GetCurrent(GameTime now) const;

		/// @param now The current time.
		/// @return Whether Vigor is used up at the given time.
		[[nodiscard]] bool IsEmpty(GameTime now) const;

		/// @return The maximum Vigor.
		[[nodiscard]] float GetMax() const { return m_state.max; }

		/// @param now The current time.
		/// @return When Vigor runs out at the current drain (now if it already did), or 0 if nothing
		///	        drains it.
		[[nodiscard]] GameTime GetEmptyTime(GameTime now) const;

		/// @param now The current time.
		/// @return When regeneration brings Vigor back to the maximum, or 0 if it does not regenerate
		///	        (draining, in combat, airborne) or is already full.
		[[nodiscard]] GameTime GetFullTime(GameTime now) const;

		/// @return Whether anything drains Vigor right now.
		[[nodiscard]] bool IsDraining() const;

		/// @return Whether the character glides.
		[[nodiscard]] bool IsGliding() const { return m_gliding; }

		/// @return Whether the glide is inside an updraft.
		[[nodiscard]] bool IsInUpdraft() const { return m_gliding && m_inUpdraft; }

		/// @return The drain on top of gliding, per second.
		[[nodiscard]] float GetExtraDrain() const { return m_extraDrain; }

		/// @return Whether the tracker considers the character in combat.
		[[nodiscard]] bool IsInCombat() const { return m_inCombat; }

		/// @return Whether the tracker considers the character grounded.
		[[nodiscard]] bool IsGrounded() const { return m_grounded; }

		/// @return The tuning values.
		[[nodiscard]] const TraversalSettings& GetSettings() const { return m_settings; }

	private:
		[[nodiscard]] VigorActivity GetActivity() const;

		/// Advances a copy of the state from the last settle to the given time.
		void Advance(VigorState& state, GameTime now) const;

	private:
		TraversalSettings m_settings;
		VigorState m_state;
		GameTime m_lastSettle = 0;
		bool m_gliding = false;
		bool m_inUpdraft = false;
		float m_extraDrain = 0.0f;
		bool m_inCombat = false;
		bool m_grounded = true;
	};
}
