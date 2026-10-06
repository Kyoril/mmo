#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

		/// Name of the thread that reported this metric. "Multiple" if more than
		/// one thread contributed to the same metric within a frame.
		std::string threadName;

		/// Time spent in this activity (accumulated over the frame).
		double totalTimeMs = 0.0;

		/// Number of times this activity was performed in the frame.
		uint64 callCount = 0;

		/// Average time in ms over the last MaxHistorySize frames this metric reported in.
		double averageTimeMs = 0.0;

		/// Maximum number of frames to keep in history.
		static constexpr size_t MaxHistorySize = 60;

		/// @brief Returns the average time in ms over the rolling history window.
		[[nodiscard]] double GetAverageTimeMs() const
		{
			return averageTimeMs;
		}
	};

	/// @brief Singleton profiler that collects per-frame timing metrics.
	///
	/// Threading contract: AddTime (and therefore PROFILE_SCOPE) may be called from any
	/// thread — each thread accumulates into its own buffer, so the hot path takes only an
	/// uncontended per-thread lock. BeginFrame, EndFrame and all getters must be called from
	/// the main thread only; EndFrame merges all per-thread buffers into the frame's metrics.
	/// Worker scopes are attributed to the frame in which they *end*.
	class Profiler
	{
	public:
		/// @brief Returns the global profiler singleton instance.
		static Profiler& GetInstance();

		/// @brief Enables or disables profiling. Main thread only.
		/// @param enabled Whether profiling is enabled.
		void SetEnabled(bool enabled);

		/// @brief Returns whether profiling is currently enabled.
		[[nodiscard]] bool IsEnabled() const { return m_enabled.load(std::memory_order_relaxed); }

		/// @brief Assigns a display name to the calling thread for metric attribution.
		/// @param name The thread name, e.g. "Main" or "mmo_worker_0".
		void SetCurrentThreadName(std::string name);

		/// @brief Call once at the start of each frame to reset per-frame metrics. Main thread only.
		void BeginFrame();

		/// @brief Call once at the end of each frame to merge, finalize and sort metrics. Main thread only.
		void EndFrame();

		/// @brief Adds measured time for a named metric. Safe to call from any thread.
		/// @param metricName The name of the metric to add time to.
		/// @param timeMs The time in milliseconds to add.
		void AddTime(std::string_view metricName, double timeMs);

		/// @brief Retrieves the sorted list of metrics from the last completed frame. Main thread only.
		[[nodiscard]] const std::vector<PerformanceMetric>& GetMetrics() const { return m_metrics; }

		/// @brief Returns the real frame time in milliseconds for the last completed frame.
		/// @remark This is the wall-clock period between successive frames and therefore
		///         includes Present()/VSync wait. It reflects the true displayed frame rate.
		[[nodiscard]] double GetFrameTimeMs() const { return m_frameTimeMs; }

		/// @brief Returns the CPU-side work time (Idle+Paint command building) for the last frame.
		/// @remark Compare with GetFrameTimeMs(): if the real frame time is much larger than the
		///         CPU time, the frame is GPU-bound (the CPU is waiting on Present/VSync).
		[[nodiscard]] double GetCpuFrameTimeMs() const { return m_cpuFrameTimeMs; }

		/// @brief Returns the current frames per second.
		[[nodiscard]] double GetFPS() const { return m_fps; }

		/// @brief Returns the rolling average frame time in ms over the history window.
		[[nodiscard]] double GetAverageFrameTimeMs() const;

		/// @brief Returns the rolling average FPS over the history window.
		[[nodiscard]] double GetAverageFPS() const;

		/// @brief Records a per-frame value that is not a summed duration (whole-frame GPU time,
		///        draw call count, ...). Main thread only. A counter keeps its last value until it
		///        is set again.
		/// @param name The counter name.
		/// @param value The value for the current frame.
		void SetCounter(std::string_view name, double value);

		/// @brief Returns all counters with their most recent values, sorted by name. Main thread only.
		[[nodiscard]] const std::map<std::string, double, std::less<>>& GetCounters() const { return m_counters; }

	private:
		/// @brief Transparent string hash for heterogeneous std::string_view lookups.
		struct StringViewHash
		{
			using is_transparent = void;

			size_t operator()(const std::string_view value) const
			{
				return std::hash<std::string_view>{}(value);
			}
		};

		/// @brief Per-frame accumulation of a single metric on a single thread.
		struct MetricAccumulator
		{
			double totalTimeMs = 0.0;
			uint64 callCount = 0;
		};

		/// @brief Accumulation buffer owned by one thread.
		///
		/// The owning thread holds a shared_ptr through a thread_local slot; the profiler
		/// holds a second one in m_threadBuffers. A use_count of 1 during EndFrame therefore
		/// means the owning thread has exited and the buffer can be pruned.
		struct ThreadBuffer
		{
			/// Guards metrics and threadName. Uncontended except during EndFrame's merge.
			std::mutex mutex;

			/// Display name of the owning thread.
			std::string threadName;

			/// Per-frame metric accumulation of the owning thread. Transparent hashing lets
			/// AddTime look up a scope's literal name without building a std::string.
			std::unordered_map<std::string, MetricAccumulator, StringViewHash, std::equal_to<>> metrics;
		};

		/// @brief Rolling history entry for a metric, persisted across frames.
		struct MetricHistory
		{
			std::deque<FrameData> history;

			/// Sum of history[].timeMs, maintained incrementally so averaging is O(1).
			double historyTimeSumMs = 0.0;

			/// Frame counter value when this metric last reported data (for pruning).
			uint64 lastSeenFrame = 0;
		};

		/// @brief Returns (and lazily registers) the calling thread's accumulation buffer.
		ThreadBuffer& GetThreadBuffer();

	private:
		/// Guards m_threadBuffers.
		std::mutex m_threadBuffersMutex;

		/// All registered per-thread buffers.
		std::vector<std::shared_ptr<ThreadBuffer>> m_threadBuffers;

		/// Sorted metrics from the last completed frame. Main thread only.
		std::vector<PerformanceMetric> m_metrics;

		/// Persistent per-metric rolling history. Main thread only.
		std::unordered_map<std::string, MetricHistory> m_metricHistory;

		/// Monotonic frame counter for history pruning.
		uint64 m_frameCounter = 0;

		/// Whether profiling is enabled.
		std::atomic<bool> m_enabled = false;

		/// Timestamp when BeginFrame was called.
		std::chrono::high_resolution_clock::time_point m_frameStartTime;

		/// Whether m_frameStartTime is valid (i.e. BeginFrame has been called at least once).
		bool m_frameStartValid = false;

		/// Timestamp of the previous BeginFrame, used to measure the real frame period.
		std::chrono::high_resolution_clock::time_point m_lastFrameBeginTime;

		/// Whether m_lastFrameBeginTime holds a valid timestamp yet.
		bool m_lastFrameBeginValid = false;

		/// Real frame time of the last completed frame, in ms (includes Present/VSync wait).
		double m_frameTimeMs = 0.0;

		/// CPU work time of the last frame (Idle+Paint), in ms — excludes Present/VSync.
		double m_cpuFrameTimeMs = 0.0;

		/// Current FPS derived from frame time.
		double m_fps = 0.0;

		/// Rolling history of frame times.
		std::deque<double> m_frameTimeHistory;

		/// Maximum frame time history size.
		static constexpr size_t MaxFrameTimeHistory = 60;

		/// Most recent value of every counter recorded through SetCounter. Main thread only.
		std::map<std::string, double, std::less<>> m_counters;
	};

	/// @brief RAII timer that measures the scope's lifetime and reports it to the Profiler.
	/// @remark Scopes run hundreds of times per frame even with the profiler off, so the timer
	///         neither allocates nor reads the clock unless profiling is enabled.
	class ScopedTimer final : public NonCopyable
	{
	public:
		/// @brief Constructs a scoped timer for the given metric name.
		/// @param metricName The name of the metric to measure. Must outlive the timer
		///        (PROFILE_SCOPE passes string literals).
		explicit ScopedTimer(const char* metricName);

		/// @brief Destructor that reports the elapsed time to the profiler.
		~ScopedTimer() override;

	private:
		const char* m_metricName;
		bool m_active;
		std::chrono::high_resolution_clock::time_point m_startTime;
	};

	// We use macros here because we want to disable profiling globally in final release builds
#define PROFILE_BEGIN_FRAME() Profiler::GetInstance().BeginFrame()
#define PROFILE_END_FRAME() Profiler::GetInstance().EndFrame()
#define PROFILE_SCOPE(name) ScopedTimer timer##__LINE__(name)
}
