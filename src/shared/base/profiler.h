#pragma once

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

#include "non_copyable.h"
#include "typedefs.h"

namespace mmo
{
	struct FrameData
	{
		double timeMs;
		int callCount;
	};

	struct PerformanceMetric
	{
		// Identifier for the metric, e.g., "Physics Update", "Render Pass"
		std::string name;

		// Time spent in this activity (accumulated)
		double totalTimeMs = 0.0;

		// Number of times this activity was performed in the frame
		uint64 callCount = 0;

		std::deque<FrameData> history;

		static constexpr size_t MaxHistorySize = 60;
	};

	class Profiler
	{
	public:
		static Profiler& GetInstance();

		void SetEnabled(bool enabled) { m_enabled = enabled; }

		// Call once per frame to reset or finalize results
		void BeginFrame();
		void EndFrame();

		// Start tracking for a given metric (by name or ID)
		void AddTime(const std::string& metricName, double timeMs);

		// Retrieve the stored metrics
		[[nodiscard]] const std::vector<PerformanceMetric>& GetMetrics() const { return m_metrics; }

	private:
		// Internal storage of metrics
		std::unordered_map<std::string, PerformanceMetric> m_metricsMap;
		std::vector<PerformanceMetric> m_metrics;

		// Possibly some stack to track nested calls for Start/End
		// Or a map from name -> last start time
		std::unordered_map<std::string, double> m_startTimes;
		bool m_enabled = false;
	};

	class ScopedTimer final : public NonCopyable
	{
	public:
		explicit ScopedTimer(std::string metricName);
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
