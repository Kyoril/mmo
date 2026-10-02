// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "page_streamer.h"

#include "log/default_log_levels.h"
#include "terrain/constants.h"

#include <algorithm>
#include <cmath>

namespace mmo::nav
{
	namespace
	{
		constexpr int32 MaxPages = static_cast<int32>(terrain::constants::MaxPages);

		/// Page column/row containing a world X or Z coordinate, clamped to the page grid. The
		/// nav mesh origin sits at -32 pages on both axes (see Map's constructor); page x runs
		/// along world X and page y along world Z.
		int32 WorldToPage(const float coordinate)
		{
			constexpr double origin = -32.0 * terrain::constants::PageSize;
			const auto page = static_cast<int32>(std::floor((coordinate - origin) / terrain::constants::PageSize));
			return std::clamp(page, 0, MaxPages - 1);
		}
	}

	PageStreamer::PageStreamer(Map& map)
		: m_map(map)
		, m_mapName(map.GetName())
		, m_claimed(std::make_unique<std::atomic<bool>[]>(terrain::constants::MaxPagesSquared))
		, m_resolved(terrain::constants::MaxPagesSquared, false)
	{
		if (m_map.HasPages())
		{
			for (int32 y = 0; y < MaxPages; ++y)
			{
				for (int32 x = 0; x < MaxPages; ++x)
				{
					if (!m_map.HasPage(x, y))
					{
						continue;
					}

					// A page someone already loaded counts as done and is never streamed again
					if (m_map.IsPageLoaded(x, y))
					{
						m_claimed[PageIndex(x, y)] = true;
						m_resolved[PageIndex(x, y)] = true;
						++m_resolvedCount;
					}
					else
					{
						m_pages.emplace_back(x, y);
					}

					++m_pageCount;
				}
			}
		}

		if (!m_pages.empty())
		{
			m_thread = std::thread([this] { ReaderThread(); });
		}
	}

	PageStreamer::~PageStreamer()
	{
		m_stop = true;
		if (m_thread.joinable())
		{
			m_thread.join();
		}
	}

	bool PageStreamer::InstallReadyPages(const std::chrono::steady_clock::duration budget)
	{
		const auto deadline = std::chrono::steady_clock::now() + budget;

		do
		{
			uint32 index;
			ReadResult result;
			{
				std::scoped_lock lock{ m_readyMutex };
				if (m_ready.empty())
				{
					break;
				}

				const auto it = m_ready.begin();
				index = it->first;
				result = std::move(it->second);
				m_ready.erase(it);
			}

			Resolve(index, std::move(result));
		}
		while (std::chrono::steady_clock::now() < deadline);

		return IsComplete();
	}

	void PageStreamer::EnsureLoaded(const float minX, const float minZ, const float maxX, const float maxZ)
	{
		if (IsComplete())
		{
			return;
		}

		const int32 firstX = WorldToPage(std::min(minX, maxX));
		const int32 lastX = WorldToPage(std::max(minX, maxX));
		const int32 firstY = WorldToPage(std::min(minZ, maxZ));
		const int32 lastY = WorldToPage(std::max(minZ, maxZ));

		for (int32 y = firstY; y <= lastY; ++y)
		{
			for (int32 x = firstX; x <= lastX; ++x)
			{
				if (!m_map.HasPage(x, y) || m_resolved[PageIndex(x, y)])
				{
					continue;
				}

				LoadNow(x, y);
			}
		}
	}

	uint32 PageStreamer::PageIndex(const int32 x, const int32 y)
	{
		return static_cast<uint32>(y) * terrain::constants::MaxPages + static_cast<uint32>(x);
	}

	void PageStreamer::ReaderThread()
	{
		for (const auto& [x, y] : m_pages)
		{
			if (m_stop)
			{
				return;
			}

			// The owning thread may have loaded this page on demand already
			if (m_claimed[PageIndex(x, y)].exchange(true))
			{
				continue;
			}

			ReadResult result;
			result.success = Map::ReadPage(m_mapName, x, y, result.page, &result.error);

			std::scoped_lock lock{ m_readyMutex };
			m_ready.emplace(PageIndex(x, y), std::move(result));
		}
	}

	void PageStreamer::Resolve(const uint32 index, ReadResult&& result)
	{
		if (m_resolved[index])
		{
			return;
		}

		m_resolved[index] = true;
		++m_resolvedCount;

		if (!result.success || !m_map.AddPage(std::move(result.page)))
		{
			++m_failedCount;
			ELOG("Nav map '" << m_mapName << "': page " << (index % terrain::constants::MaxPages) << "x" << (index / terrain::constants::MaxPages)
				<< " failed to load (" << result.error << "), units will not find paths there");
		}
	}

	void PageStreamer::LoadNow(const int32 x, const int32 y)
	{
		const uint32 index = PageIndex(x, y);

		// Already read by the background thread? Then installing it is all that is left.
		{
			std::unique_lock lock{ m_readyMutex };
			if (const auto it = m_ready.find(index); it != m_ready.end())
			{
				ReadResult result = std::move(it->second);
				m_ready.erase(it);
				lock.unlock();

				Resolve(index, std::move(result));
				return;
			}
		}

		// Read it here. If the background thread is in the middle of reading the same page, its
		// copy arrives later and is discarded by Resolve. Reading twice is cheaper than waiting.
		m_claimed[index] = true;

		ReadResult result;
		result.success = Map::ReadPage(m_mapName, x, y, result.page, &result.error);
		Resolve(index, std::move(result));
	}
}
