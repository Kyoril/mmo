// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bitmap.h"
#include "launcher_content.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace mmo
{
	/// Version 1 static publishing format, independent of game updates.
	struct LauncherManifest
	{
		/// Identity of the parsed text, so a same-content refresh preserves the reader.
		std::string revision;
		std::string heroImage;
		std::string heroTitle;
		std::string heroSubtitle;
		std::vector<LauncherArticle> news;
		std::vector<LauncherArticle> patches;
	};

	/// Immutable content handed from the content worker to the UI.
	struct RemoteLauncherContent
	{
		std::shared_ptr<const LauncherManifest> manifest;
		std::unordered_map<std::string, std::shared_ptr<const Bitmap>> images;
	};

	/// Independent, cancellable content fetcher with a last-known-good disk cache.
	class LauncherContentService final
	{
	public:
		/// Test seam; production uses verified HTTPS. Implementations must honor cancellation.
		using Fetch = std::function<bool(const std::string&, size_t, const std::atomic<bool>&, std::string&)>;
		explicit LauncherContentService(Fetch fetch = {});
		~LauncherContentService();
		/// Reads cached content immediately and refreshes once in the background.
		void Start(const std::string& url, const std::filesystem::path& cacheDirectory);
		/// Cancels networking and joins the content worker. Does not affect the updater.
		void Stop();
		/// Returns a new immutable snapshot only when the published content changed.
		bool TryGetContent(std::shared_ptr<const RemoteLauncherContent>& content, uint64& revision) const;
		/// Strictly parses bounded JSON; returns false without changing output on invalid input.
		static bool ParseManifest(const std::string& text, LauncherManifest& manifest);
		/// Only absolute HTTPS addresses without credentials or control characters are accepted.
		static bool IsHttpsUrl(const std::string& url);

	private:
		void Publish(const RemoteLauncherContent& content);
		void Refresh(std::string url, std::filesystem::path directory);
		Fetch m_fetch;
		std::atomic<bool> m_cancel{false};
		std::thread m_thread;
		mutable std::mutex m_mutex;
		std::shared_ptr<const RemoteLauncherContent> m_content;
		uint64 m_revision = 0;
	};
} // namespace mmo
