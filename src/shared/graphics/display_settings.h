// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <chrono>
#include <string>

namespace mmo
{
	/// PCI vendor ids reported by graphics adapters.
	namespace gpu_vendor
	{
		enum Type : uint32
		{
			Unknown = 0,
			Amd = 0x1002,
			Nvidia = 0x10DE,
			Intel = 0x8086,
			/// Microsoft Basic Render Driver (WARP), a software rasterizer.
			Microsoft = 0x1414,
			Apple = 0x106B,
		};
	}

	/// Describes the graphics adapter the device renders with.
	struct GpuInfo final
	{
		/// Human readable adapter name, empty when the backend cannot tell.
		std::string name;
		/// PCI vendor id, see gpu_vendor.
		uint32 vendorId = gpu_vendor::Unknown;
		/// PCI device id.
		uint32 deviceId = 0;
		/// Video memory reserved for the GPU alone, in bytes. For integrated GPUs this is only the
		/// carve-out the firmware sets aside, which says little about their speed.
		uint64 dedicatedVideoMemory = 0;
		/// True if the GPU shares the system memory with the CPU (integrated graphics).
		bool integrated = false;
		/// True if the adapter is a software rasterizer.
		bool software = false;
	};

	/// Graphics quality presets, in the order of the gxQuality cvar.
	namespace graphics_preset
	{
		enum Type
		{
			Low = 0,
			Medium = 1,
			High = 2,
			Ultra = 3,
		};
	}

	/// @brief Picks the graphics quality preset a player with the given hardware should start with.
	/// @param gpu The adapter the game renders with.
	/// @param displayWidth Width of the display the game is shown on, in pixels.
	/// @param displayHeight Height of the display the game is shown on, in pixels.
	/// @return One of graphics_preset::Type.
	/// @remarks A heuristic, not a benchmark: integrated GPUs start low (a few strong ones at Medium),
	///          dedicated ones by video memory, which tracks the GPU generation and class well enough.
	///          Displays above 1440p cost one step, since every pass scales with the pixel count.
	int RecommendGraphicsPreset(const GpuInfo& gpu, uint32 displayWidth, uint32 displayHeight);

	/// @brief Returns whether the given device is one of the integrated GPUs that hold the Medium preset.
	bool IsStrongIntegratedGpu(uint32 vendorId, uint32 deviceId);

	/// @brief Paces frames to a maximum frame rate.
	/// @remarks Only computes deadlines; sleeping is up to the caller, which knows the platform's most
	///          precise way of waiting. Deadlines advance by a fixed interval so the average rate hits
	///          the limit exactly, and they snap back to now when a frame overran, instead of rushing the
	///          following frames to catch up.
	class FramePacer final
	{
	public:
		using Clock = std::chrono::steady_clock;

	public:
		/// @brief Computes when the next frame may start.
		/// @param now The current time, taken after the frame was presented.
		/// @param maxFps The frame rate limit. Zero or less means unlimited.
		/// @return The time to wait for. Equal to now when no waiting is needed.
		Clock::time_point NextFrameStart(Clock::time_point now, float maxFps);

		/// @brief Forgets the last deadline, e.g. after the limit changed.
		void Reset() { m_hasDeadline = false; }

	private:
		Clock::time_point m_lastDeadline{};
		bool m_hasDeadline = false;
	};

	/// @brief Lowers the 3D render resolution while the GPU cannot hold a target frame rate, and raises it
	///        back once there is headroom again.
	/// @remarks Fed with the GPU time per frame, so a CPU-bound frame (where a lower resolution would not
	///          help) never lowers the scale. The scale moves in coarse steps with hysteresis because every
	///          change reallocates the render targets.
	class DynamicResolutionController final
	{
	public:
		/// Smallest scale the controller goes down to.
		static constexpr float MinScale = 0.5f;
		/// Scale change per step.
		static constexpr float Step = 0.1f;

	public:
		/// @brief Sets the frame rate to hold. Zero or less disables the controller (scale 1).
		void SetTargetFps(float fps);

		/// @brief Returns the frame rate the controller tries to hold, or zero when disabled.
		[[nodiscard]] float GetTargetFps() const { return m_targetFps; }

		/// @brief Feeds one frame into the controller.
		/// @param frameSeconds Wall time of the frame.
		/// @param gpuMilliseconds GPU time of a recent frame. Negative when no measurement is available.
		/// @return True if the scale changed.
		bool Update(double frameSeconds, double gpuMilliseconds);

		/// @brief Returns the factor to multiply the player's render scale with, in [MinScale, 1].
		[[nodiscard]] float GetScale() const { return m_scale; }

	private:
		void ResetWindow();

	private:
		float m_targetFps = 0.0f;
		float m_scale = 1.0f;

		double m_windowSeconds = 0.0;
		double m_windowGpuMs = 0.0;
		uint32 m_windowSamples = 0;

		uint32 m_slowWindows = 0;
		uint32 m_fastWindows = 0;
	};
}
