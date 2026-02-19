#pragma once

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

#include "non_copyable.h"
#include "typedefs.h"

namespace mmo
{
	/// @brief Stores a single frame's data for a profiling metric.
	struct FrameData
	{
		double timeMs;
		int callCount;
	};

	/// @brief Stores accumulated profiling data for a single named metric.
	struct PerformanceMetric
	{
		/// Identifier for the metric, e.g., "Physics Update", "Render Pass".
		std::string name;

		/// Time spent in this activity (accumulated over the frame).
		double totalTimeMs = 0.0;

		/// Number of times this activity was performed in the frame.
		uint64 callCount = 0;

		/// Rolling history of the last MaxHistorySize frames for this metric.
		std::deque<FrameData> history;

		/// Maximum number of frames to keep in history.
		static constexpr size_t MaxHistorySize = 60;

		/// @brief Returns the average time in ms over the stored history.
		[[nodiscard]] double GetAverageTimeMs() const
		{
			if (history.empty())
			{
				return totalTimeMs;
			}

			double sum = 0.0;
			for (const auto& frame : history)
			{
				sum += frame.timeMs;
			}
			return sum / static_cast<double>(history.size());
		}
	};

	/// @brief Singleton profiler that collects per-frame timing metrics.
	class Profiler
	{
	public:
		/// @brief Returns the global profiler singleton instance.
		static Profiler& GetInstance();

		/// @brief Enables or disables profiling.
		/// @param enabled Whether profiling is enabled.
		void SetEnabled(bool enabled) { m_enabled = enabled; }

		/// @brief Returns whether profiling is currently enabled.
		[[nodiscard]] bool IsEnabled() const { return m_enabled; }

		/// @brief Call once at the start of each frame to reset per-frame metrics.
		void BeginFrame();

		/// @brief Call once at the end of each frame to finalize and sort metrics.
		void EndFrame();

		/// @brief Adds measured time for a named metric.
		/// @param metricName The name of the metric to add time to.
		/// @param timeMs The time in milliseconds to add.
		void AddTime(const std::string& metricName, double timeMs);

		/// @brief Retrieves the sorted list of metrics from the last completed frame.
		[[nodiscard]] const std::vector<PerformanceMetric>& GetMetrics() const { return m_metrics; }

		/// @brief Returns the total frame time in milliseconds for the last completed frame.
		[[nodiscard]] double GetFrameTimeMs() const { return m_frameTimeMs; }

		/// @brief Returns the current frames per second.
		[[nodiscard]] double GetFPS() const { return m_fps; }

		/// @brief Returns the rolling average frame time in ms over the history window.
		[[nodiscard]] double GetAverageFrameTimeMs() const;

		/// @brief Returns the rolling average FPS over the history window.
		[[nodiscard]] double GetAverageFPS() const;

	private:
		/// Internal per-frame accumulation map.
		std::unordered_map<std::string, PerformanceMetric> m_metricsMap;

		/// Sorted metrics from the last completed frame.
		std::vector<PerformanceMetric> m_metrics;

		/// Start times for named metrics (unused currently, reserved for Start/Stop API).
		std::unordered_map<std::string, double> m_startTimes;

		/// Whether profiling is enabled.
		bool m_enabled = false;

		/// Timestamp when BeginFrame was called.
		std::chrono::high_resolution_clock::time_point m_frameStartTime;

		/// Whether m_frameStartTime is valid (i.e. BeginFrame has been called at least once).
		bool m_frameStartValid = false;

		/// Total frame time of the last completed frame, in ms.
		double m_frameTimeMs = 0.0;

		/// Current FPS derived from frame time.
		double m_fps = 0.0;

		/// Rolling history of frame times.
		std::deque<double> m_frameTimeHistory;

		/// Maximum frame time history size.
		static constexpr size_t MaxFrameTimeHistory = 60;
	};

	/// @brief RAII timer that measures the scope's lifetime and reports it to the Profiler.
	class ScopedTimer final : public NonCopyable
	{
	public:
		/// @brief Constructs a scoped timer for the given metric name.
		/// @param metricName The name of the metric to measure.
		explicit ScopedTimer(std::string metricName);

		/// @brief Destructor that reports the elapsed time to the profiler.
		~ScopedTimer() override;

	private:
		std::string m_metricName;
		std::chrono::high_resolution_clock::time_point m_startTime;
	};

	// We use macros here because we want to disable profiling globally in final release builds
#define PROFILE_BEGIN_FRAME() Profiler::GetInstance().BeginFrame()
#define PROFILE_END_FRAME() Profiler::GetInstance().EndFrame()
#define PROFILE_SCOPE(name) ScopedTimer timer##__LINE__(name)
}
