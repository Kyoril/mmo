// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Tunable settings for the screen-space contact shadow term.
	/// @remark Contact shadows are marched inline in the deferred lighting pixel shader rather than
	///         in a pass of their own, so unlike SsaoSettings this struct has no object to attach to
	///         - it lives directly on the DeferredRenderer and is uploaded through the ShadowBuffer.
	/// @remark Deliberately free of any graphics dependency so it can be compiled into the
	///         headless unit_tests target. All distances are in world units (1 unit = 1 metre).
	struct ContactShadowSettings
	{
		/// @brief Whether the contact shadow march runs at all.
		bool enabled = true;

		/// @brief Whether to output the raw contact shadow term to the screen for inspection.
		bool debugVisualization = false;

		/// @brief Length of the ray marched toward the sun, in metres.
		/// @remark Default 0.3m. This effect exists to fill the last few centimetres the cascaded
		///         shadow maps cannot resolve, so the reach is deliberately short: a longer ray is
		///         both slower and noisier at a given step count, and duplicates work the CSM is
		///         already doing correctly.
		float rayLength = 0.3f;

		/// @brief Assumed occluder thickness in metres.
		/// @remark Default 0.2m. The depth buffer is a heightfield - only the front surface is
		///         known - so a ray passing behind a stored surface needs a bounded window to
		///         decide "inside that object" from "far behind it". Too small and occluders stop
		///         registering; too large and distant background geometry shadows the foreground.
		float thickness = 0.2f;

		/// @brief Strength of the term. 1 = a hit shadows fully, 0 = the effect contributes nothing.
		float intensity = 1.0f;

		/// @brief Distance the ray origin is pushed along the surface normal, in metres.
		/// @remark Guards against the surface shadowing itself at the first step.
		float normalBias = 0.02f;

		/// @brief Distance in metres at which the term starts fading out. Derived from fadeEnd.
		float fadeStart = 21.0f;

		/// @brief Distance in metres at which the term has fully faded out.
		float fadeEnd = 35.0f;

		/// @brief Number of steps marched per pixel. Driven by the quality preset.
		uint32 stepCount = 8;

		/// @brief Applies a coarse quality preset trading visual quality for performance.
		/// @param level 0 = Low, 1 = Medium, 2 = High. Out-of-range values clamp.
		void ApplyQualityLevel(int level)
		{
			if (level <= 0)
			{
				stepCount = 4;
			}
			else if (level == 1)
			{
				stepCount = 8;
			}
			else
			{
				stepCount = 16;
			}
		}

		/// @brief Sets the ray length in metres, clamped to a sane range.
		void SetRayLength(float value)
		{
			rayLength = Clamp(value, 0.02f, 2.0f);
		}

		/// @brief Sets the assumed occluder thickness in metres, clamped to a sane range.
		void SetThickness(float value)
		{
			thickness = Clamp(value, 0.01f, 2.0f);
		}

		/// @brief Sets the strength of the term, clamped to [0, 1].
		void SetIntensity(float value)
		{
			intensity = Clamp(value, 0.0f, 1.0f);
		}

		/// @brief Sets the ray origin's normal bias in metres, clamped to a sane range.
		void SetNormalBias(float value)
		{
			normalBias = Clamp(value, 0.0f, 0.5f);
		}

		/// @brief Sets the distance at which the term has fully faded out, in metres.
		/// @remark fadeStart is derived at a fixed ratio rather than set independently, so the two
		///         can never be inverted or made equal - the shader divides by their difference.
		/// @remark The 60m ceiling is not taste. GBuffer_Normal is RGBA16F, so the radial depth in
		///         its alpha is a half float, whose ULP grows with distance (~16mm at 35m, ~63mm at
		///         100m). That is the same order as the thickness window being tested, so beyond a
		///         few tens of metres the term would be reading quantization noise rather than
		///         geometry. The effect is invisible out there anyway: a 0.3m ray subtends well
		///         under a pixel.
		void SetFadeDistance(float end)
		{
			fadeEnd = Clamp(end, 2.0f, 60.0f);
			fadeStart = fadeEnd * 0.6f;
		}

		/// @brief Returns the step count the shader should actually march, folding in `enabled`.
		/// @remark The single value the shader branches on. Making "disabled" representable in the
		///         data the shader already reads means it needs no permutation and no extra flag -
		///         the same trick SsaoPass::GetResult() plays by handing out a 1x1 white texture
		///         when it is switched off. Zero is unreachable from ApplyQualityLevel, so there is
		///         exactly one way to express "off" and it is unit-testable.
		[[nodiscard]] uint32 GetEffectiveStepCount() const
		{
			return enabled ? stepCount : 0u;
		}

	private:
		/// @brief Local clamp helper. Deliberately not pulling in a math header to keep this
		///        struct dependency-free for the headless unit_tests build.
		static float Clamp(float value, float minValue, float maxValue)
		{
			if (value < minValue)
			{
				return minValue;
			}

			if (value > maxValue)
			{
				return maxValue;
			}

			return value;
		}
	};
}
