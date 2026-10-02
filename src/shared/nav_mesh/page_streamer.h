// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "map.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mmo::nav
{
	/// @brief Loads every page of a nav Map without blocking the thread that owns the map.
	/// @details A background thread reads and parses the page files (Map::ReadPage). Detour is
	/// not thread-safe, so the owning thread installs the results itself, in time-boxed slices
	/// (InstallReadyPages). While streaming is incomplete the nav mesh has holes where pages are
	/// still pending: before a query, the owner must call EnsureLoaded for the area the query
	/// touches, which installs any missing page of that area synchronously.
	///
	/// All methods except the constructor and destructor belong to the owning thread.
	class PageStreamer final : public NonCopyable
	{
	public:
		/// @brief Starts streaming all pages of the given map.
		/// @param map The map to fill. Must outlive the streamer and must not have pages added or
		///        removed by anyone else while the streamer exists.
		explicit PageStreamer(Map& map);

		/// @brief Stops the background thread, waiting for the page it is currently reading.
		~PageStreamer() override;

	public:
		/// @brief Installs pages the background thread has read, until the budget is spent.
		/// @details Installs at least one ready page per call, so streaming always progresses.
		/// @param budget How long this call may spend installing pages.
		/// @return true when every page of the map has been dealt with (see IsComplete).
		bool InstallReadyPages(std::chrono::steady_clock::duration budget);

		/// @brief Makes sure every page overlapping a world-space XZ rectangle is installed.
		/// @details Pages the background thread has already read are taken from its results;
		/// the rest are read synchronously on the calling thread.
		void EnsureLoaded(float minX, float minZ, float maxX, float maxZ);

		/// @brief True once every page has been installed, or failed to load and was given up on.
		[[nodiscard]] bool IsComplete() const { return m_resolvedCount == m_pageCount; }

		/// @brief Number of pages the map has.
		[[nodiscard]] uint32 GetPageCount() const { return m_pageCount; }

		/// @brief Number of pages that failed to load.
		[[nodiscard]] uint32 GetFailedPageCount() const { return m_failedCount; }

	private:
		struct ReadResult
		{
			bool success = false;
			PageData page;
		};

		static uint32 PageIndex(int32 x, int32 y);

		void ReaderThread();

		/// @brief Installs a read result and marks its page as dealt with.
		void Resolve(uint32 index, ReadResult&& result);

		/// @brief Reads a page on the owning thread and installs it.
		void LoadNow(int32 x, int32 y);

	private:
		Map& m_map;
		const std::string m_mapName;

		/// Pages the map has, in the order the background thread reads them.
		std::vector<std::pair<int32, int32>> m_pages;

		/// Per page: set by whichever thread starts reading it first, so the other one skips it.
		std::unique_ptr<std::atomic<bool>[]> m_claimed;

		/// Per page, owning thread only: installed, or failed and given up on.
		std::vector<bool> m_resolved;
		uint32 m_pageCount = 0;
		uint32 m_resolvedCount = 0;
		uint32 m_failedCount = 0;

		std::mutex m_readyMutex;
		/// Pages the background thread has read that are not installed yet, by page index.
		std::unordered_map<uint32, ReadResult> m_ready;

		std::atomic<bool> m_stop { false };

		/// Declared last: the thread starts in the constructor and uses every member above.
		std::thread m_thread;
	};
}
