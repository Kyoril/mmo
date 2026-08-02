
#include "profiler.h"

#include <sstream>
#include <thread>

namespace mmo
{
	Profiler& Profiler::GetInstance()
	{
		static Profiler instance;
		return instance;
	}

	Profiler::ThreadBuffer& Profiler::GetThreadBuffer()
	{
		// Owning reference of the calling thread; released automatically on thread exit,
		// which drops the buffer's use_count to 1 so EndFrame can prune it.
		static thread_local std::shared_ptr<ThreadBuffer> t_buffer;

		if (!t_buffer)
		{
			t_buffer = std::make_shared<ThreadBuffer>();

			std::ostringstream name;
			name << "Thread " << std::this_thread::get_id();
			t_buffer->threadName = name.str();

			std::scoped_lock lock{ m_threadBuffersMutex };
			m_threadBuffers.push_back(t_buffer);
		}

		return *t_buffer;
	}

	void Profiler::SetCurrentThreadName(std::string name)
	{
		ThreadBuffer& buffer = GetThreadBuffer();

		std::scoped_lock lock{ buffer.mutex };
		buffer.threadName = std::move(name);
	}

	void Profiler::BeginFrame()
	{
		if (!IsEnabled())
		{
			return;
		}

		// The main thread is the one driving frames — name its buffer accordingly
		// unless it was named explicitly already.
		{
			ThreadBuffer& buffer = GetThreadBuffer();
			std::scoped_lock lock{ buffer.mutex };
			if (buffer.threadName.compare(0, 7, "Thread ") == 0)
			{
				buffer.threadName = "Main";
			}
		}

		const auto now = std::chrono::high_resolution_clock::now();

		// The real frame time is the wall-clock period between successive frame starts. Because
		// BeginFrame runs at the top of OnIdle and the next BeginFrame only happens after the full
		// loop iteration (Idle -> Paint -> Present/VSync), this period correctly includes the
		// Present()/VSync wait that the old begin->end measurement missed. When the GPU is the
		// bottleneck (e.g. an integrated GPU), this is the only honest frame-rate number.
		if (m_lastFrameBeginValid)
		{
			m_frameTimeMs = std::chrono::duration<double, std::milli>(now - m_lastFrameBeginTime).count();
			m_fps = (m_frameTimeMs > 0.0) ? (1000.0 / m_frameTimeMs) : 0.0;

			// Update frame time history for rolling averages
			m_frameTimeHistory.push_back(m_frameTimeMs);
			while (m_frameTimeHistory.size() > MaxFrameTimeHistory)
			{
				m_frameTimeHistory.pop_front();
			}
		}
		m_lastFrameBeginTime = now;
		m_lastFrameBeginValid = true;

		m_frameStartTime = now;
		m_frameStartValid = true;
	}

	void Profiler::EndFrame()
	{
		if (!IsEnabled())
		{
			return;
		}

		// Measure the CPU-side work time (command building) separately from the real frame time.
		// A large gap between the two (real >> CPU) means the frame is GPU-bound.
		if (m_frameStartValid)
		{
			const auto now = std::chrono::high_resolution_clock::now();
			m_cpuFrameTimeMs = std::chrono::duration<double, std::milli>(now - m_frameStartTime).count();
		}

		++m_frameCounter;

		// Merge all per-thread buffers into a single per-metric view for this frame.
		struct MergedMetric
		{
			double totalTimeMs = 0.0;
			uint64 callCount = 0;
			std::string threadName;
			bool multipleThreads = false;
		};
		std::unordered_map<std::string, MergedMetric> merged;

		{
			std::scoped_lock buffersLock{ m_threadBuffersMutex };

			for (auto it = m_threadBuffers.begin(); it != m_threadBuffers.end();)
			{
				const auto& buffer = *it;

				{
					std::scoped_lock bufferLock{ buffer->mutex };
					for (auto& [name, accumulator] : buffer->metrics)
					{
						MergedMetric& target = merged[name];
						target.totalTimeMs += accumulator.totalTimeMs;
						target.callCount += accumulator.callCount;
						if (target.threadName.empty())
						{
							target.threadName = buffer->threadName;
						}
						else if (target.threadName != buffer->threadName)
						{
							target.multipleThreads = true;
						}
					}
					buffer->metrics.clear();
				}

				// A use_count of 1 means the owning thread has exited — prune the buffer.
				if (it->use_count() == 1)
				{
					it = m_threadBuffers.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		// Build the sorted metric list, maintaining the persistent rolling history.
		m_metrics.clear();
		m_metrics.reserve(merged.size());
		for (auto& [name, data] : merged)
		{
			MetricHistory& history = m_metricHistory[name];
			history.lastSeenFrame = m_frameCounter;
			history.history.push_back(FrameData{ data.totalTimeMs, static_cast<int>(data.callCount) });
			while (history.history.size() > PerformanceMetric::MaxHistorySize)
			{
				history.history.pop_front();
			}

			PerformanceMetric metric;
			metric.name = name;
			metric.threadName = data.multipleThreads ? "Multiple" : data.threadName;
			metric.totalTimeMs = data.totalTimeMs;
			metric.callCount = data.callCount;
			metric.history = history.history;
			m_metrics.push_back(std::move(metric));
		}

		// Drop history for metrics that stopped reporting (e.g. after leaving a game state)
		// so dynamically named scopes can't grow the map without bound.
		for (auto it = m_metricHistory.begin(); it != m_metricHistory.end();)
		{
			if (m_frameCounter - it->second.lastSeenFrame > PerformanceMetric::MaxHistorySize)
			{
				it = m_metricHistory.erase(it);
			}
			else
			{
				++it;
			}
		}

		// Sort metrics by total time descending (most expensive first)
		std::sort(m_metrics.begin(), m_metrics.end(),
			[](const PerformanceMetric& a, const PerformanceMetric& b)
			{
				return a.totalTimeMs > b.totalTimeMs;
			});
	}

	void Profiler::AddTime(const std::string& metricName, const double timeMs)
	{
		if (!IsEnabled())
		{
			return;
		}

		ThreadBuffer& buffer = GetThreadBuffer();

		std::scoped_lock lock{ buffer.mutex };
		MetricAccumulator& accumulator = buffer.metrics[metricName];
		accumulator.totalTimeMs += timeMs;
		accumulator.callCount++;
	}

	double Profiler::GetAverageFrameTimeMs() const
	{
		if (m_frameTimeHistory.empty())
		{
			return m_frameTimeMs;
		}

		double sum = 0.0;
		for (const double t : m_frameTimeHistory)
		{
			sum += t;
		}
		return sum / static_cast<double>(m_frameTimeHistory.size());
	}

	double Profiler::GetAverageFPS() const
	{
		const double avgTime = GetAverageFrameTimeMs();
		return (avgTime > 0.0) ? (1000.0 / avgTime) : 0.0;
	}

	ScopedTimer::ScopedTimer(std::string metricName)
		: m_metricName(std::move(metricName))
		, m_startTime(std::chrono::high_resolution_clock::now())
	{
	}

	ScopedTimer::~ScopedTimer()
	{
		auto endTime = std::chrono::high_resolution_clock::now();
		// Calculate the duration in microseconds, then convert to milliseconds
		double elapsedMs = std::chrono::duration<double, std::milli>(endTime - m_startTime).count();

		// Send the measured time to the profiler
		Profiler::GetInstance().AddTime(m_metricName, elapsedMs);
	}
}
