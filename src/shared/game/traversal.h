// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
	/// Tuning values for the traversal abilities (Vigor, Dash, Glider). Shared by client and server.
	struct TraversalSettings
	{
		/// Maximum Vigor a character can hold.
		float maxVigor = 100.0f;

		/// Vigor regenerated per second while not draining.
		float regenPerSecond = 25.0f;

		/// Seconds without any drain before regeneration starts.
		float regenDelaySeconds = 1.0f;

		/// Vigor drained per second while gliding.
		float glideDrainPerSecond = 8.0f;

		/// Vigor drained per second while gliding inside an updraft.
		float glideDrainInUpdraftPerSecond = 4.0f;

		/// Vigor cost to start gliding.
		float glideStartCost = 5.0f;

		/// Downward speed (units per second) the glider converges to.
		float glideSinkSpeed = 2.5f;

		/// Horizontal speed (units per second) while gliding with input.
		float glideForwardSpeed = 10.0f;

		/// Vertical acceleration (units per second squared) towards the target vertical speed.
		float glideVerticalAccel = 12.0f;

		/// Upward speed used for updrafts that do not define their own strength.
		float defaultUpdraftStrength = 8.0f;
	};

	/// What the character is doing with respect to Vigor drain.
	enum class VigorActivity : uint8
	{
		/// Nothing drains Vigor.
		Idle,

		/// Gliding outside of an updraft.
		Gliding,

		/// Gliding inside an updraft (reduced drain).
		GlidingInUpdraft,

		/// Only the explicit extra drain rate applies (for example a dash).
		Draining
	};

	/// Current Vigor of a character.
	struct VigorState
	{
		/// Current Vigor.
		float current = 100.0f;

		/// Maximum Vigor.
		float max = 100.0f;

		/// Seconds since Vigor last drained.
		float secondsSinceDrain = 0.0f;
	};

	/// Advances Vigor by the given time. Drain is the glide drain of the activity plus the extra drain.
	/// Regeneration only happens without drain, while grounded and out of combat, once the regen delay passed.
	void AdvanceVigor(VigorState& state, const TraversalSettings& settings, VigorActivity activity, float extraDrainPerSecond, bool inCombat, bool grounded, float seconds);

	/// Seconds until Vigor is empty with the given constant drain, or +infinity if nothing drains.
	float SecondsUntilEmpty(const VigorState& state, const TraversalSettings& settings, VigorActivity activity, float extraDrainPerSecond);

	/// An area in which the glider is lifted.
	struct UpdraftVolume
	{
		/// Center of the volume.
		Vector3 center;

		/// Sphere radius. If greater than zero, the volume is a sphere, otherwise an oriented box.
		float radius = 0.0f;

		/// Box half extents (only used if radius is not greater than zero).
		Vector3 halfExtents;

		/// Box facing around the Y axis, in radians.
		float orientation = 0.0f;

		/// Upward speed in this volume.
		float strength = 0.0f;
	};

	/// Gets the strongest updraft containing the position, or 0 if there is none (strength <= 0 means no updraft).
	float GetUpdraftStrength(const std::vector<UpdraftVolume>& volumes, const Vector3& position);

	/// Steps the velocity of a gliding character (Y is up). An updraftStrength <= 0 means no updraft.
	Vector3 StepGlideVelocity(const Vector3& velocity, const Vector3& desiredHorizontalDir, float updraftStrength, const TraversalSettings& settings, float seconds);
}
