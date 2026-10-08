// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	class RenderWindow;

	/// @brief Frame rate limits and the dynamic render scale behind the frame rate threshold option.
	/// @remarks Owns the cvars gxMaxFpsEnabled / gxMaxFps (limit while the game window is in front),
	///          gxMaxFpsBkEnabled / gxMaxFpsBk (limit while it is in the background or minimized) and
	///          gxTargetFpsEnabled / gxTargetFps (lower the 3D resolution while the GPU cannot hold it).
	class FramePacing final
	{
	public:
		/// @brief Registers the cvars. Call after the config file has been executed.
		static void Initialize();

		/// @brief Disconnects from the cvars.
		static void Destroy();

		/// @brief Returns whether whole-frame GPU timing has to run, which the threshold needs.
		[[nodiscard]] static bool NeedsGpuTiming();

		/// @brief Finishes a frame: updates the dynamic render scale and waits as long as the active
		///        frame rate limit demands. Call right after the frame was presented.
		/// @param window The game window, asked whether it is in the foreground.
		/// @param frameSeconds Wall time of the frame that was just presented.
		/// @param gpuMilliseconds GPU time of a recent frame, negative if unknown.
		static void EndFrame(const RenderWindow& window, float frameSeconds, double gpuMilliseconds);

		/// @brief Returns the factor the frame rate threshold applies on top of the player's render
		///        scale, in [0.5, 1]. Always 1 while the threshold is off.
		[[nodiscard]] static float GetDynamicRenderScale();
	};
}
