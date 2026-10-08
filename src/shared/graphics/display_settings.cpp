// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "display_settings.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		constexpr uint64 MiB = 1024ull * 1024ull;

		/// Integrated GPUs known to hold the Medium preset at 1080p. Missing one only costs the player a
		/// Low recommendation they can raise themselves, so the list does not need to be complete.
		struct StrongIntegratedGpu
		{
			uint32 vendorId;
			uint32 deviceId;
		};

		constexpr StrongIntegratedGpu s_strongIntegratedGpus[] =
		{
			{ gpu_vendor::Amd, 0x15BF },	// Radeon 760M / 780M (Phoenix)
			{ gpu_vendor::Amd, 0x1900 },	// Radeon 760M / 780M (Hawk Point)
			{ gpu_vendor::Amd, 0x150E },	// Radeon 880M / 890M (Strix Point)
			{ gpu_vendor::Amd, 0x1586 },	// Radeon 8050S / 8060S (Strix Halo)
			{ gpu_vendor::Intel, 0x7D55 },	// Arc Graphics (Meteor Lake)
			{ gpu_vendor::Intel, 0x7DD5 },	// Arc Graphics (Meteor Lake)
			{ gpu_vendor::Intel, 0x64A0 },	// Arc 130V / 140V (Lunar Lake)
		};

		/// Displays with more pixels than this (a little above 1440p) cost one preset step.
		constexpr uint64 HighResolutionPixels = 2560ull * 1440ull * 6ull / 5ull;
	}

	bool IsStrongIntegratedGpu(const uint32 vendorId, const uint32 deviceId)
	{
		return std::any_of(std::begin(s_strongIntegratedGpus), std::end(s_strongIntegratedGpus), [&](const StrongIntegratedGpu& gpu)
		{
			return gpu.vendorId == vendorId && gpu.deviceId == deviceId;
		});
	}

	int RecommendGraphicsPreset(const GpuInfo& gpu, const uint32 displayWidth, const uint32 displayHeight)
	{
		if (gpu.software || gpu.vendorId == gpu_vendor::Microsoft)
		{
			return graphics_preset::Low;
		}

		int preset;
		if (gpu.vendorId == gpu_vendor::Unknown)
		{
			// The backend could not identify the adapter: Medium runs everywhere we have measured.
			preset = graphics_preset::Medium;
		}
		else if (gpu.integrated || gpu.vendorId == gpu_vendor::Apple ||
			((gpu.vendorId == gpu_vendor::Intel || gpu.vendorId == gpu_vendor::Amd) && gpu.dedicatedVideoMemory < 512 * MiB))
		{
			// The last case catches integrated GPUs the backend could not flag as such: they only
			// report the small firmware carve-out as their own memory.
			preset = IsStrongIntegratedGpu(gpu.vendorId, gpu.deviceId) ? graphics_preset::Medium : graphics_preset::Low;
		}
		else if (gpu.dedicatedVideoMemory < 1536 * MiB)
		{
			preset = graphics_preset::Low;
		}
		else if (gpu.dedicatedVideoMemory < 3584 * MiB)
		{
			preset = graphics_preset::Medium;
		}
		else if (gpu.dedicatedVideoMemory < 7168 * MiB)
		{
			preset = graphics_preset::High;
		}
		else
		{
			preset = graphics_preset::Ultra;
		}

		if (static_cast<uint64>(displayWidth) * displayHeight > HighResolutionPixels)
		{
			preset = std::max(static_cast<int>(graphics_preset::Low), preset - 1);
		}

		return preset;
	}

	FramePacer::Clock::time_point FramePacer::NextFrameStart(const Clock::time_point now, const float maxFps)
	{
		if (maxFps <= 0.0f)
		{
			m_hasDeadline = false;
			return now;
		}

		const auto interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / static_cast<double>(maxFps)));
		if (!m_hasDeadline)
		{
			m_hasDeadline = true;
			m_lastDeadline = now;
			return now;
		}

		Clock::time_point deadline = m_lastDeadline + interval;
		if (deadline <= now)
		{
			// The frame took longer than the interval. Start the next one right away and measure from
			// here, rather than letting the following frames run unlimited to make up for it.
			deadline = now;
		}

		m_lastDeadline = deadline;
		return deadline;
	}

	void DynamicResolutionController::SetTargetFps(const float fps)
	{
		m_targetFps = std::max(0.0f, fps);
		m_slowWindows = 0;
		m_fastWindows = 0;
		ResetWindow();
	}

	bool DynamicResolutionController::Update(const double frameSeconds, const double gpuMilliseconds)
	{
		if (m_targetFps <= 0.0f)
		{
			if (m_scale != 1.0f)
			{
				m_scale = 1.0f;
				return true;
			}

			return false;
		}

		// Without GPU timing the frame time has to do, which also reacts to CPU-bound frames.
		m_windowGpuMs += gpuMilliseconds >= 0.0 ? gpuMilliseconds : frameSeconds * 1000.0;
		m_windowSeconds += frameSeconds;
		++m_windowSamples;

		// Judge half-second windows: single frames are far too noisy (loading hitches, GPU timer lag).
		if (m_windowSeconds < 0.5)
		{
			return false;
		}

		const double average = m_windowGpuMs / static_cast<double>(m_windowSamples);
		ResetWindow();

		const double budget = 1000.0 / static_cast<double>(m_targetFps);
		if (average > budget * 0.95)
		{
			++m_slowWindows;
			m_fastWindows = 0;
		}
		else if (average < budget * 0.8)
		{
			++m_fastWindows;
			m_slowWindows = 0;
		}
		else
		{
			m_slowWindows = 0;
			m_fastWindows = 0;
		}

		// Snap to tenths so that repeated steps never accumulate float error.
		const auto snap = [](const float scale) { return std::round(scale * 10.0f) / 10.0f; };

		// Drop quickly (one second too slow), recover slowly (two seconds of headroom).
		if (m_slowWindows >= 2 && m_scale > MinScale)
		{
			m_scale = std::max(MinScale, snap(m_scale - Step));
			m_slowWindows = 0;
			return true;
		}

		if (m_fastWindows >= 4 && m_scale < 1.0f)
		{
			m_fastWindows = 0;

			// GPU time grows with the pixel count, i.e. with the square of the scale. Only step up if
			// the larger frame is predicted to fit comfortably, or the scale would bounce.
			const float next = std::min(1.0f, snap(m_scale + Step));
			const double growth = static_cast<double>(next / m_scale);
			if (average * growth * growth < budget * 0.9)
			{
				m_scale = next;
				return true;
			}
		}

		return false;
	}

	void DynamicResolutionController::ResetWindow()
	{
		m_windowSeconds = 0.0;
		m_windowGpuMs = 0.0;
		m_windowSamples = 0;
	}
}
