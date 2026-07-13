// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <vector>

#include "base/typedefs.h"

namespace mmo
{
	class AnimationState;

	/// @brief A clip with a blend weight, the output unit of the animation controller's
	///	locomotion evaluation.
	struct WeightedAnimClip
	{
		AnimationState* state{nullptr};
		float weight{0.0f};
	};

	/// @brief 1D directional blend space over the movement direction angle. Samples are
	///	animation clips placed at angles around the unit (0 = forward, 90 = right, 180 =
	///	backward, 270 = left). Evaluation blends the two samples adjacent to the requested
	///	angle; samples farther apart than the max blend angle snap to the nearest sample
	///	instead (discrete behavior for sparsely authored clip sets).
	class AnimationBlendSpace final
	{
	public:
		/// @brief One directional sample.
		struct Sample
		{
			float angle{0.0f};
			AnimationState* state{nullptr};
		};

	public:
		/// @brief Removes all samples.
		void Clear();

		/// @brief Adds a sample at the given angle (degrees, normalized to [0, 360)). A later
		///	sample at the same angle replaces the earlier one.
		void AddSample(float angleDegrees, AnimationState& state);

		/// @brief Sorts samples by angle. Must be called after the last AddSample and before
		///	the first Evaluate.
		void Finalize();

		/// @brief Sets the maximum angular gap (degrees) between adjacent samples that still blends.
		void SetMaxBlendAngle(const float degrees) { m_maxBlendAngle = degrees; }

		/// @brief Returns true when the blend space has no samples.
		[[nodiscard]] bool IsEmpty() const { return m_samples.empty(); }

		/// @brief Evaluates the blend space at the given direction angle.
		/// @param angleDegrees Movement direction in degrees (0 = forward, 90 = right, ...).
		/// @param out Receives one or two weighted clips (weights sum to 1).
		/// @return Number of clips written to @p out (0 when the blend space is empty).
		uint32 Evaluate(float angleDegrees, WeightedAnimClip out[2]) const;

	private:
		std::vector<Sample> m_samples;
		float m_maxBlendAngle{100.0f};
	};
}
