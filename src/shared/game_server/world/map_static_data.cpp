// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "map_static_data.h"

#include "server_collision_map.h"
#include "server_water_map.h"

#include "log/default_log_levels.h"
#include "nav_mesh/map.h"
#include "nav_mesh/page_streamer.h"

namespace mmo
{
	MapStaticData::MapStaticData(const std::string& directory)
		: m_directory(directory)
	{
		m_navMap = std::make_unique<nav::Map>(directory);

		// Stream the nav mesh pages in instead of loading them all here: on a large map that
		// held up the world thread. Queries load what they touch on demand until streaming
		// completes (see EnsureNavLoaded).
		m_pageStreamer = std::make_unique<nav::PageStreamer>(*m_navMap);
		m_navStreamStart = std::chrono::steady_clock::now();
		DLOG("MapStaticData: streaming " << m_pageStreamer->GetPageCount() << " nav page(s) for map '" << directory << "'");

		// Load world geometry for accurate 3D LOS. Kept even when empty, so per-instance views
		// still work for dynamic collision (doors); the empty-map early-out keeps queries cheap.
		auto collision = std::make_shared<ServerCollisionMap>(directory);
		if (collision->IsLoaded())
		{
			DLOG("MapStaticData: using geometry-based LOS for map '" << directory << "'");
		}
		else
		{
			WLOG("MapStaticData: no static collision geometry for map '"
				<< directory << "' — only dynamic objects (e.g. doors) will block IsInLineOfSight");
		}
		m_staticCollision = std::move(collision);

		// Load terrain water data for swim validation. Optional — null when the map has no water.
		auto waterMap = std::make_unique<ServerWaterMap>(directory);
		if (waterMap->IsLoaded())
		{
			m_waterMap = std::move(waterMap);
		}
	}

	MapStaticData::~MapStaticData() = default;

	void MapStaticData::EnsureNavLoaded(const float minX, const float minZ, const float maxX, const float maxZ)
	{
		if (m_pageStreamer)
		{
			m_pageStreamer->EnsureLoaded(minX, minZ, maxX, maxZ);
		}
	}

	void MapStaticData::Update()
	{
		if (!m_pageStreamer)
		{
			return;
		}

		// A slice of each 30 ms world tick, paid once per map no matter how many instances share it.
		// Reading happens on the streamer's thread; this only adds the read tiles to Detour.
		constexpr auto installBudget = std::chrono::milliseconds(4);
		if (!m_pageStreamer->InstallReadyPages(installBudget))
		{
			return;
		}

		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_navStreamStart);
		DLOG("MapStaticData: nav map '" << m_navMap->GetName() << "' complete, " << m_pageStreamer->GetPageCount() << " page(s) streamed in " << elapsed.count() << " ms"
			<< (m_pageStreamer->GetFailedPageCount() > 0 ? " (" + std::to_string(m_pageStreamer->GetFailedPageCount()) + " failed)" : std::string()));
		m_pageStreamer.reset();
	}

	std::shared_ptr<MapStaticData> MapStaticDataRegistry::Acquire(const std::string& directory)
	{
		auto& slot = m_maps[directory];
		if (auto existing = slot.lock())
		{
			return existing;
		}

		auto created = std::make_shared<MapStaticData>(directory);
		slot = created;
		return created;
	}

	void MapStaticDataRegistry::Update()
	{
		Prune();
		for (auto& [directory, weak] : m_maps)
		{
			if (const auto data = weak.lock())
			{
				data->Update();
			}
		}
	}

	size_t MapStaticDataRegistry::GetLiveCount()
	{
		Prune();
		return m_maps.size();
	}

	void MapStaticDataRegistry::Prune()
	{
		for (auto it = m_maps.begin(); it != m_maps.end(); )
		{
			if (it->second.expired())
			{
				it = m_maps.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}
