// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/vector3.h"

#include <vector>

namespace mmo
{
	/// A camera position and the point it looks at.
	struct GlueCameraKey
	{
		Vector3 position;
		Vector3 lookAt;
	};

	/// Evaluates a Catmull-Rom path through the keys. t in [0,1] is clamped and eased with
	/// smoothstep. An empty list returns a default key, a single key returns that key.
	GlueCameraKey EvaluateGluePath(const std::vector<GlueCameraKey>& keys, float t);

	/// Current state of the glue camera.
	enum class GlueCameraMode
	{
		Gate,
		FlyingIn,
		Plaza,
		FlyingOut
	};

	/// Drives the camera path parameter between the gate (0) and the plaza (1).
	class GlueCameraDirector final
	{
	public:
		/// @param flightSeconds Duration of a full fly-in in seconds.
		/// @param flyOutSeconds Duration of a full fly-out in seconds; 0 or less uses flightSeconds.
		explicit GlueCameraDirector(float flightSeconds, float flyOutSeconds = 0.0f);

		/// Starts flying towards the plaza, continuing from the current point.
		void FlyIn();

		/// Starts flying back towards the gate, continuing from the current point.
		void FlyOut();

		/// Jumps to the end of the current flight.
		void Skip();

		/// Jumps to the plaza without a flight.
		void SnapToPlaza();

		/// Jumps to the gate without a flight.
		void SnapToGate();

		/// Advances the flight.
		void Update(float deltaSeconds);

		/// Gets the current mode.
		GlueCameraMode GetMode() const { return m_mode; }

		/// Gets the path parameter: 0 at the gate, 1 at the plaza.
		float GetPathParameter() const { return m_t; }

		/// Returns true once after a flight ended, naturally or skipped.
		bool ConsumeFlightFinished();

	private:
		float m_flightSeconds;
		float m_flyOutSeconds;
		GlueCameraMode m_mode = GlueCameraMode::Gate;
		float m_t = 0.0f;
		bool m_finished = false;
	};
}
