
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

		m_metricsMap.clear();
	}

	void Profiler::EndFrame()
	{
		if (!m_enabled)
		{
			return;
		}

		// Transfer the data from the map into a vector for stable ordering
		m_metrics.clear();
		m_metrics.reserve(m_metricsMap.size());
		for (auto& kv : m_metricsMap)
		{
			m_metrics.push_back(kv.second);
		}
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
