
#include "profiler.h"

namespace mmo
{
	Profiler& Profiler::GetInstance()
	{
		static Profiler instance;
		return instance;
	}

	void Profiler::BeginFrame()
	{
		if (!m_enabled)
		{
			return;
		}

		m_frameStartTime = std::chrono::high_resolution_clock::now();
		m_frameStartValid = true;
		m_metricsMap.clear();
	}

	void Profiler::EndFrame()
	{
		if (!m_enabled)
		{
			return;
		}

		// Compute frame time
		if (m_frameStartValid)
		{
			const auto now = std::chrono::high_resolution_clock::now();
			m_frameTimeMs = std::chrono::duration<double, std::milli>(now - m_frameStartTime).count();
			m_fps = (m_frameTimeMs > 0.0) ? (1000.0 / m_frameTimeMs) : 0.0;

			// Update frame time history for rolling averages
			m_frameTimeHistory.push_back(m_frameTimeMs);
			while (m_frameTimeHistory.size() > MaxFrameTimeHistory)
			{
				m_frameTimeHistory.pop_front();
			}
		}

		// Transfer the data from the map into a vector, updating history
		m_metrics.clear();
		m_metrics.reserve(m_metricsMap.size());
		for (auto& [name, metric] : m_metricsMap)
		{
			// Push this frame's data into the metric's history
			metric.history.push_back(FrameData{ metric.totalTimeMs, static_cast<int>(metric.callCount) });
			while (metric.history.size() > PerformanceMetric::MaxHistorySize)
			{
				metric.history.pop_front();
			}

			m_metrics.push_back(metric);
		}

		// Sort metrics by total time descending (most expensive first)
		std::sort(m_metrics.begin(), m_metrics.end(),
			[](const PerformanceMetric& a, const PerformanceMetric& b)
			{
				return a.totalTimeMs > b.totalTimeMs;
			});
	}

	void Profiler::AddTime(const std::string& metricName, double timeMs)
	{
		if (!m_enabled)
		{
			return;
		}

		auto& metric = m_metricsMap[metricName];
		metric.name = metricName;
		metric.totalTimeMs += timeMs;
		metric.callCount++;
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
