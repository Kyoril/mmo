// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include <functional>

namespace mmo
{
	/// @brief What a performance capture needs from the world it measures.
	/// @remarks Provided by the world state while it is active. Every member is optional; a capture
	///          that needs a missing one degrades instead of failing (no camera orbit, no location).
	struct PerfCaptureWorldHooks
	{
		/// Returns true once the world is loaded and the player is in it.
		std::function<bool()> isWorldReady;

		/// Turns the camera around the player by the given number of degrees.
		std::function<void(float degrees)> rotateCamera;

		/// Reports the current map and player position. Returns false if there is no player.
		std::function<bool(uint32& mapId, Vector3& position)> getLocation;
	};

	/// @brief Console commands that write machine-readable performance data.
	/// @details
	///   perfdump [file]          Writes the profiler's rolling averages, counters and the current
	///                            settings as JSON (default Logs/perf_snapshot.json).
	///   benchmark [key=value...] Waits for the world, warms up, then records every frame for a fixed
	///                            time while orbiting the camera, and writes frame-time percentiles,
	///                            per-metric averages and per-view-direction segments as JSON.
	///                            Keys: duration (s, 20), warmup (s, 5), timeout (s, 240),
	///                            orbit (degrees over the run, 360), segments (8), label (text),
	///                            out (file, Logs/benchmark.json), quit (0/1: exit when done).
	/// Together with the -exec command line argument this lets a script launch the client, measure a
	/// configuration and compare runs without anyone reading numbers off the screen.
	class PerfCapture final : public NonCopyable
	{
	public:
		/// @brief Registers the console commands and starts listening for frames.
		static void Initialize();

		/// @brief Unregisters the console commands. A running benchmark is abandoned.
		static void Destroy();

		/// @brief Installs the hooks of the world that is now active.
		static void SetWorldHooks(PerfCaptureWorldHooks hooks);

		/// @brief Removes the world hooks, e.g. when leaving the world.
		static void ClearWorldHooks();
	};
}
